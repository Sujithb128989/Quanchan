#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/provider.h>
#include <openssl/evp.h>
#include <oqs/oqs.h>
#include <filesystem>
#include <unistd.h>
#include <linux/limits.h>
#include <cstring>
#include <cstdlib>

#include "pqc.grpc.pb.h"
#include "health.grpc.pb.h"
#include "secure_storage.hpp"
#include "db_manager.hpp"
#include "http_gateway.hpp"
#include "s3_client.hpp"
#include "logger.hpp"
#include "config.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using pqc::PQCStringStore;
using pqc::StoreRequest;
using pqc::StoreResponse;
using pqc::RetrieveRequest;
using pqc::RetrieveResponse;
using pqc::RotateKeyRequest;
using pqc::RotateKeyResponse;
using pqc::UploadFileRequest;
using pqc::UploadFileResponse;
using pqc::DownloadFileRequest;
using pqc::DownloadFileResponse;
using grpc::health::v1::Health;
using grpc::health::v1::HealthCheckRequest;
using grpc::health::v1::HealthCheckResponse;

// Encrypted storage & S3
std::unique_ptr<SecureStorage> storage;
std::unique_ptr<DBManager> db_manager;
std::unique_ptr<S3Client> s3_client;

// Global Dilithium5 keys (loaded or generated on startup)
std::vector<uint8_t> dilithium_pub_key;
std::vector<uint8_t> dilithium_sec_key;

static bool HasOpenSslKeyAlgorithm(const char* algorithm_name) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, algorithm_name, nullptr);
    if (!ctx) return false;
    EVP_PKEY_CTX_free(ctx);
    return true;
}

static void RequireOpenSslKeyAlgorithm(const char* algorithm_name) {
    if (!HasOpenSslKeyAlgorithm(algorithm_name)) {
        Logger::Fatal("Required OpenSSL PQC algorithm unavailable: " + std::string(algorithm_name));
        exit(EXIT_FAILURE);
    }
}

// ---------------------------------------------------------------------------
// Hardened Dilithium5 Keypair Initialization
// ---------------------------------------------------------------------------
void InitDilithiumKeys() {
    OQS_init();

    dilithium_pub_key.resize(OQS_SIG_dilithium_5_length_public_key);
    dilithium_sec_key.resize(OQS_SIG_dilithium_5_length_secret_key);

    const size_t expected_size = OQS_SIG_dilithium_5_length_public_key
                               + OQS_SIG_dilithium_5_length_secret_key;

    std::string key_file = "data/dilithium5.key";
    bool loaded = false;

    if (std::filesystem::exists(key_file)) {
        // Verify file size matches exactly before reading
        auto file_size = std::filesystem::file_size(key_file);
        if (file_size == expected_size) {
            std::ifstream in(key_file, std::ios::binary);
            if (in) {
                in.read(reinterpret_cast<char*>(dilithium_pub_key.data()), dilithium_pub_key.size());
                in.read(reinterpret_cast<char*>(dilithium_sec_key.data()), dilithium_sec_key.size());
                loaded = true;
                Logger::Info("Loaded existing Dilithium5 server keypair (" + std::to_string(expected_size) + " bytes).");
            }
        } else {
            Logger::Warn("Dilithium5 key file size mismatch: expected "
                         + std::to_string(expected_size) + ", got "
                         + std::to_string(file_size) + ". Regenerating.");
        }
    }

    if (!loaded) {
        Logger::Info("Generating new Dilithium5 server keypair...");
        OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_dilithium_5);
        if (sig == nullptr) {
            Logger::Fatal("OQS_SIG_new(dilithium_5) returned nullptr. Is liboqs linked correctly?");
            exit(EXIT_FAILURE);
        }

        if (OQS_SIG_keypair(sig, dilithium_pub_key.data(), dilithium_sec_key.data()) != OQS_SUCCESS) {
            OQS_SIG_free(sig);
            Logger::Fatal("OQS_SIG_keypair failed.");
            exit(EXIT_FAILURE);
        }
        OQS_SIG_free(sig);

        std::ofstream out(key_file, std::ios::binary);
        if (!out) {
            Logger::Fatal("Failed to write Dilithium5 key file: " + key_file);
            exit(EXIT_FAILURE);
        }
        out.write(reinterpret_cast<const char*>(dilithium_pub_key.data()), dilithium_pub_key.size());
        out.write(reinterpret_cast<const char*>(dilithium_sec_key.data()), dilithium_sec_key.size());
        Logger::Info("Dilithium5 keypair saved to " + key_file);
    }
}

// Function to read a file into a string
std::string read_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "FATAL: Failed to open file: " << filepath << std::endl;
        exit(EXIT_FAILURE);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// Function to find the certificate directory by searching upwards from the executable's location.
std::string find_cert_dir() {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        return "";
    }
    exe_path[len] = '\0';

    std::filesystem::path current_dir = std::filesystem::path(exe_path).parent_path();

    while (current_dir.has_parent_path() && current_dir != current_dir.parent_path()) {
        std::filesystem::path cert_path = current_dir / "certs" / "ca.crt";
        if (std::filesystem::exists(cert_path)) {
            return (current_dir / "certs").string();
        }
        current_dir = current_dir.parent_path();
    }

    return "";
}


class PQCStringStoreServiceImpl final : public PQCStringStore::Service {
    Status Store(ServerContext* context, const StoreRequest* request, StoreResponse* response) override {
        try {
            int64_t id = db_manager->InsertMessage(request->message());
            response->set_id(id);
            return Status::OK;
        } catch (const std::exception& e) {
            return Status(grpc::StatusCode::INTERNAL, std::string("Storage error: ") + e.what());
        }
    }

    Status Retrieve(ServerContext* context, const RetrieveRequest* request, RetrieveResponse* response) override {
        std::string msg = db_manager->GetMessage(request->id());
        if (msg.empty()) {
            return Status(grpc::StatusCode::NOT_FOUND, "Message not found");
        }
        response->set_message(msg);
        return Status::OK;
    }

    Status RotateKey(ServerContext* context, const RotateKeyRequest* request, RotateKeyResponse* response) override {
        try {
            // This orchestrates the rotation: decrypt DB -> rotate key -> encrypt DB
            db_manager->ReEncryptAll();
            response->set_status("Key rotation successful");
            return Status::OK;
        } catch (const std::exception& e) {
            Logger::Error("Key rotation failed: " + std::string(e.what()));
            return Status(grpc::StatusCode::INTERNAL, std::string("Rotation error: ") + e.what());
        }
    }

    Status UploadFile(ServerContext* context, grpc::ServerReader<UploadFileRequest>* reader, UploadFileResponse* response) override {
        UploadFileRequest request;
        std::string filename;
        std::ofstream outfile;
        std::string temp_path;
        size_t total_size = 0;

        std::filesystem::create_directories("data/uploads");

        while (reader->Read(&request)) {
            if (request.has_metadata()) {
                filename = request.metadata().filename();
                // Basic sanitization to prevent directory traversal
                filename = std::filesystem::path(filename).filename().string();
                temp_path = "data/uploads/" + filename + ".tmp";

                outfile.open(temp_path, std::ios::binary);
                if (!outfile.is_open()) {
                    return Status(grpc::StatusCode::INTERNAL, "Failed to open temp file for writing");
                }
                Logger::Info("Receiving file: " + filename);
            } else if (request.has_chunk()) {
                if (!outfile.is_open()) {
                    return Status(grpc::StatusCode::INVALID_ARGUMENT, "Chunk received before metadata");
                }
                const std::string& chunk = request.chunk();
                outfile.write(chunk.data(), chunk.size());
                total_size += chunk.size();
            }
        }

        if (outfile.is_open()) {
            outfile.close();

            // Encrypt the temp file to the final destination (local .enc file)
            std::string final_path = "data/uploads/" + filename + ".enc";
            try {
                storage->EncryptFile(temp_path, final_path);
                std::filesystem::remove(temp_path); // Remove plaintext temp file

                const auto& config = Config::Instance().Get();
                if (config.use_s3) {
                    Logger::Info("Uploading encrypted file to S3...");
                    if (s3_client->UploadFile(final_path, filename + ".enc")) {
                        Logger::Info("S3 Upload Successful. Removing local encrypted file.");
                        std::filesystem::remove(final_path);
                    } else {
                        Logger::Error("S3 Upload Failed. Keeping local file.");
                        // Fallback logic: keep local file? Or fail?
                        // For "Industrial Grade", we should fail if S3 is mandatory,
                        // but having a local fallback is robust.
                    }
                }

                response->set_id(filename);
                response->set_size(total_size);
                Logger::Info("File processed successfully: " + filename);
            } catch (const std::exception& e) {
                std::filesystem::remove(temp_path);
                Logger::Error("Encryption/Upload failed: " + std::string(e.what()));
                return Status(grpc::StatusCode::INTERNAL, "Encryption/Upload failed");
            }
        } else {
             return Status(grpc::StatusCode::ABORTED, "Upload incomplete");
        }

        return Status::OK;
    }

    Status DownloadFile(ServerContext* context, const DownloadFileRequest* request, grpc::ServerWriter<DownloadFileResponse>* writer) override {
        std::string filename = request->id();
        filename = std::filesystem::path(filename).filename().string();
        std::string encrypted_path = "data/uploads/" + filename + ".enc";

        const auto& config = Config::Instance().Get();
        if (config.use_s3) {
            // Try to download from S3 if enabled
            if (!std::filesystem::exists(encrypted_path)) {
                Logger::Info("File not found locally, checking S3...");
                if (s3_client->DownloadFile(filename + ".enc", encrypted_path)) {
                    Logger::Info("Downloaded from S3 to " + encrypted_path);
                } else {
                    return Status(grpc::StatusCode::NOT_FOUND, "File not found in S3 or Local");
                }
            }
        } else {
            if (!std::filesystem::exists(encrypted_path)) {
                return Status(grpc::StatusCode::NOT_FOUND, "File not found");
            }
        }

        // Decrypt to a temp file for streaming
        std::string temp_path = "data/uploads/" + filename + ".tmp.dec";

        try {
            storage->DecryptFile(encrypted_path, temp_path);
        } catch (const std::exception& e) {
            Logger::Error("Decryption failed: " + std::string(e.what()));
            return Status(grpc::StatusCode::INTERNAL, "Decryption failed");
        }

        std::ifstream infile(temp_path, std::ios::binary);
        if (!infile.is_open()) {
            std::filesystem::remove(temp_path);
            return Status(grpc::StatusCode::INTERNAL, "Failed to open decrypted file");
        }

        Logger::Info("Sending file: " + filename);
        char buffer[1024 * 64]; // 64KB chunks
        while (!infile.eof()) {
            infile.read(buffer, sizeof(buffer));
            std::streamsize bytes_read = infile.gcount();
            if (bytes_read > 0) {
                DownloadFileResponse response;
                response.set_chunk(std::string(buffer, bytes_read));
                writer->Write(response);
            }
        }

        infile.close();
        std::filesystem::remove(temp_path); // Cleanup temp file

        return Status::OK;
    }
};

class HealthServiceImpl final : public Health::Service {
    Status Check(ServerContext* context, const HealthCheckRequest* request, HealthCheckResponse* response) override {
        response->set_status(HealthCheckResponse::SERVING);
        return Status::OK;
    }

    Status Watch(ServerContext* context, const HealthCheckRequest* request, grpc::ServerWriter<HealthCheckResponse>* writer) override {
        HealthCheckResponse response;
        response.set_status(HealthCheckResponse::SERVING);
        writer->Write(response);
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
        return Status::OK;
    }
};


void RunServer(const std::string& cert_dir) {
    const auto& config = Config::Instance().Get();
    std::string server_address = config.address;
    PQCStringStoreServiceImpl pqc_service;
    HealthServiceImpl health_service;

    grpc::SslServerCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = read_file(cert_dir + "/ca.crt");
    grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp = {
        read_file(cert_dir + "/server.key"),
        read_file(cert_dir + "/server.crt")
    };
    ssl_opts.pem_key_cert_pairs.push_back(pkcp);
    ssl_opts.client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;

    auto server_credentials = grpc::SslServerCredentials(ssl_opts);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, server_credentials);
    builder.RegisterService(&pqc_service);
    builder.RegisterService(&health_service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    if (server == nullptr) {
        Logger::Fatal("Failed to build or start the gRPC server.");
        exit(EXIT_FAILURE);
    }
    Logger::Info("Server listening on " + server_address);
    server->Wait();
}

int main(int argc, char** argv) {
    // Initialize Config
    std::string cert_dir = find_cert_dir();
    if (cert_dir.empty()) {
        std::cerr << "FATAL: Could not find certificate directory. Searched upwards from executable path." << std::endl;
        exit(EXIT_FAILURE);
    }
    std::string root_dir = std::filesystem::path(cert_dir).parent_path().string();
    Config::Instance().Load(root_dir + "/config.json");

    // Initialize OpenSSL library
    Logger::Info("Initializing OpenSSL...");
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS | OPENSSL_INIT_LOAD_CONFIG, NULL);
    OSSL_PROVIDER* default_provider = OSSL_PROVIDER_load(nullptr, "default");
    OSSL_PROVIDER* base_provider = OSSL_PROVIDER_load(nullptr, "base");
    if (!default_provider || !base_provider) {
        Logger::Fatal("Failed to load required OpenSSL providers.");
        exit(EXIT_FAILURE);
    }
    RequireOpenSslKeyAlgorithm("ML-DSA-87");
    RequireOpenSslKeyAlgorithm("ML-KEM-768");
    RequireOpenSslKeyAlgorithm("ML-KEM-1024");
    Logger::Info("OpenSSL version: " + std::string(OpenSSL_version(OPENSSL_VERSION)));
    Logger::Info("OpenSSL config: " + std::string(std::getenv("OPENSSL_CONF") ? std::getenv("OPENSSL_CONF") : "(unset)"));
    Logger::Info("Verified OpenSSL native PQC support for ML-DSA-87, ML-KEM-768, and ML-KEM-1024.");
    Logger::Info("Found certificate directory: " + cert_dir);

    const auto& config = Config::Instance().Get();
    std::filesystem::create_directories("data");

    // Initialize Secure Storage (AES-256-GCM key)
    Logger::Info("Initializing Secure Storage...");
    storage = std::make_unique<SecureStorage>(config.key_path);

    // Initialize Dilithium5 server keypair (for batch signing)
    Logger::Info("Initializing Dilithium5 keypair...");
    InitDilithiumKeys();

    // Initialize Database Manager
    // Use PG_CONN_STRING env var from docker-compose, fallback to config.storage_path
    std::string pg_conn = "";
    const char* pg_env = std::getenv("PG_CONN_STRING");
    if (pg_env && std::strlen(pg_env) > 0) {
        pg_conn = pg_env;
        Logger::Info("Using PostgreSQL connection from PG_CONN_STRING environment variable.");
    } else {
        // Fallback: use the config storage_path as a PG connection string
        pg_conn = config.storage_path;
        Logger::Warn("PG_CONN_STRING not set. Using config.storage_path as connection string.");
    }

    Logger::Info("Initializing Database Manager...");
    db_manager = std::make_unique<DBManager>(pg_conn, *storage);

    if (config.use_s3) {
        Logger::Info("Initializing S3 Client (" + config.s3_endpoint + ")...");
        s3_client = std::make_unique<S3Client>(config);
    }

    // Start HTTPS Gateway in a separate thread (TLS 1.3 policy + Dilithium5 signing)
    std::thread http_thread([&cert_dir]() {
        RunHTTPServer(*db_manager, 8080, cert_dir, dilithium_pub_key, dilithium_sec_key);
    });
    http_thread.detach();

    RunServer(cert_dir);

    return 0;
}
