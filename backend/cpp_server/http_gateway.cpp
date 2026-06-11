/*
 * Copyright (C) 2026 QuanChan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "http_gateway.hpp"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "logger.hpp"
#include "config.hpp"
#include <nlohmann/json.hpp>
#include <oqs/oqs.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <memory>
#include <array>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <utility>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <curl/curl.h>

using json = nlohmann::json;

static OQS_SIG* GetThreadLocalSig();

// =============================================================================
// Helpers
// =============================================================================

static std::string random_hex(int len) {
    // Use OpenSSL CSPRNG instead of std::mt19937 for security-critical tokens
    int byte_count = (len + 1) / 2;
    std::vector<unsigned char> buf(byte_count);
    if (RAND_bytes(buf.data(), byte_count) != 1) {
        // Fallback should never happen in practice with a healthy OpenSSL
        Logger::Error("RAND_bytes failed in random_hex — OpenSSL CSPRNG unavailable");
        return "";
    }
    std::stringstream ss;
    for (int i = 0; i < byte_count; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)buf[i];
    }
    std::string result = ss.str();
    if ((int)result.size() > len) result.resize(len);
    return result;
}

static std::string hex_decode(const std::string& hex) {
    if (hex.size() % 2 != 0) return "";
    std::string bin;
    bin.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), nullptr, 16);
        bin.push_back(byte);
    }
    return bin;
}

static std::string hex_encode(const std::string& bin) {
    std::stringstream ss;
    for (unsigned char c : bin) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    }
    return ss.str();
}

static std::string hmac_sha512(const std::string& key, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    
    HMAC(EVP_sha512(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         hash, &hash_len);
         
    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

static size_t WriteStringCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string http_post_json(const std::string& url, const std::string& json_payload, const std::string& api_key) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response_string;
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!api_key.empty()) {
        headers = curl_slist_append(headers, ("x-api-key: " + api_key).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        Logger::Error("HTTP POST to " + url + " failed: " + std::string(curl_easy_strerror(res)));
        return "";
    }
    return response_string;
}

static EVP_PKEY* init_hermes_mlkem_key() {
    std::string key_file = "data/hermes_mlkem.key";
    EVP_PKEY* pkey = nullptr;
    
    if (std::filesystem::exists(key_file)) {
        FILE* f = fopen(key_file.c_str(), "rb");
        if (f) {
            pkey = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
            fclose(f);
            if (pkey) {
                Logger::Info("Loaded existing Hermes ML-KEM-1024 private key.");
                return pkey;
            }
        }
    }
    
    Logger::Info("Generating new Hermes ML-KEM-1024 private key...");
    pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "ML-KEM-1024");
    if (!pkey) {
        Logger::Error("Failed to generate ML-KEM-1024 keypair!");
        return nullptr;
    }
    
    std::filesystem::create_directories("data");
    FILE* f = fopen(key_file.c_str(), "wb");
    if (f) {
        PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        fclose(f);
        Logger::Info("Saved new Hermes ML-KEM-1024 private key to data/hermes_mlkem.key");
    } else {
        Logger::Error("Failed to save Hermes ML-KEM-1024 private key to file!");
    }
    
    return pkey;
}

static std::string get_hermes_mlkem_pubkey_pem(EVP_PKEY* pkey) {
    if (!pkey) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (PEM_write_bio_PUBKEY(bio, pkey) != 1) {
        BIO_free(bio);
        return "";
    }
    char* pem_data = nullptr;
    long pem_len = BIO_get_mem_data(bio, &pem_data);
    std::string pubkey_pem(pem_data, pem_len);
    BIO_free(bio);
    return pubkey_pem;
}

static std::string decapsulate_shared_secret(EVP_PKEY* pkey, const std::string& ciphertext) {
    if (!pkey || ciphertext.empty()) return "";
    
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (!ctx) return "";
    
    if (EVP_PKEY_decapsulate_init(ctx, nullptr) != 1) {
        EVP_PKEY_CTX_free(ctx);
        return "";
    }
    
    size_t shared_secret_len = 0;
    if (EVP_PKEY_decapsulate(ctx, nullptr, &shared_secret_len,
                             (const unsigned char*)ciphertext.data(), ciphertext.size()) != 1) {
        EVP_PKEY_CTX_free(ctx);
        return "";
    }
    
    std::vector<unsigned char> shared_secret(shared_secret_len);
    if (EVP_PKEY_decapsulate(ctx, shared_secret.data(), &shared_secret_len,
                             (const unsigned char*)ciphertext.data(), ciphertext.size()) != 1) {
        EVP_PKEY_CTX_free(ctx);
        return "";
    }
    
    EVP_PKEY_CTX_free(ctx);
    return std::string((char*)shared_secret.data(), shared_secret_len);
}

static std::string aes_256_gcm_decrypt(const std::string& ciphertext, const std::string& key, const std::string& iv) {
    if (key.size() != 32) return "";
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, 
                           reinterpret_cast<const unsigned char*>(key.data()), 
                           reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    std::string plaintext;
    plaintext.resize(ciphertext.size());
    int len = 0;

    if (ciphertext.size() < 16) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    std::string actual_ciphertext = ciphertext.substr(0, ciphertext.size() - 16);
    std::string tag = ciphertext.substr(ciphertext.size() - 16);

    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(&plaintext[0]), &len,
                          reinterpret_cast<const unsigned char*>(actual_ciphertext.data()), 
                          actual_ciphertext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    int plaintext_len = len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<char*>(tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    int ret = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&plaintext[plaintext_len]), &len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret > 0) {
        plaintext.resize(plaintext_len + len);
        return plaintext;
    }
    return "";
}

static std::string aes_256_gcm_encrypt(const std::string& plaintext, const std::string& key, const std::string& iv, std::string& out_tag) {
    if (key.size() != 32) return "";
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, 
                           reinterpret_cast<const unsigned char*>(key.data()), 
                           reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    std::string ciphertext;
    ciphertext.resize(plaintext.size() + 16);
    int len = 0;

    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(&ciphertext[0]), &len,
                          reinterpret_cast<const unsigned char*>(plaintext.data()), 
                          plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    int ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&ciphertext[ciphertext_len]), &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    ciphertext_len += len;
    ciphertext.resize(ciphertext_len);

    unsigned char tag_buf[16];
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag_buf) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    out_tag.assign(reinterpret_cast<char*>(tag_buf), 16);

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
}

static std::string get_extension(const std::string& filename) {
    auto pos = filename.rfind('.');
    return (pos == std::string::npos) ? "" : filename.substr(pos);
}

static std::string extension_from_content_type(const std::string& content_type) {
    std::string lowered = content_type;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lowered == "image/png") return ".png";
    if (lowered == "image/jpeg" || lowered == "image/jpg" || lowered == "image/pjpeg") return ".jpg";
    if (lowered == "image/webp") return ".webp";
    if (lowered == "image/gif") return ".gif";
    if (lowered == "image/bmp") return ".bmp";
    if (lowered == "image/svg+xml") return ".svg";
    if (lowered == "video/mp4") return ".mp4";
    if (lowered == "video/webm") return ".webm";
    if (lowered == "video/quicktime") return ".mov";
    return "";
}

static std::string extension_from_magic_bytes(const std::string& data) {
    if (data.size() >= 8 &&
        static_cast<uint8_t>(data[0]) == 0x89 &&
        data[1] == 'P' && data[2] == 'N' && data[3] == 'G' &&
        static_cast<uint8_t>(data[4]) == 0x0D &&
        static_cast<uint8_t>(data[5]) == 0x0A &&
        static_cast<uint8_t>(data[6]) == 0x1A &&
        static_cast<uint8_t>(data[7]) == 0x0A) {
        return ".png";
    }

    if (data.size() >= 3 &&
        static_cast<uint8_t>(data[0]) == 0xFF &&
        static_cast<uint8_t>(data[1]) == 0xD8 &&
        static_cast<uint8_t>(data[2]) == 0xFF) {
        return ".jpg";
    }

    if (data.size() >= 12 &&
        data.compare(0, 4, "RIFF") == 0 &&
        data.compare(8, 4, "WEBP") == 0) {
        return ".webp";
    }

    if (data.size() >= 6 &&
        (data.compare(0, 6, "GIF87a") == 0 || data.compare(0, 6, "GIF89a") == 0)) {
        return ".gif";
    }

    if (data.size() >= 2 &&
        data[0] == 'B' && data[1] == 'M') {
        return ".bmp";
    }

    if (data.size() >= 12 && data.compare(4, 4, "ftyp") == 0) {
        return ".mp4";
    }

    return "";
}

static std::string normalized_upload_extension(const std::string& filename,
                                               const std::string& content_type,
                                               const std::string& content) {
    std::string ext = get_extension(filename);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (ext == ".jpeg" || ext == ".jfif" || ext == ".pjpeg") {
        return ".jpg";
    }

    if (!ext.empty()) {
        return ext;
    }

    ext = extension_from_content_type(content_type);
    if (!ext.empty()) {
        return ext;
    }

    return extension_from_magic_bytes(content);
}

// Base64 encode using OpenSSL EVP (no external dependency)
static std::string base64_encode(const uint8_t* data, size_t len) {
    size_t out_len = 4 * ((len + 2) / 3) + 1;
    std::string out(out_len, '\0');
    int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data, static_cast<int>(len));
    out.resize(written);
    return out;
}

static std::vector<uint8_t> base64_decode(const std::string& input) {
    if (input.empty()) return {};
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    std::string cleaned = input.substr(start, end - start);
    if (cleaned.empty()) return {};

    std::replace(cleaned.begin(), cleaned.end(), '-', '+');
    std::replace(cleaned.begin(), cleaned.end(), '_', '/');

    while (cleaned.length() % 4 != 0) {
        cleaned += '=';
    }

    size_t input_len = cleaned.length();
    size_t padding = 0;
    if (input_len > 0 && cleaned[input_len - 1] == '=') {
        padding++;
        if (input_len > 1 && cleaned[input_len - 2] == '=') {
            padding++;
        }
    }

    size_t estimate = (input_len * 3) / 4;
    std::vector<uint8_t> out(estimate + 128);

    int decoded = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(cleaned.data()), static_cast<int>(input_len));
    if (decoded < 0 || static_cast<size_t>(decoded) < padding) {
        return {};
    }

    size_t actual_size = static_cast<size_t>(decoded) - padding;
    out.resize(actual_size);
    return out;
}

static std::string pkey_algorithm_name(EVP_PKEY* pkey) {
    if (!pkey) return "unknown";
    const char* type_name = EVP_PKEY_get0_type_name(pkey);
    if (type_name && *type_name) {
        return type_name;
    }
    switch (EVP_PKEY_base_id(pkey)) {
        case EVP_PKEY_RSA: return "RSA";
        case EVP_PKEY_RSA_PSS: return "RSA-PSS";
        case EVP_PKEY_EC: return "EC";
        case EVP_PKEY_ED25519: return "Ed25519";
        default: return OBJ_nid2sn(EVP_PKEY_id(pkey)) ? OBJ_nid2sn(EVP_PKEY_id(pkey)) : "unknown";
    }
}

static std::string ascii_lower_copy(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

static std::string trim_copy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

static std::string parse_cookie(const std::string& cookie_header, const std::string& name) {
    if (cookie_header.empty()) {
        return "";
    }
    std::string search = name + "=";
    size_t start = 0;
    while (true) {
        size_t pos = cookie_header.find(search, start);
        if (pos == std::string::npos) {
            break;
        }
        if (pos == 0 || cookie_header[pos - 1] == ' ' || cookie_header[pos - 1] == ';') {
            size_t val_start = pos + search.length();
            size_t val_end = cookie_header.find(';', val_start);
            if (val_end == std::string::npos) {
                return trim_copy(cookie_header.substr(val_start));
            } else {
                return trim_copy(cookie_header.substr(val_start, val_end - val_start));
            }
        }
        start = pos + 1;
    }
    return "";
}

static std::string get_client_ip(const httplib::Request& req) {
    if (req.has_header("CF-Connecting-IP")) {
        return trim_copy(req.get_header_value("CF-Connecting-IP"));
    }
    if (req.has_header("X-Forwarded-For")) {
        std::string xff = req.get_header_value("X-Forwarded-For");
        size_t comma = xff.find(',');
        if (comma != std::string::npos) {
            return trim_copy(xff.substr(0, comma));
        }
        return trim_copy(xff);
    }
    return req.remote_addr;
}


static bool contains_ascii_case_insensitive(const std::string& haystack, const std::string& needle) {
    return ascii_lower_copy(haystack).find(ascii_lower_copy(needle)) != std::string::npos;
}

static std::string sha256_hex(const std::string& value) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return "";
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_len = 0;
    std::ostringstream out;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1
        || EVP_DigestUpdate(ctx, value.data(), value.size()) != 1
        || EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    EVP_MD_CTX_free(ctx);
    for (unsigned int i = 0; i < digest_len; ++i) {
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return out.str();
}

static void apply_sse_headers(httplib::Response& res) {
    res.set_header("Cache-Control", "no-cache, no-transform");
    res.set_header("Connection", "keep-alive");
    res.set_header("X-Accel-Buffering", "no");
}

template <typename StateBuilder, typename PayloadBuilder>
static void respond_with_long_poll_sse(const httplib::Request& req,
                                       httplib::Response& res,
                                       const std::string& event_name,
                                       StateBuilder&& state_builder,
                                       PayloadBuilder&& payload_builder) {
    const std::string last_event_id = req.has_header("Last-Event-ID")
        ? trim_copy(req.get_header_value("Last-Event-ID"))
        : "";

    json current_state = state_builder();
    std::string cursor = sha256_hex(current_state.dump());
    bool changed = last_event_id.empty() || cursor != last_event_id;

    for (int attempt = 0; !changed && attempt < 24; ++attempt) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        current_state = state_builder();
        cursor = sha256_hex(current_state.dump());
        changed = cursor != last_event_id;
    }

    const std::string emitted_event = changed ? event_name : "timeout";
    json payload = payload_builder(current_state, cursor, changed);
    payload["cursor"] = cursor;

    std::ostringstream stream;
    stream << "retry: 1000\n";
    stream << "id: " << cursor << "\n";
    stream << "event: " << emitted_event << "\n";
    stream << "data: " << payload.dump() << "\n\n";

    apply_sse_headers(res);
    res.set_content(stream.str(), "text/event-stream");
}

struct TLSRuntimeState {
    std::mutex mutex;
    std::string configured_groups;
    std::string configured_ciphersuites;
    bool handshake_observed = false;
    std::string latest_protocol;
    std::string latest_cipher;
    std::string latest_group;
    bool latest_group_is_pqc_or_hybrid = false;
};

static bool group_name_looks_pqc_or_hybrid(const std::string& group_name) {
    return contains_ascii_case_insensitive(group_name, "mlkem")
        || contains_ascii_case_insensitive(group_name, "kyber");
}

static std::string tls_group_name(SSL* ssl) {
#if OPENSSL_VERSION_NUMBER >= 0x30500000L
    const char* group_name = SSL_get0_group_name(ssl);
    if (group_name) {
        return group_name;
    }
#endif
    return "";
}

static int tls_runtime_state_index() {
    static int ex_data_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return ex_data_index;
}

static void tls_info_callback(const SSL* ssl, int where, int ret) {
    (void)ret;
    if ((where & SSL_CB_HANDSHAKE_DONE) == 0) {
        return;
    }

    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
    auto* state = static_cast<TLSRuntimeState*>(SSL_CTX_get_ex_data(ctx, tls_runtime_state_index()));
    if (!state) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    state->handshake_observed = true;
    state->latest_protocol = SSL_get_version(ssl);
    if (const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl)) {
        state->latest_cipher = SSL_CIPHER_get_name(cipher);
    } else {
        state->latest_cipher.clear();
    }
    state->latest_group = tls_group_name(const_cast<SSL*>(ssl));
    state->latest_group_is_pqc_or_hybrid = group_name_looks_pqc_or_hybrid(state->latest_group);
}

static json read_certificate_facts(const std::string& cert_file) {
    json result = {
        {"cert_file", cert_file},
        {"loaded", false},
        {"subject", ""},
        {"issuer", ""},
        {"signature_algorithm", ""},
        {"public_key_algorithm", ""},
        {"public_key_bits", 0},
        {"subject_alt_names", json::array()}
    };

    FILE* fp = fopen(cert_file.c_str(), "rb");
    if (!fp) {
        result["error"] = "Failed to open certificate file";
        return result;
    }

    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);
    if (!cert) {
        result["error"] = "Failed to parse certificate";
        return result;
    }

    char subject[512] = {0};
    char issuer[512] = {0};
    X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject) - 1);
    X509_NAME_oneline(X509_get_issuer_name(cert), issuer, sizeof(issuer) - 1);
    result["loaded"] = true;
    result["subject"] = subject;
    result["issuer"] = issuer;
    result["signature_algorithm"] = OBJ_nid2ln(X509_get_signature_nid(cert));

    EVP_PKEY* pubkey = X509_get_pubkey(cert);
    if (pubkey) {
        result["public_key_algorithm"] = pkey_algorithm_name(pubkey);
        result["public_key_bits"] = EVP_PKEY_bits(pubkey);
        EVP_PKEY_free(pubkey);
    }

    GENERAL_NAMES* san_names = static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
    if (san_names) {
        const int count = sk_GENERAL_NAME_num(san_names);
        for (int i = 0; i < count; ++i) {
            const GENERAL_NAME* current = sk_GENERAL_NAME_value(san_names, i);
            if (current->type == GEN_DNS) {
                const unsigned char* data = ASN1_STRING_get0_data(current->d.dNSName);
                result["subject_alt_names"].push_back(std::string(reinterpret_cast<const char*>(data), ASN1_STRING_length(current->d.dNSName)));
            } else if (current->type == GEN_IPADD) {
                const unsigned char* data = ASN1_STRING_get0_data(current->d.iPAddress);
                int len = ASN1_STRING_length(current->d.iPAddress);
                std::ostringstream ip;
                for (int j = 0; j < len; ++j) {
                    if (j) ip << '.';
                    ip << static_cast<int>(data[j]);
                }
                result["subject_alt_names"].push_back(ip.str());
            }
        }
        GENERAL_NAMES_free(san_names);
    }

    X509_free(cert);
    return result;
}

static bool certificate_uses_pqc_keys(const json& cert_info) {
    const std::string public_key_alg = cert_info.value("public_key_algorithm", "");
    const std::string signature_alg = cert_info.value("signature_algorithm", "");
    return contains_ascii_case_insensitive(public_key_alg, "dilithium")
        || contains_ascii_case_insensitive(public_key_alg, "ml-dsa")
        || contains_ascii_case_insensitive(signature_alg, "dilithium")
        || contains_ascii_case_insensitive(signature_alg, "ml-dsa");
}

static json latest_tls_handshake_json(TLSRuntimeState& tls_state) {
    std::lock_guard<std::mutex> lock(tls_state.mutex);
    return {
        {"observed", tls_state.handshake_observed},
        {"protocol", tls_state.latest_protocol},
        {"cipher", tls_state.latest_cipher},
        {"group", tls_state.latest_group},
        {"group_is_pqc_or_hybrid", tls_state.latest_group_is_pqc_or_hybrid}
    };
}

static std::string transport_disclosure_label(const json& cert_info, TLSRuntimeState& tls_state) {
    const bool pqc_certificate = certificate_uses_pqc_keys(cert_info);
    std::lock_guard<std::mutex> lock(tls_state.mutex);
    const std::string certificate_label = pqc_certificate ? "PQC" : "CLASSICAL";

    if (!tls_state.handshake_observed) {
        return "CERT=" + certificate_label + "; POLICY=" + tls_state.configured_groups + "; SESSION-PQC=PENDING";
    }

    if (tls_state.latest_group.empty()) {
        return "CERT=" + certificate_label + "; POLICY=" + tls_state.configured_groups + "; LATEST-GROUP=UNAVAILABLE; SESSION-PQC=UNKNOWN";
    }

    return "CERT=" + certificate_label + "; POLICY=" + tls_state.configured_groups
        + "; LATEST-GROUP=" + tls_state.latest_group
        + "; SESSION-PQC=" + std::string(tls_state.latest_group_is_pqc_or_hybrid ? "YES" : "NO");
}

static bool sign_snapshot_payload(const std::string& payload,
                                  const std::vector<uint8_t>& d_pub,
                                  const std::vector<uint8_t>& d_sec,
                                  const std::string& block_field,
                                  json& response,
                                  std::string& error) {
    OQS_SIG* sig = GetThreadLocalSig();
    if (sig == nullptr) {
        error = "Cryptographic context unavailable (OQS_SIG allocation failed)";
        return false;
    }

    std::vector<uint8_t> signature(OQS_SIG_dilithium_5_length_signature);
    size_t sig_len = 0;
    if (OQS_SIG_sign(sig, signature.data(), &sig_len,
                     reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
                     d_sec.data()) != OQS_SUCCESS) {
        error = "Dilithium5 signing failed";
        return false;
    }

    response[block_field] = payload;
    response["signature"] = base64_encode(signature.data(), sig_len);
    response["pubkey"] = base64_encode(d_pub.data(), d_pub.size());
    response["signature_algorithm"] = "Dilithium5";
    return true;
}

// =============================================================================
// Thread-local OQS_SIG context (RAII, allocated once per httplib worker thread)
// =============================================================================

struct OQSSigDeleter {
    void operator()(OQS_SIG* sig) const noexcept {
        if (sig) OQS_SIG_free(sig);
    }
};
using OQSSigPtr = std::unique_ptr<OQS_SIG, OQSSigDeleter>;

// Returns a reusable OQS_SIG* for the current worker thread. May return nullptr.
static OQS_SIG* GetThreadLocalSig() {
    static thread_local OQSSigPtr tl_sig(OQS_SIG_new(OQS_SIG_alg_dilithium_5));
    return tl_sig.get();
}

static bool VerifyRequestSignature(const httplib::Request& req, const std::string& body_str, DBManager& db, std::string& out_identity_hash) {
    std::string sig_header = req.has_header("X-QC-Signature") ? req.get_header_value("X-QC-Signature") : "";
    std::string timestamp_header = req.has_header("X-QC-Timestamp") ? req.get_header_value("X-QC-Timestamp") : "";
    std::string identity_header = req.has_header("X-QC-Identity") ? req.get_header_value("X-QC-Identity") : "";

    if (sig_header.empty() || timestamp_header.empty() || identity_header.empty()) {
        return false;
    }

    try {
        int64_t request_time = std::stoll(timestamp_header);
        int64_t current_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        if (std::abs(current_time - request_time) > 300) {
            return false;
        }
    } catch (...) {
        return false;
    }

    json profile = db.GetProfile(identity_header);
    std::string b64_pubkey = "";
    if (profile.contains("error")) {
        if (req.path == "/api/profile/update") {
            try {
                auto j = json::parse(body_str);
                b64_pubkey = j.value("pqc_identity_public_key", "");
            } catch (...) {}
        }
    } else {
        b64_pubkey = profile.value("pqc_identity_public_key", "");
    }

    if (b64_pubkey.empty()) {
        return false;
    }

    std::string msg = timestamp_header + ":" + req.path + ":" + body_str;

    std::vector<uint8_t> pubkey_bytes = base64_decode(b64_pubkey);
    std::vector<uint8_t> sig_bytes = base64_decode(sig_header);

    OQS_SIG* sig_ctx = GetThreadLocalSig();
    if (!sig_ctx) return false;

    if (pubkey_bytes.size() != OQS_SIG_dilithium_5_length_public_key ||
        sig_bytes.size() != OQS_SIG_dilithium_5_length_signature) {
        return false;
    }

    int verify_res = OQS_SIG_verify(
        sig_ctx,
        reinterpret_cast<const uint8_t*>(msg.data()), msg.size(),
        sig_bytes.data(), sig_bytes.size(),
        pubkey_bytes.data()
    );

    if (verify_res == OQS_SUCCESS) {
        out_identity_hash = identity_header;
        return true;
    }
    return false;
}

struct RateLimitEntry {
    int count = 0;
    std::chrono::steady_clock::time_point window_start;
};
static std::unordered_map<std::string, RateLimitEntry> hermes_rate_limits;
static std::mutex rate_limit_mutex;

// =============================================================================
// HTTP Server
// =============================================================================

void RunHTTPServer(DBManager& db_manager, int port,
                   const std::string& cert_dir,
                   const std::vector<uint8_t>& d_pub,
                   const std::vector<uint8_t>& d_sec) {

    EVP_PKEY* hermes_pkey = init_hermes_mlkem_key();

    // Configure HTTPS with the fixed production certificate set.
    const std::string cert_file = cert_dir + "/server.crt";
    const std::string key_file = cert_dir + "/server.key";
    httplib::SSLServer svr(cert_file.c_str(), key_file.c_str());
    if (!svr.is_valid()) {
        Logger::Fatal("HTTPS gateway failed to load TLS certificate/key material.");
        std::exit(EXIT_FAILURE);
    }
    const unsigned int hardware_threads = std::max(4u, std::thread::hardware_concurrency());
    const size_t worker_count = std::min<size_t>(std::max<size_t>(hardware_threads * 2, 8), 64);
    svr.new_task_queue = [worker_count] { return new httplib::ThreadPool(static_cast<int>(worker_count)); };
    Logger::Info("Configured HTTPS worker pool with " + std::to_string(worker_count) + " threads.");
    const json cert_info = read_certificate_facts(cert_file);
    const bool pqc_certificate_detected = certificate_uses_pqc_keys(cert_info);

    TLSRuntimeState tls_state;
    tls_state.configured_groups = std::getenv("QC_TLS_GROUPS") && std::strlen(std::getenv("QC_TLS_GROUPS")) > 0
        ? std::getenv("QC_TLS_GROUPS")
        : "X25519MLKEM768:X25519";
    tls_state.configured_ciphersuites = std::getenv("QC_TLS_CIPHERSUITES") && std::strlen(std::getenv("QC_TLS_CIPHERSUITES")) > 0
        ? std::getenv("QC_TLS_CIPHERSUITES")
        : "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256";

    SSL_CTX* ssl_ctx = svr.ssl_context();
    if (!ssl_ctx) {
        Logger::Fatal("HTTPS gateway could not obtain SSL_CTX from cpp-httplib.");
        std::exit(EXIT_FAILURE);
    }
    if (SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION) != 1) {
        Logger::Fatal("Failed to enforce TLS 1.3 minimum for HTTPS gateway.");
        std::exit(EXIT_FAILURE);
    }
    if (SSL_CTX_set1_groups_list(ssl_ctx, tls_state.configured_groups.c_str()) != 1) {
        Logger::Fatal("Failed to configure PQC TLS groups: " + tls_state.configured_groups);
        std::exit(EXIT_FAILURE);
    }
    if (SSL_CTX_set_ciphersuites(ssl_ctx, tls_state.configured_ciphersuites.c_str()) != 1) {
        Logger::Fatal("Failed to configure TLS 1.3 ciphersuites for HTTPS gateway.");
        std::exit(EXIT_FAILURE);
    }
    SSL_CTX_set_ex_data(ssl_ctx, tls_runtime_state_index(), &tls_state);
    SSL_CTX_set_info_callback(ssl_ctx, tls_info_callback);

    // â”€â”€â”€ CORS & Auth Middleware â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Allowed CORS origins — only the production domain and common dev origins
    static const std::vector<std::string> allowed_origins = {
        "https://quanchan.online",
        "https://www.quanchan.online",
        "http://localhost:5173",
        "http://localhost:3000"
    };

    svr.set_pre_routing_handler([&cert_info, &tls_state](const httplib::Request& req, httplib::Response& res) {
        if (req.has_header("Origin")) {
            std::string origin = req.get_header_value("Origin");
            bool origin_allowed = false;
            for (const auto& ao : allowed_origins) {
                if (origin == ao) { origin_allowed = true; break; }
            }
            if (origin_allowed) {
                res.set_header("Access-Control-Allow-Origin", origin);
                res.set_header("Access-Control-Allow-Credentials", "true");
            }
            // If origin not allowed, no CORS headers are set — browser blocks the request
        }
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, X-API-Key, X-CSRF-Token");
        res.set_header("Access-Control-Expose-Headers", "X-PQC-Cipher, X-PQC-Proof-Endpoint");
        res.set_header("X-PQC-Cipher", transport_disclosure_label(cert_info, tls_state));
        res.set_header("X-PQC-Proof-Endpoint", "/api/crypto/tls-proof");
        
        // â”€â”€â”€ Security Hardening Headers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        res.set_header("X-Frame-Options", "DENY");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
        res.set_header("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-eval' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; media-src 'self' data: blob:; connect-src 'self' wss: ws:; font-src 'self' data:; frame-ancestors 'none';");

        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }

        // Reject credentials in query string or path target
        if (req.target.find("founder_token") != std::string::npos ||
            req.target.find("session_token") != std::string::npos) {
            res.status = 400;
            res.set_content("{\"error\": \"Credentials in URL query or path parameters are prohibited for security.\"}", "application/json");
            return httplib::Server::HandlerResponse::Handled;
        }

        // Stateless CSRF Protection
        bool is_write_method = (req.method == "POST" || req.method == "PUT" ||
                                req.method == "DELETE" || req.method == "PATCH");
        bool is_nowpayments_webhook = (req.path == "/api/payments/webhook");
        bool is_hermes_endpoint = (req.path == "/api/hermes/v1/chat/completions");
        bool skip_csrf = is_nowpayments_webhook || is_hermes_endpoint;

        std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
        std::string cookie_csrf = parse_cookie(cookie_header, "csrf_token");

        if (is_write_method && !skip_csrf) {
            std::string header_csrf = req.has_header("X-CSRF-Token") ? req.get_header_value("X-CSRF-Token") : "";
            if (cookie_csrf.empty() || header_csrf != cookie_csrf) {
                res.status = 403;
                res.set_content("{\"error\": \"CSRF token validation failed.\"}", "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        // Generate csrf_token if missing
        if (cookie_csrf.empty()) {
            std::string new_csrf = random_hex(32);
            res.headers.insert(std::make_pair("Set-Cookie", "csrf_token=" + new_csrf + "; SameSite=Strict; Path=/"));
        }

        if (req.path.find("/api/") == 0 && req.path.find("/api/live/") != 0) {
            const auto& config = Config::Instance().Get();
            if (config.http_api_key != "changeme") {
                if (!req.has_header("X-API-Key") || req.get_header_value("X-API-Key") != config.http_api_key) {
                    res.status = 401;
                    res.set_content("{\"error\": \"Unauthorized\"}", "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
            }
        }

        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Static file mounts
    svr.set_mount_point("/", "/app/www");
    std::filesystem::create_directories("/app/uploads");
    svr.set_mount_point("/uploads", "/app/uploads");

    // â”€â”€â”€ Original PQC Endpoints â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json({{"status", "SERVING"}}).dump(), "application/json");
    });

    svr.Get("/api/crypto/tls-proof", [&](const httplib::Request&, httplib::Response& res) {
        const json latest_handshake = latest_tls_handshake_json(tls_state);
        const bool session_pqc_verified = latest_handshake.value("observed", false)
            && latest_handshake.value("protocol", "") == "TLSv1.3"
            && latest_handshake.value("group_is_pqc_or_hybrid", false);
        json proof;
        proof["openssl_version"] = OpenSSL_version(OPENSSL_VERSION);
        proof["openssl_config"] = std::getenv("OPENSSL_CONF") ? std::getenv("OPENSSL_CONF") : "";
        proof["x_pqc_cipher_header"] = transport_disclosure_label(cert_info, tls_state);
        proof["pqc_certificate_detected"] = pqc_certificate_detected;
        proof["transport_claim_mode"] = "server-observed-live-handshake";
        proof["tls_policy"] = {
            {"minimum_protocol", "TLSv1.3"},
            {"configured_groups", tls_state.configured_groups},
            {"configured_ciphersuites", tls_state.configured_ciphersuites}
        };
        proof["latest_handshake"] = latest_handshake;
        proof["session_pqc_verified"] = session_pqc_verified;
        proof["certificate_note"] = "Certificate algorithm and negotiated key exchange are reported separately. Classical origin certificates can still be used with hybrid PQC key agreement.";
        proof["negotiated_cipher_available"] = latest_handshake.value("observed", false);
        proof["negotiated_cipher_note"] = "The server records the latest observed TLS handshake from OpenSSL callbacks. This is live runtime evidence, but not a request-bound proof object from cpp-httplib handlers.";
        proof["live_transport_classification"] = session_pqc_verified
            ? "tls13-pqc-or-hybrid-kex-observed"
            : (latest_handshake.value("observed", false)
                ? "tls13-classical-kex-observed"
                : "pqc-policy-configured-awaiting-live-handshake");
        proof["certificate"] = cert_info;
        res.set_content(proof.dump(), "application/json");
    });

    svr.Get("/api/crypto/status", [&](const httplib::Request&, httplib::Response& res) {
        const json latest_handshake = latest_tls_handshake_json(tls_state);
        const bool session_pqc_verified = latest_handshake.value("observed", false)
            && latest_handshake.value("protocol", "") == "TLSv1.3"
            && latest_handshake.value("group_is_pqc_or_hybrid", false);
        json status;
        status["transport"] = {
            {"https_gateway", true},
            {"grpc_tls_enabled", true},
            {"grpc_tls_runtime_verified", false},
            {"grpc_tls_note", "gRPC is built against the same OpenSSL 3.5.5 runtime and certificate set, but this process does not currently capture live negotiated group data for gRPC connections."},
            {"openssl_provider_model", "OpenSSL 3.5 native ML-KEM/ML-DSA"},
            {"openssl_version", OpenSSL_version(OPENSSL_VERSION)},
            {"x_pqc_cipher_header", transport_disclosure_label(cert_info, tls_state)},
            {"header_mode", "server-observed-live-handshake"},
            {"minimum_protocol", "TLSv1.3"},
            {"configured_groups", tls_state.configured_groups},
            {"configured_ciphersuites", tls_state.configured_ciphersuites},
            {"latest_handshake", latest_handshake},
            {"negotiated_cipher_available", latest_handshake.value("observed", false)},
            {"pqc_certificate_detected", pqc_certificate_detected},
            {"session_pqc_verified", session_pqc_verified},
            {"certificate_note", "Certificate algorithm and negotiated key exchange are reported separately. Classical origin certificates can still be used with hybrid PQC key agreement."},
            {"certificate", cert_info}
        };
        status["signatures"] = {
            {"thread_snapshots", "Dilithium5"},
            {"dm_snapshots", "Dilithium5"},
            {"identity_binding", "ML-DSA-87"}
        };
        status["messaging"] = {
            {"dm_e2ee_scheme", "ML-KEM-1024 + AES-256-GCM"},
            {"dm_attachment_e2ee", false},
            {"dm_storage_encryption", "AES-256-GCM"}
        };
        status["identity"] = {
            {"display_hash_scheme", "SHA-256(ML-DSA-87 public key)"},
            {"public_identity_scheme", "ML-DSA-87 rooted identity with Ed25519 compatibility binding"},
            {"pqc_identity_binding_scheme", "ML-DSA-87"},
            {"dm_kem_scheme", "ML-KEM-1024"}
        };
        status["storage"] = {
            {"server_storage_encryption", "AES-256-GCM"},
            {"upload_transport", "HTTPS"},
            {"s3_signature_scheme", "AWS SigV4 / HMAC-SHA256"}
        };
        res.set_content(status.dump(), "application/json");
    });

    svr.Post("/api/store", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            if (!j.contains("message")) { res.status = 400; res.set_content("{\"error\":\"Missing 'message'\"}", "application/json"); return; }
            int64_t id = db_manager.InsertMessage(j["message"]);
            res.set_content(json({{"id", id}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    svr.Get("/api/retrieve", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("id")) { res.status = 400; res.set_content("{\"error\":\"Missing 'id'\"}", "application/json"); return; }
        try {
            int64_t id = std::stoll(req.get_param_value("id"));
            std::string msg = db_manager.GetMessage(id);
            if (msg.empty()) { res.status = 404; res.set_content("{\"error\":\"Not found\"}", "application/json"); return; }
            res.set_content(json({{"message", msg}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // â”€â”€â”€ Imageboard Endpoints â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    // GET /api/boards
    svr.Get("/api/boards", [&](const httplib::Request&, httplib::Response& res) {
        try {
            res.set_content(db_manager.GetAllBoards().dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/boards/:id
    svr.Get(R"(/api/boards/([a-zA-Z0-9]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            json board = db_manager.GetBoard(req.matches[1]);
            if (board.empty()) { res.status = 404; res.set_content("{\"error\":\"Board not found\"}", "application/json"); return; }
            res.set_content(board.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/threads?board_id=g&page=1&limit=20&archived=false
    svr.Get("/api/threads", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string board_id = req.has_param("board_id") ? req.get_param_value("board_id") : "";
            if (board_id.empty()) { res.status = 400; res.set_content("{\"error\":\"board_id required\"}", "application/json"); return; }
            int page  = req.has_param("page")  ? std::stoi(req.get_param_value("page"))  : 1;
            int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 20;
            bool archived = req.has_param("archived") && req.get_param_value("archived") == "true";
            res.set_content(db_manager.GetThreads(board_id, page, limit, archived).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // â”€â”€â”€ GET /api/threads/:id â€” DILITHIUM5 BATCH SIGNED â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    svr.Get(R"(/api/threads/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int64_t thread_id = std::stoll(req.matches[1]);
            json thread_data = db_manager.GetThread(thread_id);
            if (thread_data.empty()) { res.status = 404; res.set_content("{\"error\":\"Thread not found\"}", "application/json"); return; }

            std::string payload = thread_data.dump();
            json response;
            std::string error;
            if (!sign_snapshot_payload(payload, d_pub, d_sec, "thread_block", response, error)) {
                res.status = 500;
                res.set_content(json({{"error", error}}).dump(), "application/json");
                return;
            }
            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/threads
    svr.Post("/api/threads", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string client_ip = get_client_ip(req);
            std::string ip_ban_reason;
            if (db_manager.IsIpBanned(client_ip, &ip_ban_reason)) {
                res.status = 403;
                res.set_content(json({{"error", ip_ban_reason.empty() ? "Your IP address is banned" : ip_ban_reason}}).dump(), "application/json");
                return;
            }

            auto j = json::parse(req.body);
            std::string board_id = j.value("board_id", "");
            std::string content  = j.value("content", "");
            std::string author_hash = trim_copy(j.value("author_hash", ""));
            if (board_id.empty() || content.empty()) { res.status = 400; res.set_content("{\"error\":\"board_id and content required\"}", "application/json"); return; }
            json result = db_manager.CreateThread(board_id, j.value("subject", ""), content,
                j.value("name", "Anonymous"), j.value("image_url", ""), j.value("encrypted_content", ""), author_hash);
            res.status = 201;
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            const std::string msg = e.what();
            res.status = contains_ascii_case_insensitive(msg, "banned") ? 403 : 500;
            res.set_content(json({{"error", msg}}).dump(), "application/json");
        }
    });

    // PATCH /api/threads/:id/archive — requires admin session
    svr.Patch(R"(/api/threads/(\d+)/archive)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string actor_hash;
            try {
                auto j = json::parse(req.body);
                actor_hash = trim_copy(j.value("actor_hash", ""));
            } catch (...) {
                actor_hash = req.has_param("actor_hash") ? trim_copy(req.get_param_value("actor_hash")) : "";
            }
            if (actor_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            if (!db_manager.IsModeratorAuthorized(actor_hash, founder_session_cookie)) {
                res.status = 403;
                res.set_content("{\"error\":\"Authorization required to archive threads\"}", "application/json");
                return;
            }

            int64_t tid = std::stoll(req.matches[1]);
            db_manager.ArchiveThread(tid);
            res.set_content(json({{"id", tid}, {"archived", true}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/posts
    svr.Post("/api/posts", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string client_ip = get_client_ip(req);
            std::string ip_ban_reason;
            if (db_manager.IsIpBanned(client_ip, &ip_ban_reason)) {
                res.status = 403;
                res.set_content(json({{"error", ip_ban_reason.empty() ? "Your IP address is banned" : ip_ban_reason}}).dump(), "application/json");
                return;
            }

            auto j = json::parse(req.body);
            int64_t thread_id = j.value("thread_id", (int64_t)0);
            std::string content = j.value("content", "");
            std::string author_hash = trim_copy(j.value("author_hash", ""));
            if (thread_id == 0 || content.empty()) { res.status = 400; res.set_content("{\"error\":\"thread_id and content required\"}", "application/json"); return; }
            json result = db_manager.CreatePost(thread_id, content, j.value("name", "Anonymous"),
                j.value("image_url", ""), j.value("encrypted_content", ""), j.value("sage", false), author_hash);
            res.status = 201;
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            std::string msg = e.what();
            res.status = contains_ascii_case_insensitive(msg, "banned") ? 403 : (msg == "Thread is locked") ? 403 : (msg == "Thread not found") ? 404 : 500;
            res.set_content(json({{"error", msg}}).dump(), "application/json");
        }
    });
    // â”€â”€â”€ V4 Social Endpoints â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    // GET /api/profile/:hash
    svr.Get(R"(/api/profile/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            res.set_content(db_manager.GetProfile(req.matches[1]).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/profile/update
    svr.Post("/api/profile/update", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string hash = j.value("pub_key_hash", "");
            if (hash.empty()) { res.status = 400; res.set_content("{\"error\":\"hash required\"}", "application/json"); return; }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401;
                res.set_content("{\"error\":\"Cryptographic signature verification failed\"}", "application/json");
                return;
            }

            if (verified_identity_hash != hash) {
                res.status = 403;
                res.set_content("{\"error\":\"Identity mismatch\"}", "application/json");
                return;
            }

            std::string pqc_identity_public_key = j.value("pqc_identity_public_key", "");
            if (!pqc_identity_public_key.empty()) {
                std::string computed = sha256_hex(pqc_identity_public_key).substr(0, 16);
                if (computed != hash) {
                    res.status = 400;
                    res.set_content("{\"error\":\"Identity public key hash mismatch\"}", "application/json");
                    return;
                }
            }

            std::string username = j.value("username", "");
            std::string pqc_kem_public_key = j.value("pqc_kem_public_key", "");
            std::string identity_public_key = j.value("identity_public_key", "");
            std::string pqc_identity_scheme = j.value("pqc_identity_scheme", "");
            std::string identity_binding_payload = j.value("identity_binding_payload", "");
            std::string identity_binding_signature = j.value("identity_binding_signature", "");
            std::string recovery_lookup_hash = j.value("recovery_lookup_hash", "");
            std::string recovery_bundle_ciphertext = j.value("recovery_bundle_ciphertext", "");
            std::string recovery_bundle_iv = j.value("recovery_bundle_iv", "");

            db_manager.UpdateProfile(
                hash,
                username,
                pqc_kem_public_key,
                identity_public_key,
                pqc_identity_public_key,
                pqc_identity_scheme,
                identity_binding_payload,
                identity_binding_signature,
                recovery_lookup_hash,
                recovery_bundle_ciphertext,
                recovery_bundle_iv
            );
            res.set_content("{\"status\":\"ok\"}", "application/json");
        } catch (const std::exception& e) {
            const std::string msg = e.what();
            res.status = contains_ascii_case_insensitive(msg, "username")
                || contains_ascii_case_insensitive(msg, "recovery")
                ? 400
                : 500;
            res.set_content(json({{"error", msg}}).dump(), "application/json");
        }
    });

    // POST /api/identity/recover
    svr.Post("/api/identity/recover", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string recovery_lookup_hash = trim_copy(j.value("recovery_lookup_hash", ""));
            if (recovery_lookup_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"recovery_lookup_hash required\"}", "application/json");
                return;
            }

            json result = db_manager.GetRecoveryBundle(recovery_lookup_hash);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Recovery lookup failed");
                res.status = contains_ascii_case_insensitive(error, "not found") ? 404 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }

            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/admin/claim-founder
    svr.Post("/api/admin/claim-founder", [&](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        res.status = 410;
        res.set_content(
            json({
                {"error", "Founder bootstrap is no longer handled by a server phrase. Grant the founder hash with scripts/db-admin.sh, then restore the same seed phrase in the browser."}
            }).dump(),
            "application/json"
        );
    });

    // POST /api/admin/login
    svr.Post("/api/admin/login", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string founder_token = trim_copy(j.value("founder_token", ""));

            if (actor_hash.empty() || founder_token.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash and founder_token required\"}", "application/json");
                return;
            }

            std::string out_session_id;
            json result = db_manager.AdminLogin(actor_hash, founder_token, out_session_id);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Admin login failed");
                res.status = contains_ascii_case_insensitive(error, "unauthorized") ? 401 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }

            res.headers.insert(std::make_pair("Set-Cookie", "founder_session=" + out_session_id + "; HttpOnly; Secure; SameSite=Strict; Path=/"));
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/admin/roles
    svr.Post("/api/admin/roles", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string target_hash = trim_copy(j.value("target_hash", ""));
            const std::string role = trim_copy(j.value("role", ""));

            if (actor_hash.empty() || target_hash.empty() || role.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash, target_hash, and role required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.SetProfileRole(actor_hash, founder_session_cookie, target_hash, role);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Role update failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }

            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/admin/gift
    svr.Post("/api/admin/gift", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string target_hash = trim_copy(j.value("target_hash", ""));
            const std::string gift_type = trim_copy(j.value("gift_type", ""));
            const std::string gift_value = trim_copy(j.value("gift_value", ""));
            int duration_days = j.value("duration_days", 30);

            if (actor_hash.empty() || target_hash.empty() || gift_type.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash, target_hash, and gift_type required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.GiftUser(actor_hash, founder_session_cookie, target_hash, gift_type, gift_value, duration_days);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Gifting failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }

            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/admin/reports/:actor?limit=50
    svr.Get(R"(/api/admin/reports/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string actor_hash = trim_copy(req.matches[1]);
            int limit = 50;
            if (req.has_param("limit")) {
                limit = std::max(1, std::atoi(req.get_param_value("limit").c_str()));
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.GetModerationReports(actor_hash, founder_session_cookie, limit);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Moderation lookup failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/admin/audit/:actor?limit=50
    svr.Get(R"(/api/admin/audit/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string actor_hash = trim_copy(req.matches[1]);
            int limit = 50;
            if (req.has_param("limit")) {
                limit = std::max(1, std::atoi(req.get_param_value("limit").c_str()));
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.GetModerationAudit(actor_hash, founder_session_cookie, limit);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Moderation audit lookup failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/admin/reports/:id/resolve
    svr.Post(R"(/api/admin/reports/(\d+)/resolve)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const int64_t report_id = std::stoll(req.matches[1]);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string status = trim_copy(j.value("status", ""));
            const std::string note = trim_copy(j.value("note", ""));
            if (actor_hash.empty() || status.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash and status required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.ResolveModerationReport(actor_hash, founder_session_cookie, report_id, status, note);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Resolving report failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/admin/posts/:id/delete
    svr.Post(R"(/api/admin/posts/(\d+)/delete)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const int64_t post_id = std::stoll(req.matches[1]);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            if (actor_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.DeletePostAsModerator(actor_hash, founder_session_cookie, post_id);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Delete failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/admin/users/ban (legacy)
    svr.Post("/api/admin/users/ban", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string target_hash = trim_copy(j.value("target_hash", ""));
            const std::string reason = trim_copy(j.value("reason", ""));
            if (actor_hash.empty() || target_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash and target_hash required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.BanUserAsModerator(actor_hash, founder_session_cookie, target_hash, reason);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Ban failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/admin/users/unban (legacy)
    svr.Post("/api/admin/users/unban", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string target_hash = trim_copy(j.value("target_hash", ""));
            if (actor_hash.empty() || target_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash and target_hash required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.UnbanUserAsModerator(actor_hash, founder_session_cookie, target_hash);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Unban failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/admin/ban
    svr.Post("/api/admin/ban", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string target = trim_copy(j.value("target", ""));
            const std::string ban_type = trim_copy(j.value("ban_type", "")); // "identity" or "ip"
            const std::string reason = trim_copy(j.value("reason", ""));
            int64_t duration_seconds = j.value("duration_seconds", (int64_t)0);

            if (actor_hash.empty() || target.empty() || ban_type.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash, target, and ban_type are required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.BanUser(actor_hash, founder_session_cookie, target, ban_type, reason, duration_seconds);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Ban failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // DELETE /api/admin/ban/:id
    svr.Delete(R"(/api/admin/ban/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string ban_id = trim_copy(req.matches[1]);
            std::string actor_hash = req.has_param("actor_hash") ? trim_copy(req.get_param_value("actor_hash")) : "";

            if (actor_hash.empty()) {
                try {
                    auto j = json::parse(req.body);
                    actor_hash = trim_copy(j.value("actor_hash", ""));
                } catch (...) {}
            }

            if (actor_hash.empty() || ban_id.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash and ban id are required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.UnbanUser(actor_hash, founder_session_cookie, ban_id);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Unban failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/admin/bans
    svr.Get("/api/admin/bans", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string actor_hash = req.has_param("actor_hash") ? trim_copy(req.get_param_value("actor_hash")) : "";
            if (actor_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash is required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.GetBans(actor_hash, founder_session_cookie);
            if (result.is_object() && result.contains("error")) {
                const std::string error = result.value("error", "Failed to retrieve bans");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/admin/ban/:id/extend
    svr.Post(R"(/api/admin/ban/([^/]+)/extend)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string ban_id = trim_copy(req.matches[1]);
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            int64_t duration_seconds = j.value("duration_seconds", (int64_t)0);

            if (actor_hash.empty() || ban_id.empty() || duration_seconds <= 0) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash, ban id, and positive duration_seconds are required\"}", "application/json");
                return;
            }

            std::string cookie_header = req.has_header("Cookie") ? req.get_header_value("Cookie") : "";
            std::string founder_session_cookie = parse_cookie(cookie_header, "founder_session");

            json result = db_manager.ExtendBan(actor_hash, founder_session_cookie, ban_id, duration_seconds);
            if (result.contains("error")) {
                const std::string error = result.value("error", "Extension failed");
                res.status = contains_ascii_case_insensitive(error, "authorization") ? 403 : 400;
                res.set_content(result.dump(), "application/json");
                return;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/interact — requires Dilithium5 signature to prevent vote spoofing
    svr.Post("/api/interact", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            int64_t post_id = j.value("post_id", (int64_t)0);
            std::string hash = j.value("pub_key_hash", "");
            int type = j.value("type", 0);
            if (post_id == 0 || hash.empty() || (type != 1 && type != -1)) { res.status = 400; return; }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != hash) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            res.set_content(db_manager.InteractPost(post_id, hash, type).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/friends/request
    svr.Post("/api/friends/request", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string sender = j.value("sender_hash", "");
            std::string receiver = j.value("receiver_hash", "");
            if (sender.empty() || receiver.empty()) { res.status = 400; return; }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != sender) {
                res.status = 403; res.set_content("{\"error\":\"Sender hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.SendFriendRequest(sender, receiver);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/friends/accept
    svr.Post("/api/friends/accept", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string sender = j.value("sender_hash", "");
            std::string receiver = j.value("receiver_hash", "");
            if (sender.empty() || receiver.empty()) { res.status = 400; return; }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != receiver) {
                res.status = 403; res.set_content("{\"error\":\"Receiver hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.AcceptFriendRequest(sender, receiver);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/friends/reject
    svr.Post("/api/friends/reject", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string sender = j.value("sender_hash", "");
            std::string receiver = j.value("receiver_hash", "");
            if (sender.empty() || receiver.empty()) { res.status = 400; return; }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != receiver) {
                res.status = 403; res.set_content("{\"error\":\"Receiver hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.RejectFriendRequest(sender, receiver);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/friends/cancel
    svr.Post("/api/friends/cancel", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string sender = j.value("sender_hash", "");
            std::string receiver = j.value("receiver_hash", "");
            if (sender.empty() || receiver.empty()) { res.status = 400; return; }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != sender) {
                res.status = 403; res.set_content("{\"error\":\"Sender hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.CancelFriendRequest(sender, receiver);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/friends/remove
    svr.Post("/api/friends/remove", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string user = j.value("user_hash", "");
            std::string peer = j.value("peer_hash", "");
            if (user.empty() || peer.empty()) { res.status = 400; return; }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != user) {
                res.status = 403; res.set_content("{\"error\":\"User hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.RemoveFriend(user, peer);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/friends/block
    svr.Post("/api/friends/block", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string blocker = j.value("blocker_hash", "");
            std::string blocked = j.value("blocked_hash", "");
            if (blocker.empty() || blocked.empty()) { res.status = 400; return; }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != blocker) {
                res.status = 403; res.set_content("{\"error\":\"Blocker hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.BlockUser(blocker, blocked);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/friends/unblock
    svr.Post("/api/friends/unblock", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string blocker = j.value("blocker_hash", "");
            std::string blocked = j.value("blocked_hash", "");
            if (blocker.empty() || blocked.empty()) { res.status = 400; return; }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != blocker) {
                res.status = 403; res.set_content("{\"error\":\"Blocker hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.UnblockUser(blocker, blocked);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/friends/:hash
    svr.Get(R"(/api/friends/([a-zA-Z0-9]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string target_hash = req.matches[1];
            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, "", db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != target_hash) {
                res.status = 403; res.set_content("{\"error\":\"Unauthorized to view this friends list\"}", "application/json"); return;
            }

            res.set_content(db_manager.GetFriends(target_hash).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/messages
    svr.Post("/api/messages", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string client_ip = get_client_ip(req);
            std::string ip_ban_reason;
            if (db_manager.IsIpBanned(client_ip, &ip_ban_reason)) {
                res.status = 403;
                res.set_content(json({{"error", ip_ban_reason.empty() ? "Your IP address is banned" : ip_ban_reason}}).dump(), "application/json");
                return;
            }

            auto j = json::parse(req.body);
            std::string sender = j.value("sender_hash", "");
            std::string receiver = j.value("receiver_hash", "");
            std::string content = j.value("content", "");
            std::string image_url = j.value("image_url", "");

            if (sender.empty() || receiver.empty() || (content.empty() && image_url.empty())) {
                res.status = 400;
                res.set_content("{\"error\":\"sender_hash, receiver_hash, and message body required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != sender) {
                res.status = 403; res.set_content("{\"error\":\"Sender hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.CreateDirectMessage(sender, receiver, content, image_url);
            if (result.contains("error")) {
                res.status = 400;
            } else {
                res.status = 201;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/messages
    svr.Get("/api/messages", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string user = req.has_param("user_hash") ? req.get_param_value("user_hash") : "";
            std::string peer = req.has_param("peer_hash") ? req.get_param_value("peer_hash") : "";
            if (user.empty() || peer.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"user_hash and peer_hash required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, "", db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != user) {
                res.status = 403; res.set_content("{\"error\":\"User hash mismatch\"}", "application/json"); return;
            }

            res.set_content(db_manager.GetDirectMessages(user, peer).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/messages/snapshot
    svr.Get("/api/messages/snapshot", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string user = req.has_param("user_hash") ? req.get_param_value("user_hash") : "";
            std::string peer = req.has_param("peer_hash") ? req.get_param_value("peer_hash") : "";
            if (user.empty() || peer.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"user_hash and peer_hash required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, "", db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != user) {
                res.status = 403; res.set_content("{\"error\":\"User hash mismatch\"}", "application/json"); return;
            }

            json conversation = db_manager.GetDirectMessages(user, peer);
            std::string payload = conversation.dump();
            json response;
            std::string error;
            if (!sign_snapshot_payload(payload, d_pub, d_sec, "conversation_block", response, error)) {
                res.status = 500;
                res.set_content(json({{"error", error}}).dump(), "application/json");
                return;
            }

            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/messages/inbox/:hash
    svr.Get(R"(/api/messages/inbox/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string target_hash = req.matches[1];
            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, "", db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != target_hash) {
                res.status = 403; res.set_content("{\"error\":\"Unauthorized to view this inbox\"}", "application/json"); return;
            }

            res.set_content(db_manager.GetDirectMessageInbox(target_hash).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/messages/requests/respond
    svr.Post("/api/messages/requests/respond", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string actor = j.value("actor_hash", "");
            std::string requester = j.value("requester_hash", "");
            std::string action = j.value("action", "");
            if (actor.empty() || requester.empty() || action.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash, requester_hash, and action required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != actor) {
                res.status = 403; res.set_content("{\"error\":\"Actor hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.RespondToMessageRequest(actor, requester, action);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/notifications/summary/:hash
    svr.Get(R"(/api/notifications/summary/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string target_hash = req.matches[1];
            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, "", db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != target_hash) {
                res.status = 403; res.set_content("{\"error\":\"Unauthorized to view notifications summary\"}", "application/json"); return;
            }

            res.set_content(db_manager.GetNotificationSummary(target_hash).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/notifications/:hash
    svr.Get(R"(/api/notifications/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string target_hash = req.matches[1];
            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, "", db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != target_hash) {
                res.status = 403; res.set_content("{\"error\":\"Unauthorized to view notifications\"}", "application/json"); return;
            }

            int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 50;
            res.set_content(db_manager.GetNotifications(target_hash, limit).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/notifications/read
    svr.Post("/api/notifications/read", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string user = j.value("user_hash", "");
            std::string notification_id = j.value("notification_id", "");
            if (user.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"user_hash required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != user) {
                res.status = 403; res.set_content("{\"error\":\"User hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.MarkNotificationsRead(user, notification_id);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/live/notifications/summary/:hash
    svr.Get(R"(/api/live/notifications/summary/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string user_hash = req.matches[1];
            respond_with_long_poll_sse(
                req,
                res,
                "update",
                [&]() {
                    return db_manager.GetNotificationSummary(user_hash);
                },
                [](const json& state, const std::string&, bool changed) {
                    json payload = state;
                    payload["changed"] = changed;
                    return payload;
                }
            );
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/live/notifications/feed/:hash
    svr.Get(R"(/api/live/notifications/feed/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string user_hash = req.matches[1];
            const int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 100;
            respond_with_long_poll_sse(
                req,
                res,
                "update",
                [&]() {
                    return db_manager.GetNotifications(user_hash, limit);
                },
                [](const json& state, const std::string&, bool changed) {
                    json payload = state;
                    payload["changed"] = changed;
                    return payload;
                }
            );
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/live/messages/inbox/:hash
    svr.Get(R"(/api/live/messages/inbox/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string user_hash = req.matches[1];
            respond_with_long_poll_sse(
                req,
                res,
                "update",
                [&]() {
                    return db_manager.GetDirectMessageInbox(user_hash);
                },
                [](const json& state, const std::string&, bool changed) {
                    json payload = state;
                    payload["changed"] = changed;
                    return payload;
                }
            );
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/live/messages/conversation?user_hash=...&peer_hash=...
    svr.Get("/api/live/messages/conversation", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string user = req.has_param("user_hash") ? req.get_param_value("user_hash") : "";
            const std::string peer = req.has_param("peer_hash") ? req.get_param_value("peer_hash") : "";
            if (user.empty() || peer.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"user_hash and peer_hash required\"}", "application/json");
                return;
            }

            respond_with_long_poll_sse(
                req,
                res,
                "update",
                [&]() {
                    return db_manager.GetDirectMessages(user, peer);
                },
                [&](const json& state, const std::string&, bool changed) {
                    json payload = state;
                    payload["changed"] = changed;
                    payload["peerHash"] = state.value("peerHash", peer);
                    payload["channelStatus"] = state.value("channelStatus", "unknown");
                    payload["messageCount"] = state.contains("messages") && state["messages"].is_array() ? state["messages"].size() : 0;
                    return payload;
                }
            );
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/reports
    svr.Post("/api/reports", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string reporter = j.value("reporter_hash", "");
            std::string target = j.value("target_hash", "");
            std::string reason = j.value("reason", "");
            std::string target_kind = j.value("target_kind", "user");
            int64_t target_post_id = j.value("target_post_id", static_cast<int64_t>(0));
            int64_t target_thread_id = j.value("target_thread_id", static_cast<int64_t>(0));
            std::string target_board_id = j.value("target_board_id", "");
            std::string target_display_name = j.value("target_display_name", "");
            std::string context_link = j.value("context_link", "");
            if (reporter.empty() || target.empty() || reason.empty()) {
                if (!(target_kind == "post" && !reporter.empty() && target_post_id > 0 && !reason.empty())) {
                    res.status = 400;
                    res.set_content("{\"error\":\"reporter_hash, target_hash, and reason required\"}", "application/json");
                    return;
                }
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != reporter) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.CreateReport(reporter, target, reason, target_kind, target_post_id, target_thread_id, target_board_id, target_display_name, context_link);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // -------------------------------------------------------------------------
    // NOWPayments & Subscriptions (Task 6)
    // -------------------------------------------------------------------------
    // NOWPayments & Subscriptions (Task 6 & Task 7 Customizations)
    // -------------------------------------------------------------------------

    // POST /api/payments/create
    svr.Post("/api/payments/create", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            std::string tier = trim_copy(j.value("tier", ""));
            std::string pay_currency = trim_copy(j.value("pay_currency", ""));

            // Verify actor identity via Dilithium5 signature to prevent invoice spoofing
            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != actor_hash) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            // Validate pay_currency is strictly btc, ltc, or xmr
            std::string pc_lower = pay_currency;
            std::transform(pc_lower.begin(), pc_lower.end(), pc_lower.begin(), ::tolower);
            if (pc_lower != "btc" && pc_lower != "ltc" && pc_lower != "xmr") {
                res.status = 400;
                res.set_content("{\"error\":\"pay_currency must be btc, ltc, or xmr\"}", "application/json");
                return;
            }

            bool is_sub_tier = (tier == "circle" || tier == "hermes");
            bool is_valid_tag = (tier == "daddy" || tier == "OG" || tier == "LGBT" ||
                                 tier == "VIP" || tier == "CHAD" || tier == "DONOR" ||
                                 tier == "PREMIUM" || tier == "WAIFU" || tier == "SIMP" ||
                                 tier == "ELITE" || tier == "BOOSTER" || tier == "queen");
            bool is_sticky_tier = (tier == "sticky" || tier == "bump");
            bool is_custom_tag = (tier == "custom_tag");

            if (!is_sub_tier && !is_valid_tag && !is_sticky_tier && !is_custom_tag) {
                res.status = 400;
                res.set_content("{\"error\":\"tier must be circle, hermes, sticky, custom_tag, or a valid predefined tag\"}", "application/json");
                return;
            }

            double price = 50.0; // default for predefined tags
            if (tier == "circle") price = 50.0;
            else if (tier == "hermes") price = 50.0;
            else if (is_sticky_tier) price = 5.0;
            else if (is_custom_tag) {
                std::string custom_tag_text = trim_copy(j.value("custom_tag_text", ""));
                if (custom_tag_text.empty() || custom_tag_text.length() > 6) {
                    res.status = 400;
                    res.set_content("{\"error\":\"custom_tag_text must be between 1 and 6 characters\"}", "application/json");
                    return;
                }
                for (char c : custom_tag_text) {
                    if (!std::isalnum(c) && c != ' ' && c != '_' && c != '-') {
                        res.status = 400;
                        res.set_content("{\"error\":\"custom_tag_text contains invalid characters\"}", "application/json");
                        return;
                    }
                }
                size_t len = custom_tag_text.length();
                if (len == 1) price = 50.0;
                else if (len == 2) price = 40.0;
                else if (len == 3) price = 30.0;
                else if (len == 4) price = 20.0;
                else if (len == 5) price = 10.0;
                else price = 2.0; // len == 6
            }

            std::string order_id = actor_hash;
            std::string order_description = "";

            if (is_valid_tag) {
                order_id += "_tag_" + tier;
                order_description = "QuanChan Predefined Tag - " + tier;
            } else if (is_custom_tag) {
                std::string custom_tag_text = trim_copy(j.value("custom_tag_text", ""));
                order_id += "_customtag_" + custom_tag_text;
                order_description = "QuanChan Custom Tag - " + custom_tag_text;
            } else if (is_sticky_tier) {
                std::string thread_id = trim_copy(j.value("thread_id", ""));
                order_id = "sticky_thread_" + thread_id;
                order_description = "QuanChan Thread Bump - Thread No." + thread_id;
            } else {
                order_id += "_" + tier;
                order_description = "QuanChan Subscription - " + tier + " tier";
            }

            const char* api_key_env = std::getenv("QC_NOWPAYMENTS_API_KEY");
            std::string api_key = api_key_env ? api_key_env : "";

            if (api_key.empty() || api_key == "sandbox") {
                res.status = 500;
                res.set_content("{\"error\":\"NOWPayments API key is not configured on the server.\"}", "application/json");
                return;
            }

            json payload = {
                {"price_amount", price},
                {"price_currency", "usd"},
                {"pay_currency", pay_currency},
                {"ipn_callback_url", "https://quanchan.online/api/payments/webhook"},
                {"order_id", order_id},
                {"order_description", order_description}
            };

            std::string np_response = http_post_json("https://api.nowpayments.io/v1/payment", payload.dump(), api_key);
            if (np_response.empty()) {
                res.status = 500;
                res.set_content("{\"error\":\"Failed to create payment on NOWPayments API\"}", "application/json");
                return;
            }

            res.set_content(np_response, "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/payments/webhook
    svr.Post("/api/payments/webhook", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string signature = req.has_header("x-nowpayments-sig") ? req.get_header_value("x-nowpayments-sig") : "";
            if (signature.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"Missing x-nowpayments-sig header\"}", "application/json");
                return;
            }

            const char* secret_env = std::getenv("QC_NOWPAYMENTS_IPN_SECRET");
            std::string secret = secret_env ? secret_env : "";

            if (secret.empty()) {
                res.status = 500;
                res.set_content("{\"error\":\"Webhook IPN secret is not configured on the server.\"}", "application/json");
                return;
            }

            std::string computed = hmac_sha512(secret, req.body);
            if (computed != signature) {
                res.status = 403;
                res.set_content("{\"error\":\"Invalid IPN signature\"}", "application/json");
                return;
            }

            auto body_json = json::parse(req.body);
            std::string status = body_json.value("payment_status", "");
            std::string order_id = body_json.value("order_id", "");

            if (status == "finished" || status == "confirmed") {
                size_t tag_pos = order_id.find("_tag_");
                size_t customtag_pos = order_id.find("_customtag_");
                if (tag_pos != std::string::npos) {
                    std::string user_hash = order_id.substr(0, tag_pos);
                    std::string tag_name = order_id.substr(tag_pos + 5);

                    db_manager.AddProfileTag(user_hash, tag_name);
                    Logger::Info("Added paid tag via webhook. User: " + user_hash + ", Tag: " + tag_name);
                } else if (customtag_pos != std::string::npos) {
                    std::string user_hash = order_id.substr(0, customtag_pos);
                    std::string tag_name = order_id.substr(customtag_pos + 11);

                    db_manager.AddProfileTag(user_hash, tag_name);
                    Logger::Info("Added paid custom tag via webhook. User: " + user_hash + ", Tag: " + tag_name);
                } else if (order_id.rfind("sticky_thread_", 0) == 0) {
                    std::string thread_id_str = order_id.substr(14);
                    try {
                        int64_t thread_id = std::stoll(thread_id_str);
                        db_manager.StickyThread(thread_id);
                        Logger::Info("Thread " + thread_id_str + " marked as sticky/bumped via webhook.");
                    } catch (const std::exception& e) {
                        Logger::Error("Failed to parse thread_id from order_id: " + order_id + " - " + e.what());
                    }
                } else {
                    size_t underscore = order_id.find('_');
                    if (underscore != std::string::npos) {
                        std::string user_hash = order_id.substr(0, underscore);
                        std::string tier = order_id.substr(underscore + 1);

                        json upgrade_res = db_manager.UpdateProfileSubscription(user_hash, tier, 30);
                        Logger::Info("Upgraded user subscription via webhook. User: " + user_hash + ", Tier: " + tier);
                    }
                }
            }

            res.set_content("{\"status\":\"ok\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });



    // POST /api/profile/select_tag
    svr.Post("/api/profile/select_tag", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string hash = trim_copy(j.value("pub_key_hash", ""));
            std::string tag = trim_copy(j.value("tag", ""));

            if (hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"pub_key_hash is required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != hash) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            bool ok = db_manager.SetProfileActiveTag(hash, tag);
            if (ok) {
                res.set_content("{\"status\":\"ok\",\"custom_badge\":\"" + tag + "\"}", "application/json");
            } else {
                res.status = 400;
                res.set_content("{\"error\":\"Failed to set active tag or tag not unlocked\"}", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // -------------------------------------------------------------------------
    // Cryptographic Group Rooms (Task 7)
    // -------------------------------------------------------------------------

    // POST /api/groups/create
    svr.Post("/api/groups/create", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string name = trim_copy(j.value("name", ""));
            std::string creator_hash = trim_copy(j.value("creator_hash", ""));
            std::string encrypted_key = trim_copy(j.value("encrypted_key", ""));

            if (name.empty() || creator_hash.empty() || encrypted_key.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"name, creator_hash, and encrypted_key are required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != creator_hash) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.CreateGroup(name, creator_hash, encrypted_key);
            if (result.contains("error")) {
                res.status = 403;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/groups/join
    svr.Post("/api/groups/join", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string group_id = trim_copy(j.value("group_id", ""));
            std::string user_hash = trim_copy(j.value("user_hash", ""));
            std::string encrypted_key = trim_copy(j.value("encrypted_key", ""));

            if (group_id.empty() || user_hash.empty() || encrypted_key.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"group_id, user_hash, and encrypted_key are required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != user_hash) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.JoinGroup(group_id, user_hash, encrypted_key);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/groups
    svr.Get("/api/groups", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string actor_hash = req.has_param("actor_hash") ? trim_copy(req.get_param_value("actor_hash")) : "";
            if (actor_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash parameter required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, "", db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != actor_hash) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.GetUserGroups(actor_hash);
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/groups/:id/messages
    svr.Get(R"(/api/groups/([^/]+)/messages)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string group_id = trim_copy(req.matches[1]);
            std::string actor_hash = req.has_param("actor_hash") ? trim_copy(req.get_param_value("actor_hash")) : "";
            if (actor_hash.empty() || group_id.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"group_id and actor_hash required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, "", db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != actor_hash) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.GetGroupMessages(group_id, actor_hash);
            if (result.contains("error")) {
                res.status = 403;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/groups/:id/messages
    svr.Post(R"(/api/groups/([^/]+)/messages)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string group_id = trim_copy(req.matches[1]);
            auto j = json::parse(req.body);
            std::string sender_hash = trim_copy(j.value("sender_hash", ""));
            std::string encrypted_content = trim_copy(j.value("encrypted_content", ""));

            if (group_id.empty() || sender_hash.empty() || encrypted_content.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"group_id, sender_hash, and encrypted_content required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != sender_hash) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.SendGroupMessage(group_id, sender_hash, encrypted_content);
            if (result.contains("error")) {
                res.status = 403;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/groups/:id/rotate
    svr.Post(R"(/api/groups/([^/]+)/rotate)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string group_id = trim_copy(req.matches[1]);
            auto j = json::parse(req.body);
            std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            auto new_keys = j.value("new_keys", json::array());

            if (group_id.empty() || actor_hash.empty() || new_keys.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"group_id, actor_hash, and new_keys required\"}", "application/json");
                return;
            }

            std::string verified_identity_hash;
            if (!VerifyRequestSignature(req, req.body, db_manager, verified_identity_hash)) {
                res.status = 401; res.set_content("{\"error\":\"Signature verification failed\"}", "application/json"); return;
            }
            if (verified_identity_hash != actor_hash) {
                res.status = 403; res.set_content("{\"error\":\"Identity hash mismatch\"}", "application/json"); return;
            }

            json result = db_manager.RotateGroupKeys(group_id, actor_hash, new_keys);
            if (result.contains("error")) {
                res.status = 403;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // -------------------------------------------------------------------------
    // Hermes PQC API Tier (Task 8)
    // -------------------------------------------------------------------------

    // GET /api/hermes/pubkey
    svr.Get("/api/hermes/pubkey", [&](const httplib::Request&, httplib::Response& res) {
        try {
            std::string pubkey_pem = get_hermes_mlkem_pubkey_pem(hermes_pkey);
            if (pubkey_pem.empty()) {
                res.status = 500;
                res.set_content("{\"error\":\"Failed to retrieve server ML-KEM key\"}", "application/json");
                return;
            }
            res.set_content(json({{"pubkey", pubkey_pem}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/hermes/v1/chat/completions
    svr.Post("/api/hermes/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string auth_header = req.has_header("Authorization") ? req.get_header_value("Authorization") : "";
            if (auth_header.find("Bearer ") != 0) {
                res.status = 401;
                res.set_content("{\"error\":\"Missing or invalid Authorization header\"}", "application/json");
                return;
            }
            std::string api_key = auth_header.substr(7);

            std::string user_hash;
            std::string key_tier;
            bool key_valid = db_manager.ValidateApiKey(api_key, user_hash, key_tier);
            if (!key_valid || key_tier != "hermes") {
                res.status = 403;
                res.set_content("{\"error\":\"Valid Hermes subscription required\"}", "application/json");
                return;
            }

            int limit = 10;
            int remaining = limit;
            int reset_secs = 60;
            {
                std::lock_guard<std::mutex> rl_lock(rate_limit_mutex);
                auto now = std::chrono::steady_clock::now();
                auto& entry = hermes_rate_limits[api_key];
                if (entry.count == 0 || std::chrono::duration_cast<std::chrono::seconds>(now - entry.window_start).count() >= 60) {
                    entry.count = 1;
                    entry.window_start = now;
                } else {
                    entry.count++;
                }
                remaining = std::max(0, limit - entry.count);
                reset_secs = 60 - std::chrono::duration_cast<std::chrono::seconds>(now - entry.window_start).count();

                res.set_header("X-RateLimit-Limit", std::to_string(limit));
                res.set_header("X-RateLimit-Remaining", std::to_string(remaining));
                res.set_header("X-RateLimit-Reset", std::to_string(reset_secs));

                if (entry.count > limit) {
                    res.status = 429;
                    res.set_content("{\"error\":\"Too many requests\"}", "application/json");
                    return;
                }
            }

            auto body_json = json::parse(req.body);
            std::string ciphertext_hex = body_json.value("ciphertext", "");
            std::string encrypted_payload_hex = body_json.value("payload", "");
            std::string iv_hex = body_json.value("iv", "");

            if (ciphertext_hex.empty() || encrypted_payload_hex.empty() || iv_hex.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"ciphertext, payload, and iv hex fields are required\"}", "application/json");
                return;
            }

            std::string ciphertext = hex_decode(ciphertext_hex);
            std::string encrypted_payload = hex_decode(encrypted_payload_hex);
            std::string iv = hex_decode(iv_hex);

            std::string shared_secret = decapsulate_shared_secret(hermes_pkey, ciphertext);
            if (shared_secret.empty() || shared_secret.size() < 32) {
                res.status = 400;
                res.set_content("{\"error\":\"ML-KEM decapsulation failed\"}", "application/json");
                return;
            }

            std::string aes_key = shared_secret.substr(0, 32);

            std::string decrypted_request = aes_256_gcm_decrypt(encrypted_payload, aes_key, iv);
            if (decrypted_request.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"Payload decryption failed (AES-256-GCM integrity check failed)\"}", "application/json");
                return;
            }

            auto client_req = json::parse(decrypted_request);
            std::string user_msg = "hello";
            try {
                if (client_req.contains("messages") && client_req["messages"].is_array() && !client_req["messages"].empty()) {
                    user_msg = client_req["messages"].back().value("content", "hello");
                }
            } catch (...) {}

            json completion_res = {
                {"id", "chatcmpl-" + random_hex(16)},
                {"object", "chat.completion"},
                {"created", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
                {"model", "hermes-3"},
                {"choices", json::array({
                    {
                        {"index", 0},
                        {"message", {
                            {"role", "assistant"},
                            {"content", "I am Hermes-3, your post-quantum secure AI assistant. You said: \"" + user_msg + "\""}
                        }},
                        {"finish_reason", "stop"}
                    }
                })},
                {"usage", {
                    {"prompt_tokens", (int)(user_msg.size() / 4 + 1)},
                    {"completion_tokens", 25},
                    {"total_tokens", (int)(user_msg.size() / 4 + 26)}
                }}
            };

            std::string response_str = completion_res.dump();
            std::string enc_tag;
            std::string new_iv = random_hex(12);
            std::string new_iv_bin = hex_decode(new_iv);

            std::string encrypted_response = aes_256_gcm_encrypt(response_str, aes_key, new_iv_bin, enc_tag);
            if (encrypted_response.empty()) {
                res.status = 500;
                res.set_content("{\"error\":\"Failed to encrypt response payload\"}", "application/json");
                return;
            }

            std::string final_payload_bin = encrypted_response + enc_tag;

            json wrapped_res = {
                {"payload", hex_encode(final_payload_bin)},
                {"iv", new_iv}
            };
            res.set_content(wrapped_res.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/stats
    svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
        try {
            res.set_content(db_manager.GetStats().dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/upload (multipart) â€” with EXIF stripping for privacy
    svr.Post("/api/upload", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            if (!req.has_file("file")) { res.status = 400; res.set_content("{\"error\":\"No file\"}", "application/json"); return; }
            const auto& file = req.get_file_value("file");
            std::string file_data = file.content;
            std::string ext = normalized_upload_extension(file.filename, file.content_type, file_data);
            if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".gif" &&
                ext != ".webp" &&
                ext != ".mp3" && ext != ".ogg" && ext != ".wav") {
                res.status = 400;
                res.set_content("{\"error\":\"Disallowed file extension\"}", "application/json");
                return;
            }
            std::string new_name = random_hex(16) + ext;
            std::string path = "/app/uploads/" + new_name;

            // EXIF stripping for JPEG files (privacy: remove GPS, camera model, etc.)
            if (ext == ".jpg" || ext == ".jpeg") {
                // Scan for and remove all APP1 (EXIF) markers: 0xFF 0xE1
                std::string stripped;
                stripped.reserve(file_data.size());
                size_t i = 0;
                while (i < file_data.size()) {
                    if (i + 1 < file_data.size() &&
                        static_cast<uint8_t>(file_data[i]) == 0xFF &&
                        static_cast<uint8_t>(file_data[i + 1]) == 0xE1) {
                        // Read segment length (big-endian 2 bytes after marker)
                        if (i + 3 < file_data.size()) {
                            uint16_t seg_len = (static_cast<uint8_t>(file_data[i + 2]) << 8) |
                                               static_cast<uint8_t>(file_data[i + 3]);
                            i += 2 + seg_len; // skip marker (2) + segment (seg_len includes length bytes)
                        } else {
                            break;
                        }
                    } else {
                        stripped.push_back(file_data[i]);
                        ++i;
                    }
                }
                file_data = std::move(stripped);
                Logger::Info("EXIF stripped from upload: " + file.filename);
            }

            std::ofstream ofs(path, std::ios::binary);
            if (!ofs) { res.status = 500; res.set_content("{\"error\":\"Save failed\"}", "application/json"); return; }
            ofs.write(file_data.data(), file_data.size());
            ofs.close();
            res.set_content(json({{"url", "/uploads/" + new_name}, {"filename", new_name},
                {"originalName", file.filename}, {"size", file_data.size()}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // Serve index.html for SPA routes like /crypto or /dm when directly opened.
    svr.Get(R"(/(.*))", [](const httplib::Request& req, httplib::Response& res) {
        if (req.path == "/health" || req.path.rfind("/api/", 0) == 0 || req.path.rfind("/uploads/", 0) == 0) {
            res.status = 404;
            return;
        }

        std::ifstream index_file("/app/www/index.html", std::ios::binary);
        if (!index_file) {
            res.status = 404;
            return;
        }

        std::stringstream buffer;
        buffer << index_file.rdbuf();
        res.set_content(buffer.str(), "text/html");
    });

    Logger::Info("HTTPS Gateway (TLS 1.3 + PQC certificate policy) listening on port " + std::to_string(port));
    svr.listen("0.0.0.0", port);
}
