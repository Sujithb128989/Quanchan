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

// OpenSSL base64
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

using json = nlohmann::json;

static OQS_SIG* GetThreadLocalSig();

// =============================================================================
// Helpers
// =============================================================================

static std::string random_hex(int len) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    std::stringstream ss;
    for (int i = 0; i < len; ++i) ss << std::hex << dist(rng);
    return ss.str();
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

// =============================================================================
// HTTP Server
// =============================================================================

void RunHTTPServer(DBManager& db_manager, int port,
                   const std::string& cert_dir,
                   const std::vector<uint8_t>& d_pub,
                   const std::vector<uint8_t>& d_sec) {

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

    // ─── CORS & Auth Middleware ──────────────────────────────────────────
    svr.set_pre_routing_handler([&cert_info, &tls_state](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
        res.set_header("Access-Control-Expose-Headers", "X-PQC-Cipher, X-PQC-Proof-Endpoint");
        res.set_header("X-PQC-Cipher", transport_disclosure_label(cert_info, tls_state));
        res.set_header("X-PQC-Proof-Endpoint", "/api/crypto/tls-proof");

        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
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

    // ─── Original PQC Endpoints ─────────────────────────────────────────

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

    // ─── Imageboard Endpoints ───────────────────────────────────────────

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

    // ─── GET /api/threads/:id — DILITHIUM5 BATCH SIGNED ─────────────────
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

    // PATCH /api/threads/:id/archive
    svr.Patch(R"(/api/threads/(\d+)/archive)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
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
    // ─── V4 Social Endpoints ──────────────────────────────────────────────

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
            std::string username = j.value("username", "");
            std::string pqc_kem_public_key = j.value("pqc_kem_public_key", "");
            std::string identity_public_key = j.value("identity_public_key", "");
            std::string pqc_identity_public_key = j.value("pqc_identity_public_key", "");
            std::string pqc_identity_scheme = j.value("pqc_identity_scheme", "");
            std::string identity_binding_payload = j.value("identity_binding_payload", "");
            std::string identity_binding_signature = j.value("identity_binding_signature", "");
            std::string recovery_lookup_hash = j.value("recovery_lookup_hash", "");
            std::string recovery_bundle_ciphertext = j.value("recovery_bundle_ciphertext", "");
            std::string recovery_bundle_iv = j.value("recovery_bundle_iv", "");
            if (hash.empty()) { res.status = 400; res.set_content("{\"error\":\"hash required\"}", "application/json"); return; }
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

    // POST /api/admin/roles
    svr.Post("/api/admin/roles", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string founder_token = trim_copy(j.value("founder_token", ""));
            const std::string target_hash = trim_copy(j.value("target_hash", ""));
            const std::string role = trim_copy(j.value("role", ""));

            if (actor_hash.empty() || founder_token.empty() || target_hash.empty() || role.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash, founder_token, target_hash, and role required\"}", "application/json");
                return;
            }

            json result = db_manager.SetProfileRole(actor_hash, sha256_hex(founder_token), target_hash, role);
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

    // GET /api/admin/reports/:actor?limit=50&founder_token=...
    svr.Get(R"(/api/admin/reports/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string actor_hash = trim_copy(req.matches[1]);
            const std::string founder_token = req.has_param("founder_token") ? trim_copy(req.get_param_value("founder_token")) : "";
            int limit = 50;
            if (req.has_param("limit")) {
                limit = std::max(1, std::atoi(req.get_param_value("limit").c_str()));
            }

            json result = db_manager.GetModerationReports(actor_hash, founder_token.empty() ? "" : sha256_hex(founder_token), limit);
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

    // GET /api/admin/audit/:actor?limit=50&founder_token=...
    svr.Get(R"(/api/admin/audit/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const std::string actor_hash = trim_copy(req.matches[1]);
            const std::string founder_token = req.has_param("founder_token") ? trim_copy(req.get_param_value("founder_token")) : "";
            int limit = 50;
            if (req.has_param("limit")) {
                limit = std::max(1, std::atoi(req.get_param_value("limit").c_str()));
            }

            json result = db_manager.GetModerationAudit(actor_hash, founder_token.empty() ? "" : sha256_hex(founder_token), limit);
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
            const std::string founder_token = trim_copy(j.value("founder_token", ""));
            const std::string status = trim_copy(j.value("status", ""));
            const std::string note = trim_copy(j.value("note", ""));
            if (actor_hash.empty() || status.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash and status required\"}", "application/json");
                return;
            }

            json result = db_manager.ResolveModerationReport(actor_hash, founder_token.empty() ? "" : sha256_hex(founder_token), report_id, status, note);
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
            const std::string founder_token = trim_copy(j.value("founder_token", ""));
            if (actor_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash required\"}", "application/json");
                return;
            }

            json result = db_manager.DeletePostAsModerator(actor_hash, founder_token.empty() ? "" : sha256_hex(founder_token), post_id);
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

    // POST /api/admin/users/ban
    svr.Post("/api/admin/users/ban", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string founder_token = trim_copy(j.value("founder_token", ""));
            const std::string target_hash = trim_copy(j.value("target_hash", ""));
            const std::string reason = trim_copy(j.value("reason", ""));
            if (actor_hash.empty() || target_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash and target_hash required\"}", "application/json");
                return;
            }

            json result = db_manager.BanUserAsModerator(actor_hash, founder_token.empty() ? "" : sha256_hex(founder_token), target_hash, reason);
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

    // POST /api/admin/users/unban
    svr.Post("/api/admin/users/unban", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            const std::string actor_hash = trim_copy(j.value("actor_hash", ""));
            const std::string founder_token = trim_copy(j.value("founder_token", ""));
            const std::string target_hash = trim_copy(j.value("target_hash", ""));
            if (actor_hash.empty() || target_hash.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"actor_hash and target_hash required\"}", "application/json");
                return;
            }

            json result = db_manager.UnbanUserAsModerator(actor_hash, founder_token.empty() ? "" : sha256_hex(founder_token), target_hash);
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

    // POST /api/interact
    svr.Post("/api/interact", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            int64_t post_id = j.value("post_id", (int64_t)0);
            std::string hash = j.value("pub_key_hash", "");
            int type = j.value("type", 0);
            if (post_id == 0 || hash.empty() || (type != 1 && type != -1)) { res.status = 400; return; }
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
            res.set_content(db_manager.GetFriends(req.matches[1]).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/messages
    svr.Post("/api/messages", [&](const httplib::Request& req, httplib::Response& res) {
        try {
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

    // GET /api/messages?user_hash=...&peer_hash=...
    svr.Get("/api/messages", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string user = req.has_param("user_hash") ? req.get_param_value("user_hash") : "";
            std::string peer = req.has_param("peer_hash") ? req.get_param_value("peer_hash") : "";
            if (user.empty() || peer.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"user_hash and peer_hash required\"}", "application/json");
                return;
            }
            res.set_content(db_manager.GetDirectMessages(user, peer).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/messages/snapshot?user_hash=...&peer_hash=...
    svr.Get("/api/messages/snapshot", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string user = req.has_param("user_hash") ? req.get_param_value("user_hash") : "";
            std::string peer = req.has_param("peer_hash") ? req.get_param_value("peer_hash") : "";
            if (user.empty() || peer.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"user_hash and peer_hash required\"}", "application/json");
                return;
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
            res.set_content(db_manager.GetDirectMessageInbox(req.matches[1]).dump(), "application/json");
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
            res.set_content(db_manager.GetNotificationSummary(req.matches[1]).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // GET /api/notifications/:hash
    svr.Get(R"(/api/notifications/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 50;
            res.set_content(db_manager.GetNotifications(req.matches[1], limit).dump(), "application/json");
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
            json result = db_manager.CreateReport(reporter, target, reason, target_kind, target_post_id, target_thread_id, target_board_id, target_display_name, context_link);
            if (result.contains("error")) {
                res.status = 400;
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json({{"error", e.what()}}).dump(), "application/json");
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

    // POST /api/upload (multipart) — with EXIF stripping for privacy
    svr.Post("/api/upload", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            if (!req.has_file("file")) { res.status = 400; res.set_content("{\"error\":\"No file\"}", "application/json"); return; }
            const auto& file = req.get_file_value("file");
            std::string file_data = file.content;
            std::string ext = normalized_upload_extension(file.filename, file.content_type, file_data);
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
