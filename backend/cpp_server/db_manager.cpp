#include "db_manager.hpp"
#include "logger.hpp"
#include <condition_variable>
#include "config.hpp"
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <thread>
#include <iomanip>
#include <openssl/rand.h>
#include <cstring>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <unordered_set>
#include <openssl/evp.h>
#include <array>

namespace {


std::string sha256_hex(const std::string& value) {
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

std::string HexEncode(const std::string& input) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(input.size() * 2);
    for (unsigned char c : input) {
        out.push_back(kHex[(c >> 4) & 0x0F]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    throw std::runtime_error("Invalid hex character");
}

std::string HexDecode(const std::string& input) {
    if (input.empty()) return "";
    if (input.size() % 2 != 0) {
        throw std::runtime_error("Invalid hex payload length");
    }

    std::string out;
    out.reserve(input.size() / 2);
    for (size_t i = 0; i < input.size(); i += 2) {
        char byte = static_cast<char>((HexValue(input[i]) << 4) | HexValue(input[i + 1]));
        out.push_back(byte);
    }
    return out;
}

std::string TrimCopy(const std::string& value) {
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

std::string ShortHash(const std::string& value) {
    if (value.size() <= 10) return value;
    return value.substr(0, 8) + "...";
}

bool LooksLikePqcEnvelope(const std::string& value) {
    return value.find("\"scheme\":\"ML-KEM-1024+AES-256-GCM\"") != std::string::npos;
}

bool LooksLikeInlineE2EEImage(const std::string& value) {
    return value.rfind("__QC_E2EE_IMAGE__:", 0) == 0;
}

bool IsHexString(const std::string& value) {
    if (value.empty() || (value.size() % 2) != 0) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

std::string DecodeStoredDirectMessageContent(const std::string& stored,
                                             SecureStorage& secure_storage,
                                             bool* legacy_server_wrapped = nullptr) {
    if (legacy_server_wrapped) {
        *legacy_server_wrapped = false;
    }
    if (stored.empty()) {
        return "";
    }
    if (LooksLikePqcEnvelope(stored) || LooksLikeInlineE2EEImage(stored)) {
        return stored;
    }
    if (IsHexString(stored)) {
        try {
            std::string decoded = secure_storage.Decrypt(HexDecode(stored));
            if (legacy_server_wrapped) {
                *legacy_server_wrapped = true;
            }
            return decoded;
        } catch (const std::exception&) {
        }
    }
    return stored;
}

std::string BuildDirectMessagePreview(const std::string& sender,
                                      const std::string& receiver,
                                      const std::string& stored_content,
                                      const std::string& image_url,
                                      SecureStorage& secure_storage) {
    const bool admin_conversation = sender == "admin" || receiver == "admin";
    if (!image_url.empty()) {
        return admin_conversation ? "[Image]" : "[Private attachment]";
    }
    if (stored_content.empty()) {
        return "";
    }
    if (!admin_conversation) {
        return "[Private message]";
    }

    std::string decoded = DecodeStoredDirectMessageContent(stored_content, secure_storage);
    if (LooksLikePqcEnvelope(decoded)) {
        return "[PQC Encrypted Message]";
    }
    if (LooksLikeInlineE2EEImage(decoded)) {
        return "[E2EE Image]";
    }
    return decoded;
}

std::string NormalizeRole(const std::string& value) {
    std::string lowered = TrimCopy(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lowered == "founder" || lowered == "moderator" || lowered == "user") {
        return lowered;
    }
    return "user";
}

std::string NormalizeUsernameStorage(const std::string& value) {
    std::string trimmed = TrimCopy(value);
    std::string normalized;
    bool last_separator = false;
    for (unsigned char ch : trimmed) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(ch));
            last_separator = false;
            continue;
        }
        if (ch == '_' || ch == '-' || std::isspace(ch)) {
            if (!normalized.empty() && !last_separator) {
                normalized.push_back('_');
                last_separator = true;
            }
            continue;
        }
        return "";
    }
    while (!normalized.empty() && normalized.back() == '_') {
        normalized.pop_back();
    }
    return normalized;
}

std::string CanonicalUsernameKey(const std::string& value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            lowered.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return lowered;
}

bool IsReservedUsername(const std::string& value) {
    static const std::unordered_set<std::string> reserved = {
        "founder", "moderator", "admin", "administrator", "system", "staff",
        "support", "official", "team", "contact", "owner", "quanchan"
    };
    return reserved.count(CanonicalUsernameKey(value)) > 0;
}

} // namespace

// =============================================================================
// Connection & Lifecycle
// =============================================================================

class DBManager::ConnectionPool {
public:
    ConnectionPool(const std::string& conn_info, size_t pool_size)
        : conn_info_(conn_info), pool_size_(pool_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < pool_size_; ++i) {
            PGconn* conn = PQconnectdb(conn_info_.c_str());
            if (!conn || PQstatus(conn) != CONNECTION_OK) {
                std::string err = conn ? PQerrorMessage(conn) : "PQconnectdb returned null";
                if (conn) PQfinish(conn);
                for (auto c : pool_) {
                    PQfinish(c);
                }
                throw std::runtime_error("ConnectionPool initialization failed: " + err);
            }
            pool_.push_back(conn);
        }
    }

    ~ConnectionPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto conn : pool_) {
            PQfinish(conn);
        }
        pool_.clear();
    }

    PGconn* Acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return !pool_.empty(); });
        PGconn* conn = pool_.back();
        pool_.pop_back();

        if (!conn || PQstatus(conn) != CONNECTION_OK) {
            if (conn) PQfinish(conn);
            conn = PQconnectdb(conn_info_.c_str());
            if (!conn || PQstatus(conn) != CONNECTION_OK) {
                std::string err = conn ? PQerrorMessage(conn) : "PQconnectdb returned null";
                Logger::Error("Failed to heal database connection in Acquire(): " + err);
                if (conn) PQfinish(conn);
                conn = nullptr;
            }
        }
        return conn;
    }

    void Release(PGconn* conn) {
        if (!conn) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (PQstatus(conn) != CONNECTION_OK) {
            Logger::Warn("Connection pool found a dead connection in Release. Reconnecting...");
            PQfinish(conn);
            conn = PQconnectdb(conn_info_.c_str());
            if (!conn || PQstatus(conn) != CONNECTION_OK) {
                Logger::Error("Failed to reconnect connection in pool. Replacing with nullptr.");
                if (conn) PQfinish(conn);
                conn = nullptr;
            }
        }
        pool_.push_back(conn);
        cv_.notify_one();
    }

private:
    std::string conn_info_;
    size_t pool_size_;
    std::vector<PGconn*> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

struct ConnLease {
    DBManager::ConnectionPool& pool;
    PGconn* conn;

    ConnLease(DBManager::ConnectionPool& p) : pool(p), conn(p.Acquire()) {}
    ~ConnLease() { pool.Release(conn); }

    PGconn* get() const { return conn; }
};

DBManager::DBManager(const std::string& conn_info, SecureStorage& secure_storage)
    : conn_info_(conn_info), secure_storage_(secure_storage) {
    pool_ = std::make_unique<ConnectionPool>(conn_info_, 20);
    Logger::Info("Connected to PostgreSQL with a connection pool (size=20).");
    Init();
}

DBManager::~DBManager() {
}

void DBManager::Execute(const std::string& sql, PGconn* conn) {
    if (conn) {
        PGresultPtr res(PQexec(conn, sql.c_str()));
        ExecStatusType status = res ? PQresultStatus(res.get()) : PGRES_FATAL_ERROR;
        if (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK) {
            return;
        }
        const std::string err = conn ? PQerrorMessage(conn) : "null PostgreSQL connection";
        throw std::runtime_error("SQL Execute error inside transaction: " + err + " | SQL: " + sql);
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        ConnLease lease(*pool_);
        PGconn* active_conn = lease.get();
        if (!active_conn) {
            if (attempt == 0) continue;
            throw std::runtime_error("SQL Execute error: no active connection | SQL: " + sql);
        }

        PGresultPtr res(PQexec(active_conn, sql.c_str()));
        ExecStatusType status = res ? PQresultStatus(res.get()) : PGRES_FATAL_ERROR;
        if (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK) {
            return;
        }

        const bool should_retry = active_conn && PQstatus(active_conn) != CONNECTION_OK && attempt == 0;
        const std::string err = active_conn ? PQerrorMessage(active_conn) : "null PostgreSQL connection";
        if (!should_retry) {
            throw std::runtime_error("SQL Execute error: " + err + " | SQL: " + sql);
        }
    }
}

PGresultPtr DBManager::Query(const std::string& sql, PGconn* conn) {
    if (conn) {
        PGresultPtr res(PQexec(conn, sql.c_str()));
        ExecStatusType status = res ? PQresultStatus(res.get()) : PGRES_FATAL_ERROR;
        if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) {
            return res;
        }
        const std::string err = conn ? PQerrorMessage(conn) : "null PostgreSQL connection";
        throw std::runtime_error("SQL Query error inside transaction: " + err + " | SQL: " + sql);
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        ConnLease lease(*pool_);
        PGconn* active_conn = lease.get();
        if (!active_conn) {
            if (attempt == 0) continue;
            throw std::runtime_error("SQL Query error: no active connection | SQL: " + sql);
        }

        PGresultPtr res(PQexec(active_conn, sql.c_str()));
        ExecStatusType status = res ? PQresultStatus(res.get()) : PGRES_FATAL_ERROR;
        if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) {
            return res;
        }

        const bool should_retry = active_conn && PQstatus(active_conn) != CONNECTION_OK && attempt == 0;
        const std::string err = active_conn ? PQerrorMessage(active_conn) : "null PostgreSQL connection";
        if (!should_retry) {
            throw std::runtime_error("SQL Query error: " + err + " | SQL: " + sql);
        }
    }
    throw std::runtime_error("SQL Query error: retry loop exhausted | SQL: " + sql);
}

PGresultPtr DBManager::QueryParams(const std::string& sql,
                                    const std::vector<std::string>& params,
                                    PGconn* conn) {
    if (conn) {
        std::vector<const char*> values;
        values.reserve(params.size());
        for (const auto& p : params) {
            values.push_back(p.c_str());
        }

        PGresultPtr res(PQexecParams(conn, sql.c_str(),
                                      static_cast<int>(params.size()),
                                      nullptr,        // paramTypes (infer)
                                      values.data(),
                                      nullptr,        // paramLengths
                                      nullptr,        // paramFormats
                                      0));            // resultFormat (text)

        ExecStatusType status = res ? PQresultStatus(res.get()) : PGRES_FATAL_ERROR;
        if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) {
            return res;
        }
        const std::string err = conn ? PQerrorMessage(conn) : "null PostgreSQL connection";
        throw std::runtime_error("SQL QueryParams error inside transaction: " + err + " | SQL: " + sql);
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        ConnLease lease(*pool_);
        PGconn* active_conn = lease.get();
        if (!active_conn) {
            if (attempt == 0) continue;
            throw std::runtime_error("SQL QueryParams error: no active connection | SQL: " + sql);
        }

        std::vector<const char*> values;
        values.reserve(params.size());
        for (const auto& p : params) {
            values.push_back(p.c_str());
        }

        PGresultPtr res(PQexecParams(active_conn, sql.c_str(),
                                      static_cast<int>(params.size()),
                                      nullptr,        // paramTypes (infer)
                                      values.data(),
                                      nullptr,        // paramLengths
                                      nullptr,        // paramFormats
                                      0));            // resultFormat (text)

        ExecStatusType status = res ? PQresultStatus(res.get()) : PGRES_FATAL_ERROR;
        if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) {
            return res;
        }

        const bool should_retry = active_conn && PQstatus(active_conn) != CONNECTION_OK && attempt == 0;
        const std::string err = active_conn ? PQerrorMessage(active_conn) : "null PostgreSQL connection";
        if (!should_retry) {
            throw std::runtime_error("SQL QueryParams error: " + err + " | SQL: " + sql);
        }
    }
    throw std::runtime_error("SQL QueryParams error: retry loop exhausted | SQL: " + sql);
}

// =============================================================================
// Schema Initialization
// =============================================================================

void DBManager::Init() {
    // Original PQC encrypted message store (migrated to PG)
    Execute(
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  data BYTEA"
        ");"
    );

    // Imageboard tables
    Execute(
        "CREATE TABLE IF NOT EXISTS boards ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  description TEXT DEFAULT '',"
        "  icon TEXT DEFAULT '',"
        "  nsfw BOOLEAN DEFAULT FALSE"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS threads ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  board_id TEXT NOT NULL REFERENCES boards(id),"
        "  subject TEXT DEFAULT 'No Subject',"
        "  sticky BOOLEAN DEFAULT FALSE,"
        "  locked BOOLEAN DEFAULT FALSE,"
        "  archived BOOLEAN DEFAULT FALSE,"
        "  created_at TIMESTAMPTZ DEFAULT NOW(),"
        "  last_bump  TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS posts ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  thread_id BIGINT NOT NULL REFERENCES threads(id) ON DELETE CASCADE,"
        "  board_id TEXT NOT NULL REFERENCES boards(id),"
        "  content TEXT NOT NULL,"
        "  encrypted_content TEXT,"
        "  is_encrypted BOOLEAN DEFAULT FALSE,"
        "  image_url TEXT,"
        "  name TEXT DEFAULT 'Anonymous',"
        "  tripcode TEXT,"
        "  sage BOOLEAN DEFAULT FALSE,"
        "  is_op BOOLEAN DEFAULT FALSE,"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );
    Execute("ALTER TABLE posts ADD COLUMN IF NOT EXISTS author_hash TEXT DEFAULT '';");

    // Create indexes for common queries
    Execute("CREATE INDEX IF NOT EXISTS idx_threads_board ON threads(board_id, archived, last_bump DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_posts_thread ON posts(thread_id, created_at ASC);");

    // V4 Social Layer Tables
    Execute(
        "CREATE TABLE IF NOT EXISTS profiles ("
        "  pub_key_hash TEXT PRIMARY KEY,"
        "  username TEXT,"
        "  last_active TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS pqc_kem_public_key TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS pqc_kem_scheme TEXT DEFAULT 'ML-KEM-1024';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS identity_public_key TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS pqc_identity_public_key TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS pqc_identity_scheme TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS identity_binding_payload TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS identity_binding_signature TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS role TEXT DEFAULT 'user';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS role_assigned_by TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS role_assigned_at TIMESTAMPTZ;");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS founder_session_hash TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS founder_claimed_at TIMESTAMPTZ;");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS moderator_badge TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS recovery_lookup_hash TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS recovery_bundle_ciphertext TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS recovery_bundle_iv TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS recovery_bundle_updated_at TIMESTAMPTZ;");

    Execute(
        "CREATE TABLE IF NOT EXISTS interactions ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  post_id BIGINT NOT NULL REFERENCES posts(id) ON DELETE CASCADE,"
        "  pub_key_hash TEXT NOT NULL,"
        "  type SMALLINT NOT NULL," // 1 = like, -1 = dislike
        "  created_at TIMESTAMPTZ DEFAULT NOW(),"
        "  UNIQUE(post_id, pub_key_hash)" // one vote per user per post
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS friend_requests ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  sender_hash TEXT NOT NULL,"
        "  receiver_hash TEXT NOT NULL,"
        "  status SMALLINT DEFAULT 0," // 0 = pending, 1 = accepted, 2 = rejected, 3 = canceled, 4 = removed
        "  created_at TIMESTAMPTZ DEFAULT NOW(),"
        "  UNIQUE(sender_hash, receiver_hash)" // no duplicate requests
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS direct_messages ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  sender_hash TEXT NOT NULL,"
        "  receiver_hash TEXT NOT NULL,"
        "  content TEXT,"
        "  image_url TEXT DEFAULT '',"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );
    Execute("ALTER TABLE direct_messages ADD COLUMN IF NOT EXISTS read_at TIMESTAMPTZ;");

    Execute(
        "CREATE TABLE IF NOT EXISTS message_requests ("
        "  requester_hash TEXT NOT NULL,"
        "  recipient_hash TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'pending',"
        "  created_at TIMESTAMPTZ DEFAULT NOW(),"
        "  updated_at TIMESTAMPTZ DEFAULT NOW(),"
        "  last_message_at TIMESTAMPTZ DEFAULT NOW(),"
        "  PRIMARY KEY (requester_hash, recipient_hash)"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS blocks ("
        "  blocker_hash TEXT NOT NULL,"
        "  blocked_hash TEXT NOT NULL,"
        "  created_at TIMESTAMPTZ DEFAULT NOW(),"
        "  PRIMARY KEY (blocker_hash, blocked_hash)"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS notifications ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  user_hash TEXT NOT NULL,"
        "  actor_hash TEXT DEFAULT '',"
        "  type TEXT NOT NULL,"
        "  title TEXT NOT NULL,"
        "  body TEXT DEFAULT '',"
        "  link TEXT DEFAULT '',"
        "  read_at TIMESTAMPTZ,"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS reports ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  reporter_hash TEXT NOT NULL,"
        "  target_hash TEXT NOT NULL,"
        "  reason TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'open',"
        "  created_at TIMESTAMPTZ DEFAULT NOW(),"
        "  resolved_at TIMESTAMPTZ,"
        "  resolution_note TEXT DEFAULT ''"
        ");"
    );
    Execute("ALTER TABLE reports ADD COLUMN IF NOT EXISTS target_kind TEXT DEFAULT 'user';");
    Execute("ALTER TABLE reports ADD COLUMN IF NOT EXISTS target_post_id BIGINT;");
    Execute("ALTER TABLE reports ADD COLUMN IF NOT EXISTS target_thread_id BIGINT;");
    Execute("ALTER TABLE reports ADD COLUMN IF NOT EXISTS target_board_id TEXT DEFAULT '';");
    Execute("ALTER TABLE reports ADD COLUMN IF NOT EXISTS target_display_name TEXT DEFAULT '';");
    Execute("ALTER TABLE reports ADD COLUMN IF NOT EXISTS context_link TEXT DEFAULT '';");
    Execute("ALTER TABLE reports ADD COLUMN IF NOT EXISTS resolved_by_hash TEXT DEFAULT '';");
    Execute("ALTER TABLE reports ADD COLUMN IF NOT EXISTS resolved_by_label TEXT DEFAULT '';");
    Execute("ALTER TABLE reports ADD COLUMN IF NOT EXISTS resolved_by_badge TEXT DEFAULT '';");

    Execute("CREATE EXTENSION IF NOT EXISTS \"pgcrypto\";");
    Execute(
        "CREATE TABLE IF NOT EXISTS bans ("
        "  target_identifier TEXT PRIMARY KEY,"
        "  ban_type TEXT DEFAULT 'identity',"
        "  reason TEXT DEFAULT '',"
        "  expires_at TIMESTAMPTZ,"
        "  created_by TEXT,"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );
    try {
        Execute("ALTER TABLE bans DROP CONSTRAINT IF EXISTS bans_pkey CASCADE;");
    } catch (...) {}
    Execute("ALTER TABLE bans ADD COLUMN IF NOT EXISTS id UUID DEFAULT gen_random_uuid();");
    Execute("ALTER TABLE bans ADD COLUMN IF NOT EXISTS target_identifier TEXT;");
    Execute("ALTER TABLE bans ADD COLUMN IF NOT EXISTS ban_type TEXT DEFAULT 'identity';");
    Execute("ALTER TABLE bans ADD COLUMN IF NOT EXISTS expires_at TIMESTAMPTZ;");
    Execute("ALTER TABLE bans ADD COLUMN IF NOT EXISTS created_by TEXT;");

    try {
        Execute("DELETE FROM bans WHERE target_identifier IS NULL AND target_hash IS NOT NULL;");
    } catch (...) {}
    try {
        Execute("ALTER TABLE bans ALTER COLUMN target_identifier SET NOT NULL;");
    } catch (...) {}
    try {
        Execute("ALTER TABLE bans ADD PRIMARY KEY (id);");
    } catch (...) {}
    try {
        Execute("ALTER TABLE bans ADD CONSTRAINT bans_target_identifier_unique UNIQUE (target_identifier);");
    } catch (...) {}

    Execute(
        "CREATE TABLE IF NOT EXISTS notification_queue ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  user_hash TEXT NOT NULL,"
        "  actor_hash TEXT DEFAULT '',"
        "  type TEXT NOT NULL,"
        "  title TEXT NOT NULL,"
        "  body TEXT DEFAULT '',"
        "  link TEXT DEFAULT '',"
        "  retry_count INT NOT NULL DEFAULT 0,"
        "  next_retry_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),"
        "  status TEXT NOT NULL DEFAULT 'pending',"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS moderation_queue ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  report_id BIGINT REFERENCES reports(id) ON DELETE CASCADE,"
        "  status TEXT NOT NULL DEFAULT 'pending',"
        "  assigned_to TEXT,"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS moderation_log ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  actor_hash TEXT NOT NULL,"
        "  action TEXT NOT NULL,"
        "  summary TEXT NOT NULL,"
        "  target_hash TEXT,"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS api_keys ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  user_hash TEXT UNIQUE NOT NULL,"
        "  api_key TEXT UNIQUE NOT NULL,"
        "  tier TEXT NOT NULL,"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );

    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS subscription_tier TEXT DEFAULT 'none';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS subscription_expires_at TIMESTAMPTZ;");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS custom_badge TEXT DEFAULT '';");
    Execute("ALTER TABLE profiles ADD COLUMN IF NOT EXISTS unlocked_tags TEXT DEFAULT '';");

    Execute(
        "CREATE TABLE IF NOT EXISTS groups ("
        "  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),"
        "  name TEXT NOT NULL,"
        "  created_by TEXT NOT NULL,"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS group_members ("
        "  group_id UUID REFERENCES groups(id) ON DELETE CASCADE,"
        "  user_hash TEXT NOT NULL,"
        "  encrypted_group_key TEXT NOT NULL,"
        "  role TEXT NOT NULL DEFAULT 'member',"
        "  joined_at TIMESTAMPTZ DEFAULT NOW(),"
        "  PRIMARY KEY (group_id, user_hash)"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS group_messages ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  group_id UUID REFERENCES groups(id) ON DELETE CASCADE,"
        "  sender_hash TEXT NOT NULL,"
        "  encrypted_content TEXT NOT NULL,"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS moderation_events ("
        "  id BIGSERIAL PRIMARY KEY,"
        "  actor_hash TEXT NOT NULL,"
        "  actor_label TEXT DEFAULT '',"
        "  actor_badge TEXT DEFAULT '',"
        "  action TEXT NOT NULL,"
        "  summary TEXT NOT NULL DEFAULT '',"
        "  target_hash TEXT DEFAULT '',"
        "  target_label TEXT DEFAULT '',"
        "  target_badge TEXT DEFAULT '',"
        "  report_id BIGINT,"
        "  target_post_id BIGINT,"
        "  target_thread_id BIGINT,"
        "  target_board_id TEXT DEFAULT '',"
        "  created_at TIMESTAMPTZ DEFAULT NOW()"
        ");"
    );
    Execute("ALTER TABLE moderation_events ADD COLUMN IF NOT EXISTS actor_label TEXT DEFAULT '';");
    Execute("ALTER TABLE moderation_events ADD COLUMN IF NOT EXISTS actor_badge TEXT DEFAULT '';");
    Execute("ALTER TABLE moderation_events ADD COLUMN IF NOT EXISTS target_label TEXT DEFAULT '';");
    Execute("ALTER TABLE moderation_events ADD COLUMN IF NOT EXISTS target_badge TEXT DEFAULT '';");

    Execute("CREATE INDEX IF NOT EXISTS idx_interactions_post ON interactions(post_id);");
    Execute("CREATE INDEX IF NOT EXISTS idx_friends_users ON friend_requests(sender_hash, receiver_hash, status);");
    Execute("CREATE INDEX IF NOT EXISTS idx_direct_messages_pair ON direct_messages(sender_hash, receiver_hash, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_direct_messages_receiver ON direct_messages(receiver_hash, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_message_requests_recipient ON message_requests(recipient_hash, status, last_message_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_message_requests_requester ON message_requests(requester_hash, status, last_message_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_blocks_blocker ON blocks(blocker_hash, blocked_hash);");
    Execute("CREATE INDEX IF NOT EXISTS idx_posts_author_hash ON posts(author_hash, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_profiles_lower_username ON profiles(LOWER(username));");
    Execute("CREATE INDEX IF NOT EXISTS idx_direct_messages_reverse ON direct_messages(receiver_hash, sender_hash, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_notifications_user ON notifications(user_hash, read_at, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_reports_target ON reports(target_hash, status, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_reports_post ON reports(target_post_id, status, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_bans_target_identifier ON bans(target_identifier);");
    Execute("CREATE INDEX IF NOT EXISTS idx_notification_queue_status ON notification_queue(status, next_retry_at);");
    Execute("CREATE INDEX IF NOT EXISTS idx_api_keys_hash ON api_keys(user_hash);");
    Execute("CREATE INDEX IF NOT EXISTS idx_api_keys_key ON api_keys(api_key);");
    Execute("CREATE INDEX IF NOT EXISTS idx_group_members_user ON group_members(user_hash);");
    Execute("CREATE INDEX IF NOT EXISTS idx_group_messages_group ON group_messages(group_id, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_moderation_events_created ON moderation_events(created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_moderation_events_actor ON moderation_events(actor_hash, created_at DESC);");

    int migrated_dm_ciphertexts = 0;
    int redacted_dm_bodies = 0;
    int cleared_dm_images = 0;
    PGresultPtr legacy_dm_rows = Query(
        "SELECT id, sender_hash, receiver_hash, COALESCE(content, ''), COALESCE(image_url, '') "
        "FROM direct_messages ORDER BY id ASC"
    );
    for (int i = 0; i < PQntuples(legacy_dm_rows.get()); ++i) {
        const std::string id = PQgetvalue(legacy_dm_rows.get(), i, 0);
        const std::string sender = PQgetvalue(legacy_dm_rows.get(), i, 1);
        const std::string receiver = PQgetvalue(legacy_dm_rows.get(), i, 2);
        const bool admin_conversation = sender == "admin" || receiver == "admin";
        if (admin_conversation) {
            continue;
        }

        const std::string stored_content = PQgetvalue(legacy_dm_rows.get(), i, 3);
        const std::string stored_image_url = PQgetvalue(legacy_dm_rows.get(), i, 4);
        std::string migrated_content = stored_content;
        std::string migrated_image_url = stored_image_url;
        bool should_update = false;

        if (!stored_content.empty()) {
            bool legacy_server_wrapped = false;
            const std::string decoded = DecodeStoredDirectMessageContent(stored_content, secure_storage_, &legacy_server_wrapped);
            if (legacy_server_wrapped) {
                if (LooksLikePqcEnvelope(decoded) || LooksLikeInlineE2EEImage(decoded)) {
                    migrated_content = decoded;
                    ++migrated_dm_ciphertexts;
                } else {
                    migrated_content.clear();
                    ++redacted_dm_bodies;
                }
                should_update = true;
            } else if (!LooksLikePqcEnvelope(stored_content) && !LooksLikeInlineE2EEImage(stored_content)) {
                migrated_content.clear();
                ++redacted_dm_bodies;
                should_update = true;
            }
        }

        if (!stored_image_url.empty()) {
            migrated_image_url.clear();
            ++cleared_dm_images;
            should_update = true;
        }

        if (should_update) {
            QueryParams(
                "UPDATE direct_messages SET content = NULLIF($2, ''), image_url = $3 WHERE id = $1",
                {id, migrated_content, migrated_image_url}
            );
        }
    }
    if (migrated_dm_ciphertexts > 0 || redacted_dm_bodies > 0 || cleared_dm_images > 0) {
        Logger::Info(
            "DM privacy migration complete. migrated_ciphertexts=" + std::to_string(migrated_dm_ciphertexts)
            + ", redacted_bodies=" + std::to_string(redacted_dm_bodies)
            + ", cleared_images=" + std::to_string(cleared_dm_images)
        );
    }

    SeedBoards();
    StartNotificationWorker();
}

// =============================================================================
// Original PQC Encrypted Store (gRPC-compatible)
// =============================================================================

int64_t DBManager::InsertMessage(const std::string& message) {
    std::string encrypted = secure_storage_.Encrypt(message);

    // Use PQexecParams with binary data for the BYTEA blob
    const char* paramValues[1] = { encrypted.data() };
    int paramLengths[1] = { static_cast<int>(encrypted.size()) };
    int paramFormats[1] = { 1 }; // binary

    ConnLease lease(*pool_);
    PGconn* conn = lease.get();
    if (!conn) throw std::runtime_error("InsertMessage failed: database connection unavailable");

    PGresultPtr res(PQexecParams(conn,
        "INSERT INTO messages (data) VALUES ($1) RETURNING id",
        1, nullptr, paramValues, paramLengths, paramFormats, 0));

    if (PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
        throw std::runtime_error("InsertMessage failed: " + std::string(PQerrorMessage(conn)));
    }

    return std::stoll(PQgetvalue(res.get(), 0, 0));
}

std::string DBManager::GetMessage(int64_t id) {
    std::string id_str = std::to_string(id);
    PGresultPtr res = QueryParams(
        "SELECT data FROM messages WHERE id = $1", {id_str});

    if (PQntuples(res.get()) == 0) return "";

    // PG returns BYTEA as hex-escaped string in text mode; use binary mode instead
    int len = PQgetlength(res.get(), 0, 0);
    const char* blob = PQgetvalue(res.get(), 0, 0);

    // In text mode, BYTEA comes as \\x hex. We need to unescape it.
    size_t out_len = 0;
    unsigned char* unescaped = PQunescapeBytea(
        reinterpret_cast<const unsigned char*>(blob), &out_len);
    if (!unescaped) return "";

    std::string encrypted(reinterpret_cast<const char*>(unescaped), out_len);
    PQfreemem(unescaped);

    try {
        return secure_storage_.Decrypt(encrypted);
    } catch (const std::exception& e) {
        Logger::Error("Failed to decrypt message ID " + std::to_string(id) + ": " + e.what());
        return "";
    }
}

void DBManager::ReEncryptAll() {
    Logger::Info("Starting database re-encryption...");

    ConnLease lease(*pool_);
    PGconn* conn = lease.get();
    if (!conn) throw std::runtime_error("ReEncryptAll failed: database connection unavailable");

    // 1. Read all data
    PGresultPtr select_res = Query("SELECT id, data FROM messages", conn);
    int rows = PQntuples(select_res.get());

    std::vector<std::pair<int64_t, std::string>> plaintext_rows;
    for (int i = 0; i < rows; ++i) {
        int64_t id = std::stoll(PQgetvalue(select_res.get(), i, 0));
        const char* blob = PQgetvalue(select_res.get(), i, 1);
        size_t out_len = 0;
        unsigned char* unescaped = PQunescapeBytea(
            reinterpret_cast<const unsigned char*>(blob), &out_len);
        if (!unescaped) continue;
        std::string encrypted(reinterpret_cast<const char*>(unescaped), out_len);
        PQfreemem(unescaped);

        try {
            plaintext_rows.push_back({id, secure_storage_.Decrypt(encrypted)});
        } catch (...) {
            Logger::Warn("Skipping row " + std::to_string(id) + " due to decrypt failure");
        }
    }
    select_res.reset(); // free before transaction

    // 2. Generate new key
    auto new_key = SecureStorage::GenerateKey();

    // 3. Backup old key, switch to new key, encrypt
    std::vector<unsigned char> old_key(32);
    std::memcpy(old_key.data(), secure_storage_.GetKey(), 32);
    secure_storage_.SetKey(new_key);

    std::vector<std::pair<int64_t, std::string>> encrypted_rows;
    for (const auto& row : plaintext_rows) {
        encrypted_rows.push_back({row.first, secure_storage_.Encrypt(row.second)});
    }

    // 4. Atomic: save new key to .tmp, update DB in transaction, rename key file
    std::string key_path = "data/storage.key";
    std::string tmp_key_path = key_path + ".tmp";
    SecureStorage::SaveKey(tmp_key_path, new_key);

    Execute("BEGIN", conn);
    try {
        for (const auto& row : encrypted_rows) {
            const char* paramValues[2];
            std::string id_str = std::to_string(row.first);
            paramValues[0] = row.second.data();
            paramValues[1] = id_str.c_str();
            int paramLengths[2] = { static_cast<int>(row.second.size()), 0 };
            int paramFormats[2] = { 1, 0 }; // blob binary, id text

            PGresultPtr upd(PQexecParams(conn,
                "UPDATE messages SET data = $1 WHERE id = $2",
                2, nullptr, paramValues, paramLengths, paramFormats, 0));
            if (PQresultStatus(upd.get()) != PGRES_COMMAND_OK) {
                throw std::runtime_error("Failed to update row " + id_str);
            }
        }
        Execute("COMMIT", conn);
    } catch (...) {
        Execute("ROLLBACK", conn);
        secure_storage_.SetKey(old_key);
        throw;
    }

    // 5. Atomic rename
    try {
        std::filesystem::rename(tmp_key_path, key_path);
    } catch (const std::exception& e) {
        Logger::Fatal("CRITICAL: DB re-encrypted but key rename failed! New key in " + tmp_key_path);
        throw;
    }

    Logger::Info("Database re-encryption complete with Atomic Key Rotation.");
}

// =============================================================================
// Imageboard: Helpers
// =============================================================================

std::string DBManager::GetBoardIdForThread(int64_t thread_id) {
    std::string tid = std::to_string(thread_id);
    PGresultPtr res = QueryParams("SELECT board_id FROM threads WHERE id = $1", {tid});
    if (PQntuples(res.get()) == 0) return "";
    return PQgetvalue(res.get(), 0, 0);
}

// =============================================================================
// Imageboard: Boards
// =============================================================================

json DBManager::GetAllBoards() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    json boards = json::array();

    PGresultPtr res = Query(
        "SELECT b.id, b.name, b.description, b.icon, b.nsfw,"
        "  (SELECT COUNT(*) FROM threads WHERE board_id = b.id) AS thread_count,"
        "  (SELECT COUNT(*) FROM posts WHERE board_id = b.id) AS post_count "
        "FROM boards b ORDER BY b.id");

    int rows = PQntuples(res.get());
    for (int i = 0; i < rows; ++i) {
        json board;
        board["id"]          = PQgetvalue(res.get(), i, 0);
        board["name"]        = PQgetvalue(res.get(), i, 1);
        board["description"] = PQgetvalue(res.get(), i, 2);
        board["icon"]        = PQgetvalue(res.get(), i, 3);
        board["nsfw"]        = std::string(PQgetvalue(res.get(), i, 4)) == "t";
        board["threadCount"] = std::stoi(PQgetvalue(res.get(), i, 5));
        board["postCount"]   = std::stoi(PQgetvalue(res.get(), i, 6));
        boards.push_back(board);
    }
    return boards;
}

json DBManager::GetBoard(const std::string& board_id) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    json board;

    PGresultPtr res = QueryParams(
        "SELECT b.id, b.name, b.description, b.icon, b.nsfw,"
        "  (SELECT COUNT(*) FROM threads WHERE board_id = b.id) AS thread_count,"
        "  (SELECT COUNT(*) FROM posts WHERE board_id = b.id) AS post_count "
        "FROM boards b WHERE b.id = $1", {board_id});

    if (PQntuples(res.get()) == 0) return board; // empty = not found

    board["id"]          = PQgetvalue(res.get(), 0, 0);
    board["name"]        = PQgetvalue(res.get(), 0, 1);
    board["description"] = PQgetvalue(res.get(), 0, 2);
    board["icon"]        = PQgetvalue(res.get(), 0, 3);
    board["nsfw"]        = std::string(PQgetvalue(res.get(), 0, 4)) == "t";
    board["threadCount"] = std::stoi(PQgetvalue(res.get(), 0, 5));
    board["postCount"]   = std::stoi(PQgetvalue(res.get(), 0, 6));
    return board;
}

// =============================================================================
// Imageboard: Threads
// =============================================================================

json DBManager::GetThreads(const std::string& board_id, int page, int limit, bool archived) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    json result;
    json threads = json::array();

    int offset = (page - 1) * limit;
    std::string archived_str = archived ? "true" : "false";
    std::string limit_str = std::to_string(limit);
    std::string offset_str = std::to_string(offset);

    // Count
    PGresultPtr count_res = QueryParams(
        "SELECT COUNT(*) FROM threads WHERE board_id = $1 AND archived = $2",
        {board_id, archived_str});
    int total = std::stoi(PQgetvalue(count_res.get(), 0, 0));

    // Threads
    PGresultPtr res = QueryParams(
        "SELECT t.id, t.board_id, t.subject, t.sticky, t.locked, t.archived, "
        "  t.created_at, t.last_bump, "
        "  (SELECT COUNT(*) - 1 FROM posts WHERE thread_id = t.id) AS reply_count, "
        "  (SELECT COUNT(*) FROM posts WHERE thread_id = t.id AND image_url IS NOT NULL AND image_url != '') AS image_count "
        "FROM threads t WHERE t.board_id = $1 AND t.archived = $2 "
        "ORDER BY t.sticky DESC, t.last_bump DESC "
        "LIMIT $3 OFFSET $4",
        {board_id, archived_str, limit_str, offset_str});

    int rows = PQntuples(res.get());
    for (int i = 0; i < rows; ++i) {
        json thread;
        std::string tid = PQgetvalue(res.get(), i, 0);
        thread["id"]         = std::stoll(tid);
        thread["boardId"]    = PQgetvalue(res.get(), i, 1);
        thread["subject"]    = PQgetvalue(res.get(), i, 2);
        thread["sticky"]     = std::string(PQgetvalue(res.get(), i, 3)) == "t";
        thread["locked"]     = std::string(PQgetvalue(res.get(), i, 4)) == "t";
        thread["archived"]   = std::string(PQgetvalue(res.get(), i, 5)) == "t";
        thread["createdAt"]  = PQgetvalue(res.get(), i, 6);
        thread["lastBump"]   = PQgetvalue(res.get(), i, 7);
        thread["replyCount"] = std::stoi(PQgetvalue(res.get(), i, 8));
        thread["imageCount"] = std::stoi(PQgetvalue(res.get(), i, 9));

        // Get OP post for this thread
        PGresultPtr op_res = QueryParams(
            "SELECT p.id, p.content, p.encrypted_content, p.is_encrypted, p.image_url, p.name, p.created_at, "
            "  COALESCE(CASE WHEN pr.subscription_tier IS NOT NULL AND pr.subscription_tier != '' AND pr.subscription_tier != 'none' AND (pr.subscription_expires_at IS NULL OR pr.subscription_expires_at > NOW()) THEN pr.subscription_tier ELSE '' END, '') AS active_sub_tier, "
            "  COALESCE(pr.custom_badge, '') AS custom_badge "
            "FROM posts p LEFT JOIN profiles pr ON p.author_hash = pr.pub_key_hash WHERE p.thread_id = $1 AND p.is_op = TRUE LIMIT 1", {tid});

        if (PQntuples(op_res.get()) > 0) {
            json op;
            op["id"]               = std::stoll(PQgetvalue(op_res.get(), 0, 0));
            op["content"]          = PQgetvalue(op_res.get(), 0, 1);
            op["encryptedContent"] = PQgetisnull(op_res.get(), 0, 2) ? nullptr : json(PQgetvalue(op_res.get(), 0, 2));
            op["isEncrypted"]      = std::string(PQgetvalue(op_res.get(), 0, 3)) == "t";
            op["imageUrl"]         = PQgetisnull(op_res.get(), 0, 4) ? nullptr : json(PQgetvalue(op_res.get(), 0, 4));
            op["name"]             = PQgetvalue(op_res.get(), 0, 5);
            op["createdAt"]        = PQgetvalue(op_res.get(), 0, 6);
            op["subscriptionTier"] = PQgetvalue(op_res.get(), 0, 7);
            op["customBadge"]      = PQgetvalue(op_res.get(), 0, 8);
            thread["op"] = op;
        }

        threads.push_back(thread);
    }

    result["threads"] = threads;
    result["pagination"] = {
        {"page", page}, {"limit", limit}, {"total", total},
        {"pages", total > 0 ? (total + limit - 1) / limit : 0}
    };
    return result;
}

json DBManager::GetThread(int64_t thread_id) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    json result;

    std::string tid = std::to_string(thread_id);
    PGresultPtr res = QueryParams(
        "SELECT id, board_id, subject, sticky, locked, archived, created_at, last_bump "
        "FROM threads WHERE id = $1", {tid});

    if (PQntuples(res.get()) == 0) return result; // empty = not found

    result["id"]        = std::stoll(PQgetvalue(res.get(), 0, 0));
    result["boardId"]   = PQgetvalue(res.get(), 0, 1);
    result["subject"]   = PQgetvalue(res.get(), 0, 2);
    result["sticky"]    = std::string(PQgetvalue(res.get(), 0, 3)) == "t";
    result["locked"]    = std::string(PQgetvalue(res.get(), 0, 4)) == "t";
    result["archived"]  = std::string(PQgetvalue(res.get(), 0, 5)) == "t";
    result["createdAt"] = PQgetvalue(res.get(), 0, 6);
    result["lastBump"]  = PQgetvalue(res.get(), 0, 7);

    // Get all posts
    PGresultPtr post_res = QueryParams(
        "SELECT p.id, p.content, p.encrypted_content, p.is_encrypted, p.image_url, p.name, "
        "  p.tripcode, p.sage, p.is_op, p.created_at, "
        "  COALESCE(CASE WHEN pr.subscription_tier IS NOT NULL AND pr.subscription_tier != '' AND pr.subscription_tier != 'none' AND (pr.subscription_expires_at IS NULL OR pr.subscription_expires_at > NOW()) THEN pr.subscription_tier ELSE '' END, '') AS active_sub_tier, "
        "  COALESCE(pr.custom_badge, '') AS custom_badge "
        "FROM posts p LEFT JOIN profiles pr ON p.author_hash = pr.pub_key_hash WHERE p.thread_id = $1 ORDER BY p.created_at ASC", {tid});

    json op = nullptr;
    json replies = json::array();
    int post_rows = PQntuples(post_res.get());

    for (int i = 0; i < post_rows; ++i) {
        json post;
        post["id"]               = std::stoll(PQgetvalue(post_res.get(), i, 0));
        post["content"]          = PQgetvalue(post_res.get(), i, 1);
        post["encryptedContent"] = PQgetisnull(post_res.get(), i, 2) ? nullptr : json(PQgetvalue(post_res.get(), i, 2));
        post["isEncrypted"]      = std::string(PQgetvalue(post_res.get(), i, 3)) == "t";
        post["imageUrl"]         = PQgetisnull(post_res.get(), i, 4) ? nullptr : json(PQgetvalue(post_res.get(), i, 4));
        post["name"]             = PQgetvalue(post_res.get(), i, 5);
        post["tripcode"]         = PQgetisnull(post_res.get(), i, 6) ? nullptr : json(PQgetvalue(post_res.get(), i, 6));
        post["sage"]             = std::string(PQgetvalue(post_res.get(), i, 7)) == "t";
        post["isOP"]             = std::string(PQgetvalue(post_res.get(), i, 8)) == "t";
        post["createdAt"]        = PQgetvalue(post_res.get(), i, 9);
        post["subscriptionTier"] = PQgetvalue(post_res.get(), i, 10);
        post["customBadge"]      = PQgetvalue(post_res.get(), i, 11);

        if (post["isOP"].get<bool>()) {
            op = post;
        } else {
            replies.push_back(post);
        }
    }

    result["op"]         = op;
    result["replies"]    = replies;
    result["replyCount"] = static_cast<int>(replies.size());
    return result;
}

json DBManager::CreateThread(const std::string& board_id, const std::string& subject,
                             const std::string& content, const std::string& name,
                             const std::string& image_url, const std::string& encrypted_content,
                             const std::string& author_hash) {
    ConnLease lease(*pool_);
    PGconn* conn = lease.get();
    if (!conn) throw std::runtime_error("CreateThread failed: database connection unavailable");

    const std::string author = ResolveExistingProfileHash(author_hash);
    std::string ban_reason;
    if (!author.empty() && IsProfileBanned(author, &ban_reason)) {
        throw std::runtime_error(ban_reason.empty() ? "This identity is banned from posting" : ban_reason);
    }

    std::string subj = subject.empty() ? "No Subject" : subject;
    PGresultPtr thread_res = QueryParams(
        "INSERT INTO threads (board_id, subject) VALUES ($1, $2) RETURNING id",
        {board_id, subj}, conn);
    int64_t thread_id = std::stoll(PQgetvalue(thread_res.get(), 0, 0));

    // Insert OP post
    bool is_encrypted = !encrypted_content.empty();
    std::string post_content = is_encrypted ? "[Encrypted Post]" : content;
    std::string poster_name = name.empty() ? "Anonymous" : name;
    std::string enc_flag = is_encrypted ? "true" : "false";

    // Use NULLs properly for optional fields
    std::vector<const char*> values = {
        std::to_string(thread_id).c_str(), // will be dangling — need to store
    };

    // Build it properly with stored strings
    std::string tid_str = std::to_string(thread_id);
    const char* pv[9];
    pv[0] = tid_str.c_str();
    pv[1] = board_id.c_str();
    pv[2] = post_content.c_str();
    pv[3] = is_encrypted ? encrypted_content.c_str() : nullptr;
    pv[4] = enc_flag.c_str();
    pv[5] = image_url.empty() ? nullptr : image_url.c_str();
    pv[6] = poster_name.c_str();
    pv[7] = author.empty() ? nullptr : author.c_str();
    pv[8] = "true";

    PGresultPtr post_res(PQexecParams(conn,
        "INSERT INTO posts (thread_id, board_id, content, encrypted_content, is_encrypted, image_url, name, author_hash, is_op) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, COALESCE($8, ''), $9) RETURNING id",
        9, nullptr, pv, nullptr, nullptr, 0));

    if (PQresultStatus(post_res.get()) != PGRES_TUPLES_OK) {
        throw std::runtime_error("Failed to insert OP post: " + std::string(PQerrorMessage(conn)));
    }
    int64_t post_id = std::stoll(PQgetvalue(post_res.get(), 0, 0));

    json result;
    result["id"]      = thread_id;
    result["boardId"] = board_id;
    result["subject"] = subj;
    result["op"] = {
        {"id", post_id}, {"content", post_content},
        {"name", poster_name},
        {"imageUrl", image_url.empty() ? nullptr : json(image_url)},
        {"isEncrypted", is_encrypted}
    };
    result["replyCount"] = 0;
    result["sticky"]     = false;
    result["locked"]     = false;
    result["archived"]   = false;
    return result;
}

void DBManager::ArchiveThread(int64_t thread_id) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    std::string tid = std::to_string(thread_id);
    QueryParams("UPDATE threads SET archived = TRUE WHERE id = $1", {tid});
}

// =============================================================================
// Imageboard: Posts
// =============================================================================

json DBManager::CreatePost(int64_t thread_id, const std::string& content,
                           const std::string& name, const std::string& image_url,
                           const std::string& encrypted_content, bool sage,
                           const std::string& author_hash) {
    ConnLease lease(*pool_);
    PGconn* conn = lease.get();
    if (!conn) throw std::runtime_error("CreatePost failed: database connection unavailable");

    std::string bid = GetBoardIdForThread(thread_id);
    if (bid.empty()) throw std::runtime_error("Thread not found");
    const std::string author = ResolveExistingProfileHash(author_hash);
    std::string ban_reason;
    if (!author.empty() && IsProfileBanned(author, &ban_reason)) {
        throw std::runtime_error(ban_reason.empty() ? "This identity is banned from posting" : ban_reason);
    }

    // Check locked
    std::string tid = std::to_string(thread_id);
    PGresultPtr lock_res = QueryParams("SELECT locked FROM threads WHERE id = $1", {tid}, conn);
    if (PQntuples(lock_res.get()) > 0 && std::string(PQgetvalue(lock_res.get(), 0, 0)) == "t") {
        throw std::runtime_error("Thread is locked");
    }

    bool is_encrypted = !encrypted_content.empty();
    std::string post_content = is_encrypted ? "[Encrypted Post]" : content;
    std::string poster_name = name.empty() ? "Anonymous" : name;
    std::string enc_flag = is_encrypted ? "true" : "false";
    std::string sage_flag = sage ? "true" : "false";

    const char* pv[9];
    pv[0] = tid.c_str();
    pv[1] = bid.c_str();
    pv[2] = post_content.c_str();
    pv[3] = is_encrypted ? encrypted_content.c_str() : nullptr;
    pv[4] = enc_flag.c_str();
    pv[5] = image_url.empty() ? nullptr : image_url.c_str();
    pv[6] = poster_name.c_str();
    pv[7] = sage_flag.c_str();
    pv[8] = author.empty() ? nullptr : author.c_str();

    PGresultPtr res(PQexecParams(conn,
        "INSERT INTO posts (thread_id, board_id, content, encrypted_content, is_encrypted, image_url, name, sage, author_hash) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, COALESCE($9, '')) RETURNING id",
        9, nullptr, pv, nullptr, nullptr, 0));

    if (PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
        throw std::runtime_error("Failed to insert post: " + std::string(PQerrorMessage(conn)));
    }
    int64_t post_id = std::stoll(PQgetvalue(res.get(), 0, 0));

    // Bump unless sage
    if (!sage) {
        QueryParams("UPDATE threads SET last_bump = NOW() WHERE id = $1", {tid});
    }

    json result;
    result["id"]          = post_id;
    result["threadId"]    = thread_id;
    result["boardId"]     = bid;
    result["content"]     = post_content;
    result["name"]        = poster_name;
    result["isEncrypted"] = is_encrypted;
    result["sage"]        = sage;
    return result;
}

// =============================================================================
// Imageboard: Stats
// =============================================================================

json DBManager::GetStats() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    json result;

    auto count = [&](const std::string& table) -> int {
        PGresultPtr res = Query("SELECT COUNT(*) FROM " + table);
        return std::stoi(PQgetvalue(res.get(), 0, 0));
    };

    result["boards"]  = count("boards");
    result["threads"] = count("threads");
    result["posts"]   = count("posts");
    result["users"]   = count("profiles");
    PGresultPtr named_users_res = Query("SELECT COUNT(*) FROM profiles WHERE COALESCE(username, '') <> ''");
    result["namedUsers"] = std::stoi(PQgetvalue(named_users_res.get(), 0, 0));

    PGresultPtr enc_res = Query("SELECT COUNT(*) FROM posts WHERE is_encrypted = TRUE");
    result["encrypted"] = std::stoi(PQgetvalue(enc_res.get(), 0, 0));

    return result;
}

// =============================================================================
// V4 Social Layer
// =============================================================================

json DBManager::GetProfile(const std::string& pub_key_hash) {
    if (pub_key_hash.empty()) return json({});
    const std::string resolved_hash = ResolveProfileHash(pub_key_hash);
    if (resolved_hash.empty()) return json({});
    json profile = json::object();
    
    // Get basic info
    {
        PGresultPtr res = QueryParams(
            "SELECT username, last_active, COALESCE(pqc_kem_public_key, ''), "
            "       COALESCE(pqc_kem_scheme, 'ML-KEM-1024'), COALESCE(identity_public_key, ''), "
            "       COALESCE(pqc_identity_public_key, ''), COALESCE(pqc_identity_scheme, ''), "
            "       COALESCE(identity_binding_payload, ''), COALESCE(identity_binding_signature, ''), "
            "       COALESCE(role, 'user'), COALESCE(role_assigned_by, ''), "
            "       COALESCE((EXTRACT(EPOCH FROM founder_claimed_at) * 1000)::BIGINT::TEXT, ''), "
            "       CASE WHEN COALESCE(recovery_bundle_ciphertext, '') <> '' THEN 'true' ELSE 'false' END, "
            "       COALESCE((EXTRACT(EPOCH FROM role_assigned_at) * 1000)::BIGINT::TEXT, ''), "
            "       COALESCE(custom_badge, ''), COALESCE(unlocked_tags, '') "
            "FROM profiles WHERE pub_key_hash = $1",
            {resolved_hash}
        );
        if (PQntuples(res.get()) > 0) {
            profile["username"] = PQgetvalue(res.get(), 0, 0);
            profile["last_active"] = PQgetvalue(res.get(), 0, 1);
            profile["pqc_kem_public_key"] = PQgetvalue(res.get(), 0, 2);
            profile["pqc_kem_scheme"] = PQgetvalue(res.get(), 0, 3);
            profile["identity_public_key"] = PQgetvalue(res.get(), 0, 4);
            profile["pqc_identity_public_key"] = PQgetvalue(res.get(), 0, 5);
            profile["pqc_identity_scheme"] = PQgetvalue(res.get(), 0, 6);
            profile["identity_binding_payload"] = PQgetvalue(res.get(), 0, 7);
            profile["identity_binding_signature"] = PQgetvalue(res.get(), 0, 8);
            profile["role"] = PQgetvalue(res.get(), 0, 9);
            profile["role_assigned_by"] = PQgetvalue(res.get(), 0, 10);
            profile["founder_claimed_at"] = PQgetvalue(res.get(), 0, 11);
            profile["recovery_configured"] = std::string(PQgetvalue(res.get(), 0, 12)) == "true";
            profile["role_assigned_at"] = PQgetvalue(res.get(), 0, 13);
            profile["custom_badge"] = PQgetvalue(res.get(), 0, 14);
            profile["unlocked_tags"] = PQgetvalue(res.get(), 0, 15);
            std::string assigned_by = profile["role_assigned_by"].get<std::string>();
            profile["role_assigned_by_username"] = assigned_by.empty() ? "" : ResolveProfileUsername(assigned_by);
            profile["role_badge"] = GetRoleBadge(resolved_hash, profile["role"].get<std::string>());
        } else {
            profile["username"] = "Anonymous";
            profile["last_active"] = "";
            profile["pqc_kem_public_key"] = "";
            profile["pqc_kem_scheme"] = "ML-KEM-1024";
            profile["identity_public_key"] = "";
            profile["pqc_identity_public_key"] = "";
            profile["pqc_identity_scheme"] = "";
            profile["identity_binding_payload"] = "";
            profile["identity_binding_signature"] = "";
            profile["role"] = "user";
            profile["role_assigned_by"] = "";
            profile["role_assigned_by_username"] = "";
            profile["founder_claimed_at"] = "";
            profile["recovery_configured"] = false;
            profile["role_assigned_at"] = "";
            profile["role_badge"] = "USER";
            profile["custom_badge"] = "";
            profile["unlocked_tags"] = "";
        }
    }
    profile["banned"] = IsProfileBanned(resolved_hash);
    
    profile["pub_key_hash"] = resolved_hash;
    
    // Get recent posts
    {
        PGresultPtr res = QueryParams(
            "SELECT p.id, p.thread_id, p.board_id, p.content, p.image_url, p.created_at, p.name "
            "FROM posts p "
            "WHERE COALESCE(p.author_hash, '') = $1 OR COALESCE(p.tripcode, '') = $1 OR p.name = $2 OR p.name LIKE $3 "
            "ORDER BY p.created_at DESC LIMIT 10",
            {resolved_hash, "Identity!" + resolved_hash, "Identity!%#" + resolved_hash}
        );
        json posts = json::array();
        int rows = PQntuples(res.get());
        for (int i = 0; i < rows; ++i) {
            json post = {
                {"id", std::stoll(PQgetvalue(res.get(), i, 0))},
                {"thread_id", std::stoll(PQgetvalue(res.get(), i, 1))},
                {"board_id", PQgetvalue(res.get(), i, 2)},
                {"content", PQgetvalue(res.get(), i, 3)},
                {"image_url", PQgetvalue(res.get(), i, 4)},
                {"created_at", PQgetvalue(res.get(), i, 5)},
                {"name", PQgetvalue(res.get(), i, 6)}
            };
            posts.push_back(post);
        }
        profile["recent_posts"] = posts;
    }
    
    return profile;
}

void DBManager::UpdateProfile(const std::string& pub_key_hash, const std::string& username,
                             const std::string& pqc_kem_public_key,
                             const std::string& identity_public_key,
                             const std::string& pqc_identity_public_key,
                             const std::string& pqc_identity_scheme,
                             const std::string& identity_binding_payload,
                             const std::string& identity_binding_signature,
                             const std::string& recovery_lookup_hash,
                             const std::string& recovery_bundle_ciphertext,
                             const std::string& recovery_bundle_iv) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    std::string normalized_username = "";
    if (!username.empty()) {
        normalized_username = NormalizeUsernameStorage(username);
        if (normalized_username.empty()) {
            throw std::runtime_error("Username may only contain letters, numbers, underscores, and hyphens");
        }
        if (normalized_username.size() < 3 || normalized_username.size() > 24) {
            throw std::runtime_error("Username must be between 3 and 24 characters");
        }
        if (IsReservedUsername(normalized_username)) {
            throw std::runtime_error("That username is reserved");
        }
        PGresultPtr existing = QueryParams(
            "SELECT pub_key_hash FROM profiles WHERE LOWER(username) = LOWER($1) AND pub_key_hash <> $2 LIMIT 1",
            {normalized_username, pub_key_hash}
        );
        if (PQntuples(existing.get()) > 0) {
            throw std::runtime_error("Username is already taken");
        }
    }

    const bool has_recovery_fields = !recovery_lookup_hash.empty()
        || !recovery_bundle_ciphertext.empty()
        || !recovery_bundle_iv.empty();
    if (has_recovery_fields && (recovery_lookup_hash.empty() || recovery_bundle_ciphertext.empty() || recovery_bundle_iv.empty())) {
        throw std::runtime_error("Recovery bundle payload is incomplete");
    }

    QueryParams(
        "INSERT INTO profiles ("
        "  pub_key_hash, username, pqc_kem_public_key, pqc_kem_scheme, identity_public_key, "
        "  pqc_identity_public_key, pqc_identity_scheme, identity_binding_payload, identity_binding_signature, "
        "  recovery_lookup_hash, recovery_bundle_ciphertext, recovery_bundle_iv, last_active"
        ") VALUES ($1, $2, $3, 'ML-KEM-1024', $4, $5, $6, $7, $8, $9, $10, $11, NOW()) "
        "ON CONFLICT (pub_key_hash) DO UPDATE SET "
        "username = CASE WHEN EXCLUDED.username <> '' THEN EXCLUDED.username ELSE profiles.username END, "
        "pqc_kem_public_key = CASE WHEN EXCLUDED.pqc_kem_public_key <> '' THEN EXCLUDED.pqc_kem_public_key ELSE profiles.pqc_kem_public_key END, "
        "identity_public_key = CASE WHEN EXCLUDED.identity_public_key <> '' THEN EXCLUDED.identity_public_key ELSE profiles.identity_public_key END, "
        "pqc_identity_public_key = CASE WHEN EXCLUDED.pqc_identity_public_key <> '' THEN EXCLUDED.pqc_identity_public_key ELSE profiles.pqc_identity_public_key END, "
        "pqc_identity_scheme = CASE WHEN EXCLUDED.pqc_identity_scheme <> '' THEN EXCLUDED.pqc_identity_scheme ELSE profiles.pqc_identity_scheme END, "
        "identity_binding_payload = CASE WHEN EXCLUDED.identity_binding_payload <> '' THEN EXCLUDED.identity_binding_payload ELSE profiles.identity_binding_payload END, "
        "identity_binding_signature = CASE WHEN EXCLUDED.identity_binding_signature <> '' THEN EXCLUDED.identity_binding_signature ELSE profiles.identity_binding_signature END, "
        "recovery_lookup_hash = CASE WHEN EXCLUDED.recovery_lookup_hash <> '' THEN EXCLUDED.recovery_lookup_hash ELSE profiles.recovery_lookup_hash END, "
        "recovery_bundle_ciphertext = CASE WHEN EXCLUDED.recovery_bundle_ciphertext <> '' THEN EXCLUDED.recovery_bundle_ciphertext ELSE profiles.recovery_bundle_ciphertext END, "
        "recovery_bundle_iv = CASE WHEN EXCLUDED.recovery_bundle_iv <> '' THEN EXCLUDED.recovery_bundle_iv ELSE profiles.recovery_bundle_iv END, "
        "recovery_bundle_updated_at = CASE WHEN EXCLUDED.recovery_lookup_hash <> '' THEN NOW() ELSE profiles.recovery_bundle_updated_at END, "
        "pqc_kem_scheme = CASE WHEN EXCLUDED.pqc_kem_public_key <> '' THEN 'ML-KEM-1024' ELSE profiles.pqc_kem_scheme END, "
        "last_active = NOW()",
        {
            pub_key_hash,
            normalized_username,
            pqc_kem_public_key,
            identity_public_key,
            pqc_identity_public_key,
            pqc_identity_scheme,
            identity_binding_payload,
            identity_binding_signature,
            recovery_lookup_hash,
            recovery_bundle_ciphertext,
            recovery_bundle_iv
        }
    );
}

json DBManager::GetRecoveryBundle(const std::string& recovery_lookup_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string trimmed_lookup = TrimCopy(recovery_lookup_hash);
    if (trimmed_lookup.empty()) {
        return {{"error", "Recovery lookup hash required"}};
    }

    PGresultPtr res = QueryParams(
        "SELECT pub_key_hash, COALESCE(username, ''), COALESCE(recovery_bundle_ciphertext, ''), COALESCE(recovery_bundle_iv, '') "
        "FROM profiles WHERE recovery_lookup_hash = $1 LIMIT 1",
        {trimmed_lookup}
    );
    if (PQntuples(res.get()) == 0) {
        return {{"error", "Recovery bundle not found"}};
    }

    const std::string ciphertext = PQgetvalue(res.get(), 0, 2);
    const std::string iv = PQgetvalue(res.get(), 0, 3);
    if (ciphertext.empty() || iv.empty()) {
        return {{"error", "Recovery bundle not configured"}};
    }

    return {
        {"pub_key_hash", PQgetvalue(res.get(), 0, 0)},
        {"username", PQgetvalue(res.get(), 0, 1)},
        {"recovery_bundle_ciphertext", ciphertext},
        {"recovery_bundle_iv", iv}
    };
}

json DBManager::ClaimFounderRole(const std::string& pub_key_hash, const std::string& founder_session_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string resolved_hash = ResolveProfileHash(pub_key_hash);
    if (resolved_hash.empty()) {
        return {{"error", "Profile hash required"}};
    }
    if (founder_session_hash.empty()) {
        return {{"error", "Founder session hash required"}};
    }

    PGresultPtr founder_res = Query(
        "SELECT pub_key_hash FROM profiles WHERE role = 'founder' LIMIT 1"
    );
    if (PQntuples(founder_res.get()) > 0) {
        return {{"error", "Founder role has already been claimed. Restore the founder identity vault instead."}};
    }

    QueryParams(
        "INSERT INTO profiles (pub_key_hash, username, last_active) "
        "VALUES ($1, '', NOW()) "
        "ON CONFLICT (pub_key_hash) DO NOTHING",
        {resolved_hash}
    );

    QueryParams(
        "UPDATE profiles SET "
        "role = 'founder', "
        "role_assigned_by = $1, "
        "role_assigned_at = NOW(), "
        "founder_claimed_at = COALESCE(founder_claimed_at, NOW()), "
        "founder_session_hash = $2, "
        "last_active = NOW() "
        "WHERE pub_key_hash = $1",
        {resolved_hash, founder_session_hash}
    );

    return {
        {"status", "ok"},
        {"pub_key_hash", resolved_hash},
        {"role", "founder"}
    };
}

json DBManager::AdminLogin(const std::string& actor_hash, const std::string& founder_token, std::string& out_session_id) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty()) {
        return {{"error", "Actor profile hash required"}};
    }
    if (founder_token.empty()) {
        return {{"error", "Founder token required"}};
    }

    PGresultPtr res = QueryParams(
        "SELECT role, founder_session_hash FROM profiles WHERE pub_key_hash = $1 LIMIT 1",
        {actor}
    );
    if (PQntuples(res.get()) == 0) {
        return {{"error", "Actor profile not found"}};
    }

    std::string role = PQgetvalue(res.get(), 0, 0);
    std::string founder_session_hash = PQgetvalue(res.get(), 0, 1);

    if (role != "founder") {
        return {{"error", "Actor is not the founder"}};
    }

    std::string hashed_token = sha256_hex(founder_token);
    if (hashed_token != founder_session_hash) {
        return {{"error", "Invalid founder token"}};
    }

    std::string session_id = GenerateRandomHex(32);
    std::string hashed_session = sha256_hex(session_id);

    QueryParams(
        "UPDATE profiles SET active_session_hash = $2, session_expires_at = NOW() + INTERVAL '24 hours' WHERE pub_key_hash = $1",
        {actor, hashed_session}
    );

    out_session_id = session_id;
    return {{"status", "success"}};
}

json DBManager::SetProfileRole(const std::string& actor_hash, const std::string& founder_session_hash,
                               const std::string& target_hash, const std::string& role) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    const std::string target = ResolveProfileHash(target_hash);
    const std::string normalized_role = NormalizeRole(role);

    if (actor.empty() || target.empty()) {
        return {{"error", "Actor and target are required"}};
    }
    if (normalized_role == "founder") {
        return {{"error", "Founder role cannot be assigned from the UI"}};
    }
    if (actor == target) {
        return {{"error", "Founder cannot change their own role here"}};
    }

    PGresultPtr actor_res = QueryParams(
        "SELECT COALESCE(role, 'user'), COALESCE(founder_session_hash, '') "
        "FROM profiles WHERE pub_key_hash = $1",
        {actor}
    );
    if (PQntuples(actor_res.get()) == 0) {
        return {{"error", "Founder profile not found"}};
    }

    const std::string actor_role = NormalizeRole(PQgetvalue(actor_res.get(), 0, 0));
    const std::string stored_founder_session_hash = PQgetvalue(actor_res.get(), 0, 1);
    if (actor_role != "founder" || stored_founder_session_hash.empty() || stored_founder_session_hash != founder_session_hash) {
        return {{"error", "Founder authorization failed"}};
    }

    PGresultPtr target_res = QueryParams(
        "SELECT COALESCE(role, 'user') FROM profiles WHERE pub_key_hash = $1",
        {target}
    );
    const std::string previous_role = PQntuples(target_res.get()) > 0
        ? NormalizeRole(PQgetvalue(target_res.get(), 0, 0))
        : "user";
    if (previous_role == "founder") {
        return {{"error", "Founder role cannot be modified"}};
    }

    QueryParams(
        "INSERT INTO profiles (pub_key_hash, username, last_active) "
        "VALUES ($1, '', NOW()) "
        "ON CONFLICT (pub_key_hash) DO NOTHING",
        {target}
    );

    QueryParams(
        "UPDATE profiles SET "
        "role = $1, "
        "role_assigned_by = $2, "
        "role_assigned_at = NOW(), "
        "last_active = NOW() "
        "WHERE pub_key_hash = $3",
        {normalized_role, actor, target}
    );
    if (normalized_role == "moderator") {
        EnsureModeratorBadge(target);
    }
    CreateNotification(target, actor, "role_change", "Role updated",
                       "Your role is now " + normalized_role + ".",
                       "/u/" + target);
    if (previous_role != normalized_role) {
        const std::string target_label = ResolveProfileUsername(target);
        CreateModerationEvent(
            actor,
            normalized_role == "moderator" ? "grant_moderator" : "remove_moderator",
            normalized_role == "moderator"
                ? "Granted moderator role to " + target_label + "."
                : "Removed moderator role from " + target_label + ".",
            target
        );
    }

    return {
        {"status", "ok"},
        {"target_hash", target},
        {"role", normalized_role},
        {"role_assigned_by", actor}
    };
}

json DBManager::GiftUser(const std::string& actor_hash, const std::string& founder_session_cookie,
                         const std::string& target_hash, const std::string& gift_type,
                         const std::string& gift_value, int duration_days) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_cookie, true)) {
        return {{"error", "Founder authorization failed"}};
    }
    const std::string target = ResolveExistingProfileHash(target_hash);
    if (target.empty()) {
        return {{"error", "Target profile not found"}};
    }

    const std::string normalized_gift_type = TrimCopy(gift_type);
    const std::string normalized_gift_value = TrimCopy(gift_value);

    if (normalized_gift_type == "tag") {
        if (normalized_gift_value.empty() || normalized_gift_value == "clear" || normalized_gift_value == "none") {
            QueryParams(
                "UPDATE profiles SET custom_badge = '' WHERE pub_key_hash = $1",
                {target}
            );
            CreateNotification(target, actor, "gift_received", "Badge Cleared",
                               "The founder cleared your custom badge.", "/u/" + target);
            CreateModerationEvent(actor, "clear_tag", "Cleared custom badge from " + target, target);
        } else {
            AddProfileTag(target, normalized_gift_value);
            CreateNotification(target, actor, "gift_received", "Special Gift Received",
                               "The founder gifted you the tag: " + normalized_gift_value, "/u/" + target);
            CreateModerationEvent(actor, "gift_tag", "Gifted custom badge: " + normalized_gift_value + " to " + target, target);
        }
        return {{"status", "success"}};
    } else if (normalized_gift_type == "subscription") {
        UpdateProfileSubscription(target, normalized_gift_value, duration_days);
        CreateNotification(target, actor, "gift_received", "Special Gift Received",
                           "The founder gifted you a " + normalized_gift_value + " subscription!", "/u/" + target);
        CreateModerationEvent(actor, "gift_subscription", "Gifted subscription: " + normalized_gift_value + " to " + target, target);
        return {{"status", "success"}};
    } else {
        return {{"error", "Invalid gift type"}};
    }
}

json DBManager::InteractPost(int64_t post_id, const std::string& pub_key_hash, int type) {
    std::string type_str = std::to_string(type);
    std::string post_id_str = std::to_string(post_id);
    
    // Insert or update interaction
    QueryParams(
        "INSERT INTO interactions (post_id, pub_key_hash, type) VALUES ($1, $2, $3) "
        "ON CONFLICT (post_id, pub_key_hash) DO UPDATE SET type = EXCLUDED.type",
        {post_id_str, pub_key_hash, type_str}
    );
    
    // Get new totals
    PGresultPtr res = QueryParams(
        "SELECT "
        "  COALESCE(SUM(CASE WHEN type = 1 THEN 1 ELSE 0 END), 0) as likes, "
        "  COALESCE(SUM(CASE WHEN type = -1 THEN 1 ELSE 0 END), 0) as dislikes "
        "FROM interactions WHERE post_id = $1",
        {post_id_str}
    );
    
    json result = {
        {"status", "ok"},
        {"likes", std::stoll(PQgetvalue(res.get(), 0, 0))},
        {"dislikes", std::stoll(PQgetvalue(res.get(), 0, 1))}
    };
    return result;
}

json DBManager::SendFriendRequest(const std::string& sender_hash, const std::string& receiver_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string s = ResolveExistingProfileHash(sender_hash);
    const std::string r = ResolveExistingProfileHash(receiver_hash);
    if (s.empty() || r.empty()) {
        return {{"error", "Both users must publish a profile before sending friend requests"}};
    }
    if (s == r) {
        return {{"error", "Cannot add yourself"}};
    }
    std::string ban_reason;
    if (IsProfileBanned(s, &ban_reason)) {
        return {{"error", ban_reason.empty() ? "This identity is banned from sending friend requests" : ban_reason}};
    }

    std::string blocker;
    if (IsBlockedEitherDirection(s, r, &blocker)) {
        return {{"error", blocker == s ? "You have blocked this user" : "This user has blocked you"}};
    }
    if (HasAcceptedFriendship(s, r)) {
        return {{"status", "already_friends"}};
    }

    PGresultPtr reverse = QueryParams(
        "SELECT status FROM friend_requests WHERE sender_hash = $1 AND receiver_hash = $2 LIMIT 1",
        {r, s}
    );
    if (PQntuples(reverse.get()) > 0 && std::string(PQgetvalue(reverse.get(), 0, 0)) == "0") {
        QueryParams(
            "UPDATE friend_requests SET status = 1 WHERE "
            "(sender_hash = $1 AND receiver_hash = $2) OR (sender_hash = $2 AND receiver_hash = $1)",
            {s, r}
        );
        CreateNotification(r, s, "friend_accept", "Friend request accepted",
                           ResolveProfileUsername(s) + " accepted your friend request.",
                           "/u/" + s);
        return {{"status", "accepted"}, {"auto_accepted", true}};
    }

    PGresultPtr existing = QueryParams(
        "SELECT status FROM friend_requests WHERE sender_hash = $1 AND receiver_hash = $2 LIMIT 1",
        {s, r}
    );
    if (PQntuples(existing.get()) > 0 && std::string(PQgetvalue(existing.get(), 0, 0)) == "0") {
        return {{"status", "pending"}};
    }

    QueryParams(
        "INSERT INTO friend_requests (sender_hash, receiver_hash, status, created_at) VALUES ($1, $2, 0, NOW()) "
        "ON CONFLICT (sender_hash, receiver_hash) DO UPDATE SET status = 0, created_at = NOW()",
        {s, r}
    );
    CreateNotification(r, s, "friend_request", "New friend request",
                       ResolveProfileUsername(s) + " sent you a friend request.",
                       "/u/" + s);
    return {{"status", "pending"}};
}

json DBManager::AcceptFriendRequest(const std::string& sender_hash, const std::string& receiver_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string s = ResolveExistingProfileHash(sender_hash);
    const std::string r = ResolveExistingProfileHash(receiver_hash);
    if (s.empty() || r.empty()) {
        return {{"error", "Both users must publish a profile before accepting friend requests"}};
    }

    PGresultPtr pending = QueryParams(
        "SELECT 1 FROM friend_requests WHERE sender_hash = $1 AND receiver_hash = $2 AND status = 0 LIMIT 1",
        {s, r}
    );
    if (PQntuples(pending.get()) == 0) {
        return {{"error", "No pending request from that user"}};
    }

    QueryParams(
        "UPDATE friend_requests SET status = 1 WHERE "
        "(sender_hash = $1 AND receiver_hash = $2) OR (sender_hash = $2 AND receiver_hash = $1)",
        {s, r}
    );
    CreateNotification(s, r, "friend_accept", "Friend request accepted",
                       ResolveProfileUsername(r) + " accepted your friend request.",
                       "/u/" + r);
    return {{"status", "accepted"}};
}

json DBManager::RejectFriendRequest(const std::string& sender_hash, const std::string& receiver_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string s = ResolveExistingProfileHash(sender_hash);
    const std::string r = ResolveExistingProfileHash(receiver_hash);
    if (s.empty() || r.empty()) {
        return {{"error", "Both users must publish a profile before rejecting friend requests"}};
    }

    PGresultPtr pending = QueryParams(
        "SELECT 1 FROM friend_requests WHERE sender_hash = $1 AND receiver_hash = $2 AND status = 0 LIMIT 1",
        {s, r}
    );
    if (PQntuples(pending.get()) == 0) {
        return {{"error", "No pending request from that user"}};
    }

    QueryParams(
        "UPDATE friend_requests SET status = 2 WHERE sender_hash = $1 AND receiver_hash = $2",
        {s, r}
    );
    CreateNotification(s, r, "friend_reject", "Friend request declined",
                       ResolveProfileUsername(r) + " declined your friend request.",
                       "/u/" + r);
    return {{"status", "rejected"}};
}

json DBManager::CancelFriendRequest(const std::string& sender_hash, const std::string& receiver_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string s = ResolveExistingProfileHash(sender_hash);
    const std::string r = ResolveExistingProfileHash(receiver_hash);
    if (s.empty() || r.empty()) {
        return {{"error", "Both users must publish a profile before canceling friend requests"}};
    }

    QueryParams(
        "UPDATE friend_requests SET status = 3 WHERE sender_hash = $1 AND receiver_hash = $2 AND status = 0",
        {s, r}
    );
    return {{"status", "canceled"}};
}

json DBManager::RemoveFriend(const std::string& user_hash, const std::string& peer_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string user = ResolveExistingProfileHash(user_hash);
    const std::string peer = ResolveExistingProfileHash(peer_hash);
    if (user.empty() || peer.empty()) {
        return {{"error", "Both users must publish a profile before removing friends"}};
    }

    QueryParams(
        "UPDATE friend_requests SET status = 4 WHERE status = 1 AND ("
        "(sender_hash = $1 AND receiver_hash = $2) OR (sender_hash = $2 AND receiver_hash = $1))",
        {user, peer}
    );
    return {{"status", "removed"}};
}

json DBManager::GetFriends(const std::string& pub_key_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string resolved_hash = ResolveExistingProfileHash(pub_key_hash);
    if (resolved_hash.empty()) {
        return {
            {"friends", json::array()},
            {"pending_received", json::array()},
            {"pending_sent", json::array()},
            {"blocked", json::array()}
        };
    }

    auto push_profile_ref = [&](json& target, const std::string& hash) {
        target.push_back({
            {"hash", hash},
            {"username", ResolveProfileUsername(hash)}
        });
    };

    PGresultPtr accepted = QueryParams(
        "SELECT receiver_hash FROM friend_requests WHERE sender_hash = $1 AND status = 1 "
        "UNION "
        "SELECT sender_hash FROM friend_requests WHERE receiver_hash = $1 AND status = 1",
        {resolved_hash}
    );
    json friends = json::array();
    for (int i = 0; i < PQntuples(accepted.get()); ++i) {
        push_profile_ref(friends, PQgetvalue(accepted.get(), i, 0));
    }

    PGresultPtr pending_received = QueryParams(
        "SELECT sender_hash FROM friend_requests WHERE receiver_hash = $1 AND status = 0 ORDER BY created_at DESC",
        {resolved_hash}
    );
    json incoming = json::array();
    for (int i = 0; i < PQntuples(pending_received.get()); ++i) {
        push_profile_ref(incoming, PQgetvalue(pending_received.get(), i, 0));
    }

    PGresultPtr pending_sent = QueryParams(
        "SELECT receiver_hash FROM friend_requests WHERE sender_hash = $1 AND status = 0 ORDER BY created_at DESC",
        {resolved_hash}
    );
    json outgoing = json::array();
    for (int i = 0; i < PQntuples(pending_sent.get()); ++i) {
        push_profile_ref(outgoing, PQgetvalue(pending_sent.get(), i, 0));
    }

    PGresultPtr blocked_res = QueryParams(
        "SELECT blocked_hash FROM blocks WHERE blocker_hash = $1 ORDER BY created_at DESC",
        {resolved_hash}
    );
    json blocked = json::array();
    for (int i = 0; i < PQntuples(blocked_res.get()); ++i) {
        push_profile_ref(blocked, PQgetvalue(blocked_res.get(), i, 0));
    }

    return {
        {"friends", friends},
        {"pending_received", incoming},
        {"pending_sent", outgoing},
        {"blocked", blocked}
    };
}

json DBManager::BlockUser(const std::string& blocker_hash, const std::string& blocked_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string blocker = ResolveExistingProfileHash(blocker_hash);
    const std::string blocked = ResolveExistingProfileHash(blocked_hash);
    if (blocker.empty() || blocked.empty()) {
        return {{"error", "Both users must publish a profile before blocking"}};
    }
    if (blocker == blocked) {
        return {{"error", "Cannot block yourself"}};
    }

    QueryParams(
        "INSERT INTO blocks (blocker_hash, blocked_hash) VALUES ($1, $2) ON CONFLICT DO NOTHING",
        {blocker, blocked}
    );
    QueryParams(
        "UPDATE friend_requests SET status = 4 WHERE (sender_hash = $1 AND receiver_hash = $2) OR (sender_hash = $2 AND receiver_hash = $1)",
        {blocker, blocked}
    );
    QueryParams(
        "INSERT INTO message_requests (requester_hash, recipient_hash, status, created_at, updated_at, last_message_at) "
        "VALUES ($1, $2, 'blocked', NOW(), NOW(), NOW()) "
        "ON CONFLICT (requester_hash, recipient_hash) DO UPDATE SET status = 'blocked', updated_at = NOW(), last_message_at = NOW()",
        {blocker, blocked}
    );
    QueryParams(
        "INSERT INTO message_requests (requester_hash, recipient_hash, status, created_at, updated_at, last_message_at) "
        "VALUES ($1, $2, 'blocked', NOW(), NOW(), NOW()) "
        "ON CONFLICT (requester_hash, recipient_hash) DO UPDATE SET status = 'blocked', updated_at = NOW(), last_message_at = NOW()",
        {blocked, blocker}
    );
    return {{"status", "blocked"}};
}

json DBManager::UnblockUser(const std::string& blocker_hash, const std::string& blocked_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string blocker = ResolveExistingProfileHash(blocker_hash);
    const std::string blocked = ResolveExistingProfileHash(blocked_hash);
    if (blocker.empty() || blocked.empty()) {
        return {{"error", "Both users must publish a profile before unblocking"}};
    }

    QueryParams(
        "DELETE FROM blocks WHERE blocker_hash = $1 AND blocked_hash = $2",
        {blocker, blocked}
    );
    QueryParams(
        "UPDATE message_requests SET status = 'declined', updated_at = NOW() "
        "WHERE ((requester_hash = $1 AND recipient_hash = $2) OR (requester_hash = $2 AND recipient_hash = $1)) "
        "AND status = 'blocked'",
        {blocker, blocked}
    );
    return {{"status", "unblocked"}};
}

json DBManager::CreateDirectMessage(const std::string& sender_hash, const std::string& receiver_hash,
                                    const std::string& content, const std::string& image_url) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    std::string sender = ResolveExistingProfileHash(sender_hash, true);
    std::string receiver = ResolveExistingProfileHash(receiver_hash, true);
    std::string trimmed_content = TrimCopy(content);

    if (sender.empty() || receiver.empty()) {
        return {{"error", "Both users must publish their profile before direct messages can be delivered"}};
    }
    if (sender == receiver) {
        return {{"error", "Cannot message yourself"}};
    }
    std::string ban_reason;
    if (sender != "admin" && IsProfileBanned(sender, &ban_reason)) {
        return {{"error", ban_reason.empty() ? "This identity is banned from direct messages" : ban_reason}};
    }
    if (trimmed_content.empty() && image_url.empty()) {
        return {{"error", "Message content or image required"}};
    }

    std::string blocker;
    if (sender != "admin" && receiver != "admin" && IsBlockedEitherDirection(sender, receiver, &blocker)) {
        return {{"error", blocker == sender ? "You have blocked this user" : "This user has blocked you"}};
    }

    std::string channel_status = "accepted";
    const bool admin_conversation = sender == "admin" || receiver == "admin";
    const bool accepted_channel = sender == "admin"
        || receiver == "admin"
        || HasAcceptedFriendship(sender, receiver)
        || HasAcceptedMessageChannel(sender, receiver);

    if (!accepted_channel) {
        const std::string outgoing_request = GetMessageRequestStatus(sender, receiver);
        const std::string incoming_request = GetMessageRequestStatus(receiver, sender);

        if (incoming_request == "pending") {
            return {{"error", "This user already sent you a message request. Accept or decline it first."}};
        }
        if (incoming_request == "blocked" || outgoing_request == "blocked") {
            return {{"error", "This conversation is blocked"}};
        }

        if (outgoing_request != "pending") {
            QueryParams(
                "INSERT INTO message_requests (requester_hash, recipient_hash, status, created_at, updated_at, last_message_at) "
                "VALUES ($1, $2, 'pending', NOW(), NOW(), NOW()) "
                "ON CONFLICT (requester_hash, recipient_hash) DO UPDATE SET status = 'pending', updated_at = NOW(), last_message_at = NOW()",
                {sender, receiver}
            );
            CreateNotification(receiver, sender, "message_request", "New message request",
                               ResolveProfileUsername(sender) + " wants to message you.",
                               "/dm/" + sender);
        } else {
            QueryParams(
                "UPDATE message_requests SET updated_at = NOW(), last_message_at = NOW() "
                "WHERE requester_hash = $1 AND recipient_hash = $2",
                {sender, receiver}
            );
        }
        channel_status = "request_pending";
    }

    if (!admin_conversation) {
        if (!image_url.empty()) {
            return {{"error", "Private direct-message attachments must be sent as encrypted inline payloads, not server-hosted image URLs"}};
        }
        if (trimmed_content.empty()) {
            return {{"error", "Private direct messages require an encrypted payload"}};
        }
        if (!LooksLikePqcEnvelope(trimmed_content)) {
            return {{"error", "Private direct messages must be encrypted in the browser before upload"}};
        }
    }

    PGresultPtr res = QueryParams(
        "INSERT INTO direct_messages (sender_hash, receiver_hash, content, image_url) "
        "VALUES ($1, $2, NULLIF($3, ''), $4) "
        "RETURNING id, (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT",
        {sender, receiver, trimmed_content, image_url}
    );

    if (channel_status == "accepted" && receiver != "admin") {
        CreateNotification(receiver, sender, "direct_message", "New direct message",
                           ResolveProfileUsername(sender) + " sent you a direct message.",
                           "/dm/" + sender);
    }

    return {
        {"id", std::stoll(PQgetvalue(res.get(), 0, 0))},
        {"senderHash", sender},
        {"receiverHash", receiver},
        {"content", trimmed_content},
        {"imageUrl", image_url},
        {"timestamp", std::stoll(PQgetvalue(res.get(), 0, 1))},
        {"channelStatus", channel_status}
    };
}

json DBManager::GetDirectMessages(const std::string& user_hash, const std::string& peer_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    std::string me = ResolveExistingProfileHash(user_hash, true);
    std::string peer = ResolveExistingProfileHash(peer_hash, true);

    if (me.empty() || peer.empty()) {
        return {{"messages", json::array()}, {"channelStatus", "unknown"}};
    }

    std::string channel_status = "no_channel";
    std::string blocker;
    if (me == "admin" || peer == "admin" || HasAcceptedFriendship(me, peer) || HasAcceptedMessageChannel(me, peer)) {
        channel_status = "accepted";
    } else if (IsBlockedEitherDirection(me, peer, &blocker)) {
        channel_status = "blocked";
    } else {
        const std::string outgoing_request = GetMessageRequestStatus(me, peer);
        const std::string incoming_request = GetMessageRequestStatus(peer, me);
        if (incoming_request == "pending") {
            channel_status = "request_pending_incoming";
        } else if (outgoing_request == "pending") {
            channel_status = "request_pending_outgoing";
        } else if (incoming_request == "declined" || outgoing_request == "declined") {
            channel_status = "request_declined";
        }
    }

    PGresultPtr res = QueryParams(
        "SELECT id, sender_hash, receiver_hash, COALESCE(content, ''), COALESCE(image_url, ''), "
        "       (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT, read_at IS NOT NULL "
        "FROM direct_messages "
        "WHERE (sender_hash = $1 AND receiver_hash = $2) "
        "   OR (sender_hash = $2 AND receiver_hash = $1) "
        "ORDER BY created_at ASC, id ASC",
        {me, peer}
    );

    json messages = json::array();
    int rows = PQntuples(res.get());
    for (int i = 0; i < rows; ++i) {
        std::string stored_content = PQgetvalue(res.get(), i, 3);
        std::string text = DecodeStoredDirectMessageContent(stored_content, secure_storage_);

        messages.push_back({
            {"id", std::stoll(PQgetvalue(res.get(), i, 0))},
            {"senderHash", PQgetvalue(res.get(), i, 1)},
            {"receiverHash", PQgetvalue(res.get(), i, 2)},
            {"text", text},
            {"isPqcEncrypted", LooksLikePqcEnvelope(text)},
            {"imageUrl", PQgetvalue(res.get(), i, 4)},
            {"timestamp", std::stoll(PQgetvalue(res.get(), i, 5))},
            {"isRead", std::string(PQgetvalue(res.get(), i, 6)) == "t"}
        });
    }

    QueryParams(
        "UPDATE direct_messages SET read_at = NOW() "
        "WHERE sender_hash = $1 AND receiver_hash = $2 AND read_at IS NULL",
        {peer, me}
    );

    return {
        {"peerHash", peer},
        {"channelStatus", channel_status},
        {"messages", messages}
    };
}

json DBManager::GetDirectMessageInbox(const std::string& user_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    std::string me = ResolveExistingProfileHash(user_hash, true);
    if (me.empty()) {
        return {
            {"conversations", json::array()},
            {"received_requests", json::array()},
            {"sent_requests", json::array()}
        };
    }

    PGresultPtr res = QueryParams(
        "SELECT DISTINCT ON (peer) "
        "       peer, "
        "       COALESCE(p.username, 'Anonymous'), "
        "       dm.id, "
        "       dm.sender_hash, "
        "       dm.receiver_hash, "
        "       COALESCE(dm.content, ''), "
        "       COALESCE(dm.image_url, ''), "
        "       (EXTRACT(EPOCH FROM dm.created_at) * 1000)::BIGINT, "
        "       (SELECT COUNT(*) FROM direct_messages WHERE sender_hash = peer AND receiver_hash = $1 AND read_at IS NULL) "
        "FROM ("
        "    SELECT id, sender_hash, receiver_hash, content, image_url, created_at, "
        "           CASE WHEN sender_hash = $1 THEN receiver_hash ELSE sender_hash END as peer "
        "    FROM direct_messages "
        "    WHERE sender_hash = $1 OR receiver_hash = $1 "
        ") dm "
        "LEFT JOIN profiles p ON dm.peer = p.pub_key_hash "
        "ORDER BY peer, dm.created_at DESC, dm.id DESC",
        {me}
    );

    json conversations = json::array();
    int rows = PQntuples(res.get());
    for (int i = 0; i < rows; ++i) {
        std::string peer = PQgetvalue(res.get(), i, 0);
        const bool accepted_channel = peer == "admin"
            || HasAcceptedFriendship(me, peer)
            || HasAcceptedMessageChannel(me, peer);
        if (!accepted_channel) {
            continue;
        }

        std::string username = PQgetvalue(res.get(), i, 1);
        std::string sender = PQgetvalue(res.get(), i, 3);
        std::string receiver = PQgetvalue(res.get(), i, 4);
        std::string stored_content = PQgetvalue(res.get(), i, 5);
        std::string stored_image_url = PQgetvalue(res.get(), i, 6);
        std::string preview = BuildDirectMessagePreview(sender, receiver, stored_content, stored_image_url, secure_storage_);
        int unread_count = std::stoi(PQgetvalue(res.get(), i, 8));

        conversations.push_back({
            {"hash", peer},
            {"username", username},
            {"lastMessage", preview},
            {"lastTimestamp", std::stoll(PQgetvalue(res.get(), i, 7))},
            {"unreadCount", unread_count}
        });
    }

    auto latest_request_preview = [&](const std::string& requester, const std::string& recipient) {
        PGresultPtr request_res = QueryParams(
            "SELECT COALESCE(content, ''), COALESCE(image_url, ''), (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT "
            "FROM direct_messages WHERE sender_hash = $1 AND receiver_hash = $2 "
            "ORDER BY created_at DESC, id DESC LIMIT 1",
            {requester, recipient}
        );
        json out = {
            {"lastMessage", ""},
            {"lastTimestamp", 0},
            {"unreadCount", 0}
        };
        if (PQntuples(request_res.get()) == 0) {
            return out;
        }

        const std::string stored_content = PQgetvalue(request_res.get(), 0, 0);
        const std::string stored_image_url = PQgetvalue(request_res.get(), 0, 1);
        std::string preview = BuildDirectMessagePreview(requester, recipient, stored_content, stored_image_url, secure_storage_);

        PGresultPtr unread_res = QueryParams(
            "SELECT COUNT(*) FROM direct_messages WHERE sender_hash = $1 AND receiver_hash = $2 AND read_at IS NULL",
            {requester, recipient}
        );
        out["lastMessage"] = preview;
        out["lastTimestamp"] = std::stoll(PQgetvalue(request_res.get(), 0, 2));
        out["unreadCount"] = std::stoi(PQgetvalue(unread_res.get(), 0, 0));
        return out;
    };

    PGresultPtr received_requests = QueryParams(
        "SELECT requester_hash FROM message_requests WHERE recipient_hash = $1 AND status = 'pending' ORDER BY last_message_at DESC",
        {me}
    );
    json incoming = json::array();
    for (int i = 0; i < PQntuples(received_requests.get()); ++i) {
        const std::string requester = PQgetvalue(received_requests.get(), i, 0);
        json preview = latest_request_preview(requester, me);
        incoming.push_back({
            {"hash", requester},
            {"username", ResolveProfileUsername(requester)},
            {"lastMessage", preview["lastMessage"]},
            {"lastTimestamp", preview["lastTimestamp"]},
            {"unreadCount", preview["unreadCount"]}
        });
    }

    PGresultPtr sent_requests = QueryParams(
        "SELECT recipient_hash FROM message_requests WHERE requester_hash = $1 AND status = 'pending' ORDER BY last_message_at DESC",
        {me}
    );
    json outgoing = json::array();
    for (int i = 0; i < PQntuples(sent_requests.get()); ++i) {
        const std::string recipient = PQgetvalue(sent_requests.get(), i, 0);
        json preview = latest_request_preview(me, recipient);
        outgoing.push_back({
            {"hash", recipient},
            {"username", ResolveProfileUsername(recipient)},
            {"lastMessage", preview["lastMessage"]},
            {"lastTimestamp", preview["lastTimestamp"]},
            {"unreadCount", preview["unreadCount"]}
        });
    }

    return {
        {"conversations", conversations},
        {"received_requests", incoming},
        {"sent_requests", outgoing}
    };
}

json DBManager::RespondToMessageRequest(const std::string& actor_hash, const std::string& requester_hash, const std::string& action) {
    if (TrimCopy(action) == "block") {
        return BlockUser(actor_hash, requester_hash);
    }

    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveExistingProfileHash(actor_hash);
    const std::string requester = ResolveExistingProfileHash(requester_hash);
    const std::string normalized_action = TrimCopy(action);
    if (actor.empty() || requester.empty()) {
        return {{"error", "Both users must publish a profile before responding to message requests"}};
    }
    std::string ban_reason;
    if (IsProfileBanned(actor, &ban_reason)) {
        return {{"error", ban_reason.empty() ? "This identity is banned from message moderation" : ban_reason}};
    }

    PGresultPtr pending = QueryParams(
        "SELECT 1 FROM message_requests WHERE requester_hash = $1 AND recipient_hash = $2 AND status = 'pending' LIMIT 1",
        {requester, actor}
    );
    if (PQntuples(pending.get()) == 0) {
        return {{"error", "No pending message request from that user"}};
    }

    if (normalized_action == "accept") {
        QueryParams(
            "UPDATE message_requests SET status = 'accepted', updated_at = NOW() "
            "WHERE requester_hash = $1 AND recipient_hash = $2",
            {requester, actor}
        );
        QueryParams(
            "UPDATE message_requests SET status = 'accepted', updated_at = NOW() "
            "WHERE requester_hash = $1 AND recipient_hash = $2 AND status = 'pending'",
            {actor, requester}
        );
        CreateNotification(requester, actor, "message_request_accept", "Message request accepted",
                           ResolveProfileUsername(actor) + " accepted your message request.",
                           "/dm/" + actor);
        return {{"status", "accepted"}};
    }

    if (normalized_action == "decline") {
        QueryParams(
            "UPDATE message_requests SET status = 'declined', updated_at = NOW() "
            "WHERE requester_hash = $1 AND recipient_hash = $2",
            {requester, actor}
        );
        CreateNotification(requester, actor, "message_request_decline", "Message request declined",
                           ResolveProfileUsername(actor) + " declined your message request.",
                           "/u/" + actor);
        return {{"status", "declined"}};
    }

    return {{"error", "Unsupported message request action"}};
}

json DBManager::GetNotifications(const std::string& user_hash, int limit) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string user = ResolveExistingProfileHash(user_hash);
    if (user.empty()) {
        return {{"notifications", json::array()}};
    }

    PGresultPtr res = QueryParams(
        "SELECT id, COALESCE(actor_hash, ''), type, title, COALESCE(body, ''), COALESCE(link, ''), "
        "       read_at IS NOT NULL, (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT "
        "FROM notifications WHERE user_hash = $1 "
        "ORDER BY created_at DESC LIMIT $2",
        {user, std::to_string(limit > 0 ? limit : 50)}
    );

    json notifications = json::array();
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        const std::string actor = PQgetvalue(res.get(), i, 1);
        notifications.push_back({
            {"id", std::stoll(PQgetvalue(res.get(), i, 0))},
            {"actorHash", actor},
            {"actorUsername", actor.empty() ? "" : ResolveProfileUsername(actor)},
            {"type", PQgetvalue(res.get(), i, 2)},
            {"title", PQgetvalue(res.get(), i, 3)},
            {"body", PQgetvalue(res.get(), i, 4)},
            {"link", PQgetvalue(res.get(), i, 5)},
            {"read", std::string(PQgetvalue(res.get(), i, 6)) == "t"},
            {"timestamp", std::stoll(PQgetvalue(res.get(), i, 7))}
        });
    }

    return {{"notifications", notifications}};
}

json DBManager::MarkNotificationsRead(const std::string& user_hash, const std::string& notification_id) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string user = ResolveExistingProfileHash(user_hash);
    if (user.empty()) {
        return {{"error", "Published profile required"}};
    }

    if (notification_id.empty()) {
        QueryParams(
            "UPDATE notifications SET read_at = NOW() WHERE user_hash = $1 AND read_at IS NULL",
            {user}
        );
        return {{"status", "all_read"}};
    }

    QueryParams(
        "UPDATE notifications SET read_at = NOW() WHERE user_hash = $1 AND id = $2",
        {user, notification_id}
    );
    return {{"status", "read"}, {"id", notification_id}};
}

json DBManager::GetNotificationSummary(const std::string& user_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string user = ResolveExistingProfileHash(user_hash);
    if (user.empty()) {
        return {
            {"notificationsUnread", 0},
            {"friendRequestsPending", 0},
            {"messageRequestsPending", 0},
            {"dmUnread", 0},
            {"total", 0}
        };
    }

    auto count_single = [&](const std::string& sql, const std::vector<std::string>& params) {
        PGresultPtr res = QueryParams(sql, params);
        return std::stoi(PQgetvalue(res.get(), 0, 0));
    };

    const int notifications_unread = count_single(
        "SELECT COUNT(*) FROM notifications WHERE user_hash = $1 AND read_at IS NULL",
        {user}
    );
    const int friend_requests_pending = count_single(
        "SELECT COUNT(*) FROM friend_requests WHERE receiver_hash = $1 AND status = 0",
        {user}
    );
    const int message_requests_pending = count_single(
        "SELECT COUNT(*) FROM message_requests WHERE recipient_hash = $1 AND status = 'pending'",
        {user}
    );
    const int dm_unread = count_single(
        "SELECT COUNT(*) FROM direct_messages dm WHERE receiver_hash = $1 AND read_at IS NULL AND ("
        "dm.sender_hash = 'admin' "
        "OR EXISTS (SELECT 1 FROM friend_requests fr WHERE fr.status = 1 AND ("
        "   (fr.sender_hash = dm.sender_hash AND fr.receiver_hash = $1) "
        "   OR "
        "   (fr.sender_hash = $1 AND fr.receiver_hash = dm.sender_hash)"
        ")) "
        "OR EXISTS (SELECT 1 FROM message_requests mr WHERE mr.status = 'accepted' AND ("
        "   (mr.requester_hash = dm.sender_hash AND mr.recipient_hash = $1) "
        "   OR "
        "   (mr.requester_hash = $1 AND mr.recipient_hash = dm.sender_hash)"
        "))"
        ")",
        {user}
    );

    return {
        {"notificationsUnread", notifications_unread},
        {"friendRequestsPending", friend_requests_pending},
        {"messageRequestsPending", message_requests_pending},
        {"dmUnread", dm_unread},
        {"total", notifications_unread + friend_requests_pending + message_requests_pending + dm_unread}
    };
}

json DBManager::CreateReport(const std::string& reporter_hash, const std::string& target_hash, const std::string& reason,
                             const std::string& target_kind, int64_t target_post_id, int64_t target_thread_id,
                             const std::string& target_board_id, const std::string& target_display_name,
                             const std::string& context_link) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string reporter = ResolveExistingProfileHash(reporter_hash);
    const std::string normalized_kind = TrimCopy(target_kind).empty() ? "user" : TrimCopy(target_kind);
    const std::string target = ResolveExistingProfileHash(target_hash);
    const std::string trimmed_reason = TrimCopy(reason);
    if (reporter.empty()) {
        return {{"error", "Reporter profile not found"}};
    }
    std::string ban_reason;
    if (IsProfileBanned(reporter, &ban_reason)) {
        return {{"error", ban_reason.empty() ? "This identity is banned from filing reports" : ban_reason}};
    }
    if (normalized_kind == "user" && target.empty()) {
        return {{"error", "Both users must publish a profile before reports can be filed"}};
    }
    if (normalized_kind == "user" && reporter == target) {
        return {{"error", "Cannot report yourself"}};
    }
    if (trimmed_reason.empty()) {
        return {{"error", "Report reason required"}};
    }
    if (normalized_kind != "user" && normalized_kind != "post") {
        return {{"error", "Unsupported report type"}};
    }
    if (normalized_kind == "post" && target_post_id <= 0) {
        return {{"error", "Post report missing target post id"}};
    }

    std::string resolved_target = target;
    std::string resolved_board = TrimCopy(target_board_id);
    std::string resolved_display_name = TrimCopy(target_display_name);
    std::string resolved_context_link = TrimCopy(context_link);
    std::string notification_title = "New user report";
    std::string notification_body;
    std::string notification_link;

    if (normalized_kind == "post") {
        const std::string post_id = std::to_string(target_post_id);
        PGresultPtr post_res = QueryParams(
            "SELECT board_id, thread_id, COALESCE(name, 'Anonymous'), COALESCE(content, ''), is_op, COALESCE(author_hash, '') "
            "FROM posts WHERE id = $1 LIMIT 1",
            {post_id}
        );
        if (PQntuples(post_res.get()) == 0) {
            return {{"error", "Target post not found"}};
        }
        resolved_board = PQgetvalue(post_res.get(), 0, 0);
        const std::string thread_id = PQgetvalue(post_res.get(), 0, 1);
        target_thread_id = std::stoll(thread_id);
        if (resolved_display_name.empty()) {
            resolved_display_name = PQgetvalue(post_res.get(), 0, 2);
        }
        if (resolved_target.empty()) {
            resolved_target = PQgetvalue(post_res.get(), 0, 5);
        }
        if (resolved_context_link.empty()) {
            resolved_context_link = "/" + resolved_board + "/thread/" + thread_id + "#p" + post_id;
        }
        notification_title = "New post report";
        notification_body = ResolveProfileUsername(reporter) + " reported post #" + post_id + ".";
        notification_link = resolved_context_link;
    } else {
        notification_body = ResolveProfileUsername(reporter) + " reported " + ResolveProfileUsername(resolved_target) + ".";
        notification_link = !resolved_context_link.empty() ? resolved_context_link : "/u/" + resolved_target;
    }

    PGresultPtr res = QueryParams(
        "INSERT INTO reports (reporter_hash, target_hash, reason, target_kind, target_post_id, target_thread_id, target_board_id, target_display_name, context_link) "
        "VALUES ($1, $2, $3, $4, NULLIF($5, '0')::BIGINT, NULLIF($6, '0')::BIGINT, $7, $8, $9) "
        "RETURNING id, (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT",
        {
            reporter,
            resolved_target,
            trimmed_reason,
            normalized_kind,
            std::to_string(target_post_id),
            std::to_string(target_thread_id),
            resolved_board,
            resolved_display_name,
            resolved_context_link
        }
    );

    PGresultPtr moderators = Query(
        "SELECT pub_key_hash FROM profiles WHERE role IN ('founder', 'moderator')"
    );
    for (int i = 0; i < PQntuples(moderators.get()); ++i) {
        const std::string moderator_hash = PQgetvalue(moderators.get(), i, 0);
        if (moderator_hash != reporter) {
            CreateNotification(moderator_hash, reporter, "report", notification_title, notification_body, notification_link);
        }
    }

    return {
        {"status", "reported"},
        {"id", std::stoll(PQgetvalue(res.get(), 0, 0))},
        {"timestamp", std::stoll(PQgetvalue(res.get(), 0, 1))}
    };
}

json DBManager::GetModerationReports(const std::string& actor_hash, const std::string& founder_session_hash, int limit) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_hash)) {
        return {{"error", "Moderator authorization failed"}};
    }

    const int safe_limit = std::max(1, std::min(limit, 200));
    PGresultPtr res = QueryParams(
        "SELECT id, reporter_hash, target_hash, reason, status, "
        "COALESCE(target_kind, 'user'), COALESCE(target_post_id::TEXT, ''), COALESCE(target_thread_id::TEXT, ''), "
        "COALESCE(target_board_id, ''), COALESCE(target_display_name, ''), COALESCE(context_link, ''), "
        "COALESCE((EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT::TEXT, ''), "
        "COALESCE((EXTRACT(EPOCH FROM resolved_at) * 1000)::BIGINT::TEXT, ''), "
        "COALESCE(resolution_note, ''), COALESCE(resolved_by_hash, ''), "
        "COALESCE(resolved_by_label, ''), COALESCE(resolved_by_badge, '') "
        "FROM reports ORDER BY created_at DESC LIMIT $1",
        {std::to_string(safe_limit)}
    );

    json items = json::array();
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        const std::string reporter = PQgetvalue(res.get(), i, 1);
        const std::string target_hash = PQgetvalue(res.get(), i, 2);
        items.push_back({
            {"id", std::stoll(PQgetvalue(res.get(), i, 0))},
            {"reporterHash", reporter},
            {"reporterLabel", ResolveProfileUsername(reporter)},
            {"targetHash", target_hash},
            {"targetLabel", target_hash.empty() ? PQgetvalue(res.get(), i, 9) : ResolveProfileUsername(target_hash)},
            {"reason", PQgetvalue(res.get(), i, 3)},
            {"status", PQgetvalue(res.get(), i, 4)},
            {"targetKind", PQgetvalue(res.get(), i, 5)},
            {"targetPostId", PQgetvalue(res.get(), i, 6)},
            {"targetThreadId", PQgetvalue(res.get(), i, 7)},
            {"targetBoardId", PQgetvalue(res.get(), i, 8)},
            {"targetDisplayName", PQgetvalue(res.get(), i, 9)},
            {"contextLink", PQgetvalue(res.get(), i, 10)},
            {"createdAt", PQgetvalue(res.get(), i, 11)},
            {"resolvedAt", PQgetvalue(res.get(), i, 12)},
            {"resolutionNote", PQgetvalue(res.get(), i, 13)},
            {"resolvedByHash", PQgetvalue(res.get(), i, 14)},
            {"resolvedByLabel", PQgetvalue(res.get(), i, 15)},
            {"resolvedByBadge", PQgetvalue(res.get(), i, 16)},
        });
    }

    return {{"reports", items}};
}

json DBManager::GetModerationAudit(const std::string& actor_hash, const std::string& founder_session_hash, int limit) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_hash)) {
        return {{"error", "Moderator authorization failed"}};
    }

    const int safe_limit = std::max(1, std::min(limit, 200));
    PGresultPtr res = QueryParams(
        "SELECT id, actor_hash, COALESCE(actor_label, ''), COALESCE(actor_badge, ''), action, summary, COALESCE(target_hash, ''), "
        "COALESCE(target_label, ''), COALESCE(target_badge, ''), "
        "COALESCE(report_id::TEXT, ''), COALESCE(target_post_id::TEXT, ''), "
        "COALESCE(target_thread_id::TEXT, ''), COALESCE(target_board_id, ''), "
        "COALESCE((EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT::TEXT, '') "
        "FROM moderation_events ORDER BY created_at DESC LIMIT $1",
        {std::to_string(safe_limit)}
    );

    json items = json::array();
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        const std::string event_actor = PQgetvalue(res.get(), i, 1);
        const std::string action = PQgetvalue(res.get(), i, 4);
        const std::string target_hash = PQgetvalue(res.get(), i, 6);
        const std::string board_id = PQgetvalue(res.get(), i, 12);
        const std::string thread_id = PQgetvalue(res.get(), i, 11);
        const std::string post_id = PQgetvalue(res.get(), i, 10);

        std::string target_link;
        if ((action == "report_open" || action == "report_resolved" || action == "report_dismissed")
            && !post_id.empty() && !board_id.empty() && !thread_id.empty()) {
            target_link = "/" + board_id + "/thread/" + thread_id + "#p" + post_id;
        } else if (!target_hash.empty()) {
            target_link = "/u/" + target_hash;
        }

        items.push_back({
            {"id", std::stoll(PQgetvalue(res.get(), i, 0))},
            {"actorHash", event_actor},
            {"actorLabel", PQgetvalue(res.get(), i, 2)},
            {"actorBadge", PQgetvalue(res.get(), i, 3)},
            {"action", action},
            {"summary", PQgetvalue(res.get(), i, 5)},
            {"targetHash", target_hash},
            {"targetLabel", PQgetvalue(res.get(), i, 7)},
            {"targetBadge", PQgetvalue(res.get(), i, 8)},
            {"reportId", PQgetvalue(res.get(), i, 9)},
            {"targetPostId", post_id},
            {"targetThreadId", thread_id},
            {"targetBoardId", board_id},
            {"targetLink", target_link},
            {"createdAt", PQgetvalue(res.get(), i, 13)},
        });
    }

    return {{"events", items}};
}

json DBManager::ResolveModerationReport(const std::string& actor_hash, const std::string& founder_session_hash,
                                        int64_t report_id, const std::string& status, const std::string& note) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    const std::string normalized_status = TrimCopy(status);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_hash)) {
        return {{"error", "Moderator authorization failed"}};
    }
    if (report_id <= 0) {
        return {{"error", "Report id required"}};
    }
    if (normalized_status != "open" && normalized_status != "resolved" && normalized_status != "dismissed") {
        return {{"error", "Unsupported report status"}};
    }

    PGresultPtr report_res = QueryParams(
        "SELECT COALESCE(target_kind, 'user'), COALESCE(target_hash, ''), COALESCE(target_display_name, ''), "
        "COALESCE(target_post_id::TEXT, ''), COALESCE(target_thread_id::TEXT, ''), COALESCE(target_board_id, '') "
        "FROM reports WHERE id = $1 LIMIT 1",
        {std::to_string(report_id)}
    );
    if (PQntuples(report_res.get()) == 0) {
        return {{"error", "Report not found"}};
    }

    QueryParams(
        "UPDATE reports SET status = $1, resolved_at = CASE WHEN $1 = 'open' THEN NULL ELSE NOW() END, "
        "resolution_note = $2, resolved_by_hash = $3, "
        "resolved_by_label = CASE WHEN $1 = 'open' THEN '' ELSE $4 END, "
        "resolved_by_badge = CASE WHEN $1 = 'open' THEN '' ELSE $5 END "
        "WHERE id = $6",
        {normalized_status, TrimCopy(note), actor, ResolveProfileUsername(actor), GetRoleBadge(actor), std::to_string(report_id)}
    );

    const std::string target_kind = PQgetvalue(report_res.get(), 0, 0);
    const std::string target_hash = PQgetvalue(report_res.get(), 0, 1);
    const std::string target_display_name = PQgetvalue(report_res.get(), 0, 2);
    const std::string target_post_id = PQgetvalue(report_res.get(), 0, 3);
    const std::string target_thread_id = PQgetvalue(report_res.get(), 0, 4);
    const std::string target_board_id = PQgetvalue(report_res.get(), 0, 5);
    const std::string target_label = !target_hash.empty()
        ? ResolveProfileUsername(target_hash)
        : (!target_display_name.empty() ? target_display_name : ("post #" + target_post_id));
    const std::string summary = normalized_status == "resolved"
        ? "Resolved report #" + std::to_string(report_id) + " against " + target_label + "."
        : normalized_status == "dismissed"
            ? "Dismissed report #" + std::to_string(report_id) + " against " + target_label + "."
            : "Reopened report #" + std::to_string(report_id) + " against " + target_label + ".";
    CreateModerationEvent(
        actor,
        "report_" + normalized_status,
        summary,
        target_hash,
        report_id,
        target_post_id.empty() ? 0 : std::stoll(target_post_id),
        target_thread_id.empty() ? 0 : std::stoll(target_thread_id),
        target_board_id
    );

    return {{"status", normalized_status}, {"id", report_id}, {"resolvedBy", actor}};
}

json DBManager::BanUserAsModerator(const std::string& actor_hash, const std::string& founder_session_cookie,
                                   const std::string& target_hash, const std::string& reason) {
    return BanUser(actor_hash, founder_session_cookie, target_hash, "identity", reason, 0);
}

json DBManager::UnbanUserAsModerator(const std::string& actor_hash, const std::string& founder_session_cookie,
                                     const std::string& target_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_cookie)) {
        return {{"error", "Moderator authorization failed"}};
    }
    const std::string target = ResolveExistingProfileHash(target_hash);
    if (target.empty()) {
        return {{"error", "Target profile not found"}};
    }

    std::string salt = Config::Instance().Get().server_salt;
    std::string hashed_target = sha256_hex(target + salt);

    QueryParams("DELETE FROM bans WHERE target_identifier = $1", {hashed_target});
    CreateNotification(target, actor, "moderation_unban", "Account restriction lifted",
                       "Your identity is no longer restricted by moderation.",
                       "/u/" + target);
    CreateModerationEvent(actor, "unban_user",
                          "Lifted moderation restrictions for " + ResolveProfileUsername(target) + ".",
                          target);
    return {{"status", "unbanned"}, {"target_hash", target}};
}

json DBManager::BanUser(const std::string& actor_hash, const std::string& founder_session_cookie,
                        const std::string& target, const std::string& ban_type,
                        const std::string& reason, int64_t duration_seconds) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_cookie)) {
        return {{"error", "Moderator authorization failed"}};
    }

    std::string trimmed_target = TrimCopy(target);
    std::string trimmed_reason = TrimCopy(reason);
    std::string trimmed_type = TrimCopy(ban_type);

    if (trimmed_target.empty() || trimmed_type.empty()) {
        return {{"error", "Target and ban_type are required"}};
    }

    if (trimmed_type != "identity" && trimmed_type != "ip") {
        return {{"error", "Invalid ban_type (must be 'identity' or 'ip')"}};
    }

    if (trimmed_type == "identity") {
        const std::string resolved_target = ResolveExistingProfileHash(trimmed_target);
        if (!resolved_target.empty()) {
            PGresultPtr target_role = QueryParams(
                "SELECT COALESCE(role, 'user') FROM profiles WHERE pub_key_hash = $1 LIMIT 1",
                {resolved_target}
            );
            const std::string normalized_target_role = PQntuples(target_role.get()) > 0
                ? NormalizeRole(PQgetvalue(target_role.get(), 0, 0))
                : "user";
            if (normalized_target_role == "founder" || normalized_target_role == "moderator") {
                return {{"error", "Moderator identities cannot be banned"}};
            }
        }
    }

    std::string salt = Config::Instance().Get().server_salt;
    std::string hashed_target = sha256_hex(trimmed_target + salt);

    std::string expires_at_str = "";
    bool has_expiry = (duration_seconds > 0);
    
    PGresultPtr res;
    if (has_expiry) {
        res = QueryParams(
            "INSERT INTO bans (target_identifier, ban_type, reason, expires_at, created_by, created_at) "
            "VALUES ($1, $2, $3, NOW() + ($4 || ' seconds')::INTERVAL, $5, NOW()) "
            "ON CONFLICT (target_identifier) DO UPDATE SET "
            "reason = EXCLUDED.reason, expires_at = EXCLUDED.expires_at, created_by = EXCLUDED.created_by, created_at = NOW() "
            "RETURNING id, expires_at",
            {hashed_target, trimmed_type, trimmed_reason, std::to_string(duration_seconds), actor}
        );
    } else {
        res = QueryParams(
            "INSERT INTO bans (target_identifier, ban_type, reason, expires_at, created_by, created_at) "
            "VALUES ($1, $2, $3, NULL, $4, NOW()) "
            "ON CONFLICT (target_identifier) DO UPDATE SET "
            "reason = EXCLUDED.reason, expires_at = NULL, created_by = EXCLUDED.created_by, created_at = NOW() "
            "RETURNING id, expires_at",
            {hashed_target, trimmed_type, trimmed_reason, actor}
        );
    }

    if (PQntuples(res.get()) == 0) {
        return {{"error", "Failed to insert/update ban"}};
    }

    std::string ban_id = PQgetvalue(res.get(), 0, 0);
    std::string expires_at_val = PQgetisnull(res.get(), 0, 1) ? "never" : PQgetvalue(res.get(), 0, 1);

    if (trimmed_type == "identity") {
        const std::string resolved_target = ResolveExistingProfileHash(trimmed_target);
        if (!resolved_target.empty()) {
            CreateNotification(resolved_target, actor, "moderation_ban", "Account restricted",
                               trimmed_reason.empty() ? "Your identity has been restricted by moderation." : trimmed_reason,
                               "/u/" + resolved_target);
            CreateModerationEvent(actor, "ban_user",
                                  "Banned identity " + ResolveProfileUsername(resolved_target) + " (Exp: " + expires_at_val + ").",
                                  resolved_target);
        }
    } else {
        CreateModerationEvent(actor, "ban_ip",
                              "Banned IP address " + hashed_target.substr(0, 8) + "... (Exp: " + expires_at_val + ").",
                              "");
    }

    return {{"status", "banned"}, {"id", ban_id}, {"target_identifier", hashed_target}, {"expires_at", expires_at_val}};
}

json DBManager::UnbanUser(const std::string& actor_hash, const std::string& founder_session_cookie,
                          const std::string& ban_id) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_cookie)) {
        return {{"error", "Moderator authorization failed"}};
    }

    PGresultPtr res = QueryParams(
        "DELETE FROM bans WHERE id = $1 RETURNING target_identifier, ban_type",
        {ban_id}
    );

    if (PQntuples(res.get()) == 0) {
        return {{"error", "Ban record not found"}};
    }

    std::string target_identifier = PQgetvalue(res.get(), 0, 0);
    std::string ban_type = PQgetvalue(res.get(), 0, 1);

    CreateModerationEvent(actor, "unban_user",
                          "Unbanned target " + target_identifier.substr(0, 8) + "... (Type: " + ban_type + ").",
                          "");

    return {{"status", "unbanned"}, {"id", ban_id}};
}

json DBManager::GetBans(const std::string& actor_hash, const std::string& founder_session_cookie) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_cookie)) {
        return {{"error", "Moderator authorization failed"}};
    }

    PGresultPtr res = Query(
        "SELECT id, target_identifier, ban_type, reason, expires_at, created_by, created_at "
        "FROM bans ORDER BY created_at DESC"
    );

    json list = json::array();
    int rows = PQntuples(res.get());
    for (int i = 0; i < rows; ++i) {
        json item;
        item["id"] = PQgetvalue(res.get(), i, 0);
        item["target_identifier"] = PQgetvalue(res.get(), i, 1);
        item["ban_type"] = PQgetvalue(res.get(), i, 2);
        item["reason"] = PQgetvalue(res.get(), i, 3);
        item["expires_at"] = PQgetisnull(res.get(), i, 4) ? json() : PQgetvalue(res.get(), i, 4);
        item["created_by"] = PQgetvalue(res.get(), i, 5);
        item["created_at"] = PQgetvalue(res.get(), i, 6);
        list.push_back(item);
    }

    return list;
}

json DBManager::ExtendBan(const std::string& actor_hash, const std::string& founder_session_cookie,
                          const std::string& ban_id, int64_t duration_seconds) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_cookie)) {
        return {{"error", "Moderator authorization failed"}};
    }

    if (duration_seconds <= 0) {
        return {{"error", "Extension duration must be positive"}};
    }

    PGresultPtr res = QueryParams(
        "UPDATE bans "
        "SET expires_at = COALESCE(expires_at, NOW()) + ($2 || ' seconds')::INTERVAL "
        "WHERE id = $1 RETURNING target_identifier, expires_at",
        {ban_id, std::to_string(duration_seconds)}
    );

    if (PQntuples(res.get()) == 0) {
        return {{"error", "Ban record not found"}};
    }

    std::string target_identifier = PQgetvalue(res.get(), 0, 0);
    std::string expires_at = PQgetvalue(res.get(), 0, 1);

    CreateModerationEvent(actor, "extend_ban",
                          "Extended ban for " + target_identifier.substr(0, 8) + "... (New Exp: " + expires_at + ").",
                          "");

    return {{"status", "extended"}, {"id", ban_id}, {"expires_at", expires_at}};
}

json DBManager::DeletePostAsModerator(const std::string& actor_hash, const std::string& founder_session_cookie, int64_t post_id) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_cookie)) {
        return {{"error", "Moderator authorization failed"}};
    }
    if (post_id <= 0) {
        return {{"error", "Post id required"}};
    }

    PGresultPtr post_res = QueryParams(
        "SELECT thread_id, board_id, is_op FROM posts WHERE id = $1 LIMIT 1",
        {std::to_string(post_id)}
    );
    if (PQntuples(post_res.get()) == 0) {
        return {{"error", "Post not found"}};
    }

    const int64_t thread_id = std::stoll(PQgetvalue(post_res.get(), 0, 0));
    const std::string board_id = PQgetvalue(post_res.get(), 0, 1);
    const bool is_op = std::string(PQgetvalue(post_res.get(), 0, 2)) == "t";

    if (is_op) {
        QueryParams("DELETE FROM threads WHERE id = $1", {std::to_string(thread_id)});
    } else {
        QueryParams("DELETE FROM posts WHERE id = $1", {std::to_string(post_id)});
    }

    CreateModerationEvent(
        actor,
        is_op ? "delete_thread" : "delete_post",
        is_op
            ? "Deleted thread #" + std::to_string(thread_id) + " on /" + board_id + "/."
            : "Deleted post #" + std::to_string(post_id) + " in thread #" + std::to_string(thread_id) + " on /" + board_id + "/.",
        "",
        0,
        post_id,
        thread_id,
        board_id
    );

    return {
        {"status", "deleted"},
        {"postId", post_id},
        {"threadId", thread_id},
        {"boardId", board_id},
        {"threadDeleted", is_op}
    };
}

bool DBManager::HasAcceptedFriendship(const std::string& left_hash, const std::string& right_hash) {
    PGresultPtr res = QueryParams(
        "SELECT 1 FROM friend_requests "
        "WHERE status = 1 AND ("
        "  (sender_hash = $1 AND receiver_hash = $2) "
        "  OR "
        "  (sender_hash = $2 AND receiver_hash = $1)"
        ") LIMIT 1",
        {left_hash, right_hash}
    );
    return PQntuples(res.get()) > 0;
}

bool DBManager::HasAcceptedMessageChannel(const std::string& left_hash, const std::string& right_hash) {
    PGresultPtr res = QueryParams(
        "SELECT 1 FROM message_requests "
        "WHERE status = 'accepted' AND ("
        "  (requester_hash = $1 AND recipient_hash = $2) "
        "  OR "
        "  (requester_hash = $2 AND recipient_hash = $1)"
        ") LIMIT 1",
        {left_hash, right_hash}
    );
    return PQntuples(res.get()) > 0;
}

std::string DBManager::GetMessageRequestStatus(const std::string& requester_hash, const std::string& recipient_hash) {
    PGresultPtr res = QueryParams(
        "SELECT status FROM message_requests WHERE requester_hash = $1 AND recipient_hash = $2 LIMIT 1",
        {requester_hash, recipient_hash}
    );
    if (PQntuples(res.get()) == 0) {
        return "";
    }
    return PQgetvalue(res.get(), 0, 0);
}

bool DBManager::IsBlockedEitherDirection(const std::string& left_hash, const std::string& right_hash, std::string* blocker_hash) {
    PGresultPtr res = QueryParams(
        "SELECT blocker_hash FROM blocks WHERE "
        "(blocker_hash = $1 AND blocked_hash = $2) OR (blocker_hash = $2 AND blocked_hash = $1) "
        "LIMIT 1",
        {left_hash, right_hash}
    );
    if (PQntuples(res.get()) == 0) {
        return false;
    }
    if (blocker_hash) {
        *blocker_hash = PQgetvalue(res.get(), 0, 0);
    }
    return true;
}

bool DBManager::IsModeratorAuthorized(const std::string& actor_hash, const std::string& founder_session_cookie, bool founder_only) {
    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty()) {
        return false;
    }

    PGresultPtr actor_res = QueryParams(
        "SELECT COALESCE(role, 'user'), COALESCE(active_session_hash, ''), "
        "       COALESCE(session_expires_at > NOW(), FALSE) "
        "FROM profiles WHERE pub_key_hash = $1 LIMIT 1",
        {actor}
    );
    if (PQntuples(actor_res.get()) == 0) {
        return false;
    }

    const std::string actor_role = NormalizeRole(PQgetvalue(actor_res.get(), 0, 0));
    const std::string stored_active_session_hash = PQgetvalue(actor_res.get(), 0, 1);
    const bool not_expired = std::string(PQgetvalue(actor_res.get(), 0, 2)) == "t";

    if (actor_role == "founder") {
        if (founder_session_cookie.empty()) {
            return false;
        }
        std::string hashed_cookie = sha256_hex(founder_session_cookie);
        return not_expired && !stored_active_session_hash.empty() && stored_active_session_hash == hashed_cookie;
    }
    if (founder_only) {
        return false;
    }
    return actor_role == "moderator";
}


// =============================================================================
// Board Seeding
// =============================================================================

void DBManager::SeedBoards() {
    ConnLease lease(*pool_);
    PGconn* conn = lease.get();
    if (!conn) {
        Logger::Error("Failed to seed boards: no database connection available");
        return;
    }

    PGresultPtr res = Query("SELECT COUNT(*) FROM boards", conn);
    int count = std::stoi(PQgetvalue(res.get(), 0, 0));
    if (count > 0) return;

    Logger::Info("Seeding default boards...");

    struct BoardDef { const char* id; const char* name; const char* desc; const char* icon; const char* nsfw; };
    BoardDef boards[] = {
        {"b",     "Random",                "The random board. Anything goes.",           "/icons/b.png",     "true"},
        {"g",     "Technology",            "Discussion of technology and computing.",    "/icons/g.png",     "false"},
        {"sci",   "Science & Math",        "Scientific discussion and mathematics.",     "/icons/sci.png",   "false"},
        {"pol",   "Politically Incorrect", "Political news and discussion.",             "/icons/pol.png",   "false"},
        {"a",     "Anime & Manga",         "Anime, manga, and related Japanese media.", "/icons/a.png",     "false"},
        {"v",     "Video Games",           "Video games and gaming culture.",            "/icons/v.png",     "false"},
        {"fit",   "Fitness",               "Fitness, health, and sports.",               "/icons/fit.png",   "false"},
        {"mu",    "Music",                 "Music, albums, and artists.",                "/icons/mu.png",    "false"},
        {"x",     "Paranormal",            "Paranormal, conspiracy, occult.",            "/icons/x.png",     "false"},
        {"qc",    "QuanChan Meta",         "QuanChan encrypted discussions.",            "/icons/qc.png",    "false"},
        {"adult", "Adult +18",             "Not safe for work adult content.",           "/icons/adult.png", "true"},
    };

    for (const auto& b : boards) {
        const char* pv[5] = { b.id, b.name, b.desc, b.icon, b.nsfw };
        PGresultPtr ins(PQexecParams(conn,
            "INSERT INTO boards (id, name, description, icon, nsfw) VALUES ($1, $2, $3, $4, $5) ON CONFLICT (id) DO NOTHING",
            5, nullptr, pv, nullptr, nullptr, 0));
    }

    Logger::Info("Seeded 11 default boards.");
}

std::string DBManager::ResolveProfileHash(const std::string& value) {
    std::string trimmed = TrimCopy(value);
    if (trimmed.empty() || trimmed == "admin") {
        return trimmed;
    }

    PGresultPtr res = QueryParams(
        "SELECT pub_key_hash FROM profiles WHERE pub_key_hash = $1 OR LOWER(username) = LOWER($1) LIMIT 1",
        {trimmed}
    );
    if (PQntuples(res.get()) > 0) {
        return PQgetvalue(res.get(), 0, 0);
    }
    return trimmed;
}

std::string DBManager::ResolveExistingProfileHash(const std::string& value, bool allow_admin) {
    std::string trimmed = TrimCopy(value);
    if (trimmed.empty()) {
        return "";
    }
    if (allow_admin && trimmed == "admin") {
        return trimmed;
    }

    PGresultPtr res = QueryParams(
        "SELECT pub_key_hash FROM profiles WHERE pub_key_hash = $1 OR LOWER(username) = LOWER($1) LIMIT 1",
        {trimmed}
    );
    if (PQntuples(res.get()) == 0) {
        return "";
    }
    return PQgetvalue(res.get(), 0, 0);
}

std::string DBManager::ResolveProfileUsername(const std::string& value) {
    if (value == "admin") {
        return "Admin";
    }

    PGresultPtr res = QueryParams(
        "SELECT username FROM profiles WHERE pub_key_hash = $1 LIMIT 1",
        {value}
    );
    if (PQntuples(res.get()) > 0) {
        std::string username = PQgetvalue(res.get(), 0, 0);
        if (!username.empty()) {
            return username;
        }
    }
    return ShortHash(value);
}

std::string DBManager::EnsureModeratorBadge(const std::string& profile_hash) {
    if (profile_hash.empty()) {
        return "";
    }

    PGresultPtr existing = QueryParams(
        "SELECT COALESCE(role, 'user'), COALESCE(moderator_badge, '') "
        "FROM profiles WHERE pub_key_hash = $1 LIMIT 1",
        {profile_hash}
    );
    if (PQntuples(existing.get()) == 0 || NormalizeRole(PQgetvalue(existing.get(), 0, 0)) != "moderator") {
        return "";
    }

    const std::string current_badge = TrimCopy(PQgetvalue(existing.get(), 0, 1));
    if (!current_badge.empty()) {
        return current_badge;
    }

    PGresultPtr max_badge = Query(
        "SELECT COALESCE(MAX(CASE "
        "WHEN moderator_badge ~ '^MOD-[0-9]+$' "
        "THEN CAST(SUBSTRING(moderator_badge FROM 5) AS INTEGER) "
        "ELSE 0 END), 0) "
        "FROM profiles"
    );
    const int next_badge = std::stoi(PQgetvalue(max_badge.get(), 0, 0)) + 1;
    const std::string assigned_badge = "MOD-" + std::to_string(next_badge);

    QueryParams(
        "UPDATE profiles SET moderator_badge = $1 WHERE pub_key_hash = $2",
        {assigned_badge, profile_hash}
    );
    return assigned_badge;
}

std::string DBManager::GetRoleBadge(const std::string& profile_hash, const std::string& role) {
    if (profile_hash.empty()) {
        return "";
    }

    std::string normalized_role = NormalizeRole(role);
    if (normalized_role == "user") {
        PGresultPtr role_res = QueryParams(
            "SELECT COALESCE(role, 'user') FROM profiles WHERE pub_key_hash = $1 LIMIT 1",
            {profile_hash}
        );
        if (PQntuples(role_res.get()) > 0) {
            normalized_role = NormalizeRole(PQgetvalue(role_res.get(), 0, 0));
        }
    }

    if (normalized_role == "founder") {
        return "FOUNDER";
    }
    if (normalized_role == "moderator") {
        return EnsureModeratorBadge(profile_hash);
    }
    return "USER";
}

bool DBManager::IsProfileBanned(const std::string& profile_hash, std::string* reason) {
    if (reason) {
        *reason = "";
    }
    if (profile_hash.empty()) {
        return false;
    }

    std::string salt = Config::Instance().Get().server_salt;
    std::string hashed_target = sha256_hex(profile_hash + salt);

    PGresultPtr res = QueryParams(
        "SELECT COALESCE(reason, '') FROM bans "
        "WHERE target_identifier = $1 AND ban_type = 'identity' AND (expires_at IS NULL OR expires_at > NOW()) LIMIT 1",
        {hashed_target}
    );
    if (PQntuples(res.get()) == 0) {
        return false;
    }

    if (reason) {
        const std::string stored_reason = TrimCopy(PQgetvalue(res.get(), 0, 0));
        *reason = stored_reason.empty() ? "This identity is banned from moderation-protected actions" : stored_reason;
    }
    return true;
}

bool DBManager::IsIpBanned(const std::string& ip_address, std::string* reason) {
    if (reason) {
        *reason = "";
    }
    if (ip_address.empty()) {
        return false;
    }

    std::string salt = Config::Instance().Get().server_salt;
    std::string hashed_target = sha256_hex(ip_address + salt);

    PGresultPtr res = QueryParams(
        "SELECT COALESCE(reason, '') FROM bans "
        "WHERE target_identifier = $1 AND ban_type = 'ip' AND (expires_at IS NULL OR expires_at > NOW()) LIMIT 1",
        {hashed_target}
    );
    if (PQntuples(res.get()) == 0) {
        return false;
    }

    if (reason) {
        const std::string stored_reason = TrimCopy(PQgetvalue(res.get(), 0, 0));
        *reason = stored_reason.empty() ? "Your IP address is banned" : stored_reason;
    }
    return true;
}

void DBManager::CreateNotification(const std::string& user_hash, const std::string& actor_hash,
                                   const std::string& type, const std::string& title,
                                   const std::string& body, const std::string& link) {
    if (user_hash.empty() || user_hash == "admin") {
        return;
    }

    QueryParams(
        "INSERT INTO notification_queue (user_hash, actor_hash, type, title, body, link) "
        "VALUES ($1, $2, $3, $4, $5, $6)",
        {user_hash, actor_hash, type, title, body, link}
    );
}

void DBManager::StartNotificationWorker() {
    std::thread([this]() {
        Logger::Info("Notification queue worker thread started.");
        while (true) {
            try {
                PGresultPtr res;
                {
                    std::lock_guard<decltype(mutex_)> lock(mutex_);
                    res = Query(
                        "SELECT id, user_hash, actor_hash, type, title, body, link, retry_count "
                        "FROM notification_queue "
                        "WHERE status = 'pending' AND next_retry_at <= NOW() "
                        "ORDER BY created_at ASC LIMIT 1 FOR UPDATE SKIP LOCKED"
                    );
                }

                if (PQntuples(res.get()) > 0) {
                    std::string q_id = PQgetvalue(res.get(), 0, 0);
                    std::string user_hash = PQgetvalue(res.get(), 0, 1);
                    std::string actor_hash = PQgetvalue(res.get(), 0, 2);
                    std::string type = PQgetvalue(res.get(), 0, 3);
                    std::string title = PQgetvalue(res.get(), 0, 4);
                    std::string body = PQgetvalue(res.get(), 0, 5);
                    std::string link = PQgetvalue(res.get(), 0, 6);
                    int retry_count = std::stoi(PQgetvalue(res.get(), 0, 7));

                    bool success = false;
                    try {
                        std::lock_guard<decltype(mutex_)> lock(mutex_);
                        QueryParams(
                            "INSERT INTO notifications (user_hash, actor_hash, type, title, body, link) "
                            "VALUES ($1, $2, $3, $4, $5, $6)",
                            {user_hash, actor_hash, type, title, body, link}
                        );
                        QueryParams(
                            "UPDATE notification_queue SET status = 'completed' WHERE id = $1",
                            {q_id}
                        );
                        success = true;
                    } catch (const std::exception& e) {
                        Logger::Error("Notification delivery insert failed: " + std::string(e.what()));
                    }

                    if (!success) {
                        int next_retry = retry_count + 1;
                        int backoff = 1 << next_retry;
                        std::lock_guard<decltype(mutex_)> lock(mutex_);
                        QueryParams(
                            "UPDATE notification_queue "
                            "SET retry_count = $2, next_retry_at = NOW() + ($3 || ' seconds')::INTERVAL, "
                            "    status = CASE WHEN $2 >= 5 THEN 'failed' ELSE 'pending' END "
                            "WHERE id = $1",
                            {q_id, std::to_string(next_retry), std::to_string(backoff)}
                        );
                    }
                }
            } catch (const std::exception& e) {
                Logger::Error("Notification queue worker exception: " + std::string(e.what()));
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();
}

json DBManager::UpdateProfileSubscription(const std::string& user_hash, const std::string& tier, int duration_days) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string resolved = ResolveProfileHash(user_hash);
    if (resolved.empty()) {
        return {{"error", "Profile not found"}};
    }

    QueryParams(
        "UPDATE profiles SET subscription_tier = $2, subscription_expires_at = NOW() + ($3 || ' days')::INTERVAL "
        "WHERE pub_key_hash = $1",
        {resolved, tier, std::to_string(duration_days)}
    );

    std::string key = "";
    if (tier == "hermes" || tier == "circle") {
        key = "qc_" + GenerateRandomHex(24);
        QueryParams(
            "INSERT INTO api_keys (user_hash, api_key, tier) VALUES ($1, $2, $3) "
            "ON CONFLICT (user_hash) DO UPDATE SET api_key = EXCLUDED.api_key, tier = EXCLUDED.tier",
            {resolved, key, tier}
        );
    }

    return {{"status", "success"}, {"subscription_tier", tier}, {"api_key", key}};
}

bool DBManager::ValidateApiKey(const std::string& api_key, std::string& user_hash, std::string& tier) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    if (api_key.empty()) return false;

    PGresultPtr res = QueryParams(
        "SELECT user_hash, tier FROM api_keys WHERE api_key = $1 LIMIT 1",
        {api_key}
    );
    if (PQntuples(res.get()) == 0) return false;

    user_hash = PQgetvalue(res.get(), 0, 0);
    tier = PQgetvalue(res.get(), 0, 1);

    PGresultPtr sub_res = QueryParams(
        "SELECT (subscription_expires_at IS NULL OR subscription_expires_at > NOW()) FROM profiles "
        "WHERE pub_key_hash = $1 LIMIT 1",
        {user_hash}
    );
    if (PQntuples(sub_res.get()) > 0) {
        return std::string(PQgetvalue(sub_res.get(), 0, 0)) == "t";
    }
    return false;
}

bool DBManager::AddProfileTag(const std::string& user_hash, const std::string& tag_name) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string resolved = ResolveProfileHash(user_hash);
    if (resolved.empty()) return false;

    PGresultPtr res = QueryParams("SELECT COALESCE(unlocked_tags, '') FROM profiles WHERE pub_key_hash = $1", {resolved});
    if (PQntuples(res.get()) == 0) return false;

    std::string current_tags = PQgetvalue(res.get(), 0, 0);
    std::string new_tags = current_tags;
    
    bool exists = false;
    std::string comma_tag = "," + tag_name + ",";
    std::string padded_tags = "," + current_tags + ",";
    if (padded_tags.find(comma_tag) != std::string::npos || current_tags == tag_name) {
        exists = true;
    }

    if (!exists) {
        if (!new_tags.empty()) {
            new_tags += ",";
        }
        new_tags += tag_name;
    }

    QueryParams(
        "UPDATE profiles SET unlocked_tags = $2, custom_badge = $3 WHERE pub_key_hash = $1",
        {resolved, new_tags, tag_name}
    );
    return true;
}

bool DBManager::SetProfileActiveTag(const std::string& user_hash, const std::string& tag_name) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string resolved = ResolveProfileHash(user_hash);
    if (resolved.empty()) return false;

    if (tag_name.empty()) {
        QueryParams(
            "UPDATE profiles SET custom_badge = '' WHERE pub_key_hash = $1",
            {resolved}
        );
        return true;
    }

    PGresultPtr res = QueryParams("SELECT COALESCE(unlocked_tags, '') FROM profiles WHERE pub_key_hash = $1", {resolved});
    if (PQntuples(res.get()) == 0) return false;

    std::string current_tags = PQgetvalue(res.get(), 0, 0);
    std::string comma_tag = "," + tag_name + ",";
    std::string padded_tags = "," + current_tags + ",";
    bool exists = (padded_tags.find(comma_tag) != std::string::npos || current_tags == tag_name);

    if (!exists) {
        return false;
    }

    QueryParams(
        "UPDATE profiles SET custom_badge = $2 WHERE pub_key_hash = $1",
        {resolved, tag_name}
    );
    return true;
}

std::string DBManager::GenerateRandomHex(int len) {
    std::vector<unsigned char> buf(len / 2);
    if (RAND_bytes(buf.data(), buf.size()) != 1) {
        throw std::runtime_error("OpenSSL RAND_bytes failed");
    }
    std::stringstream ss;
    for (unsigned char c : buf) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    }
    return ss.str();
}

json DBManager::CreateGroup(const std::string& name, const std::string& creator_hash, const std::string& encrypted_key) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string creator = ResolveProfileHash(creator_hash);
    if (creator.empty()) {
        return {{"error", "Creator profile not found"}};
    }

    PGresultPtr tier_res = QueryParams(
        "SELECT subscription_tier, (subscription_expires_at IS NULL OR subscription_expires_at > NOW()) "
        "FROM profiles WHERE pub_key_hash = $1 LIMIT 1",
        {creator}
    );
    bool is_subscribed = false;
    if (PQntuples(tier_res.get()) > 0) {
        std::string tier = PQgetvalue(tier_res.get(), 0, 0);
        std::string active = PQgetvalue(tier_res.get(), 0, 1);
        if ((tier == "circle" || tier == "hermes") && active == "t") {
            is_subscribed = true;
        }
    }

    if (!is_subscribed) {
        return {{"error", "Active Circle or Hermes subscription tier required to create group rooms"}};
    }

    PGresultPtr res = QueryParams(
        "INSERT INTO groups (name, created_by) VALUES ($1, $2) RETURNING id",
        {name, creator}
    );
    if (PQntuples(res.get()) == 0) {
        return {{"error", "Failed to create group"}};
    }
    std::string group_id = PQgetvalue(res.get(), 0, 0);

    QueryParams(
        "INSERT INTO group_members (group_id, user_hash, encrypted_group_key, role) "
        "VALUES ($1, $2, $3, 'admin')",
        {group_id, creator, encrypted_key}
    );

    return {{"status", "success"}, {"group_id", group_id}};
}

json DBManager::JoinGroup(const std::string& group_id, const std::string& user_hash, const std::string& encrypted_key) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string user = ResolveProfileHash(user_hash);
    if (user.empty()) {
        return {{"error", "User profile not found"}};
    }

    PGresultPtr gp_res = QueryParams("SELECT id FROM groups WHERE id = $1", {group_id});
    if (PQntuples(gp_res.get()) == 0) {
        return {{"error", "Group not found"}};
    }

    QueryParams(
        "INSERT INTO group_members (group_id, user_hash, encrypted_group_key, role) "
        "VALUES ($1, $2, $3, 'member') ON CONFLICT (group_id, user_hash) DO UPDATE "
        "SET encrypted_group_key = EXCLUDED.encrypted_group_key",
        {group_id, user, encrypted_key}
    );

    return {{"status", "success"}};
}

json DBManager::GetGroupMessages(const std::string& group_id, const std::string& actor_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty()) {
        return {{"error", "Authorization failed"}};
    }

    PGresultPtr mem_res = QueryParams(
        "SELECT role FROM group_members WHERE group_id = $1 AND user_hash = $2",
        {group_id, actor}
    );
    if (PQntuples(mem_res.get()) == 0) {
        return {{"error", "Access denied: not a group member"}};
    }

    PGresultPtr msg_res = QueryParams(
        "SELECT m.id, m.sender_hash, m.encrypted_content, "
        "       COALESCE((EXTRACT(EPOCH FROM m.created_at) * 1000)::BIGINT::TEXT, '') "
        "FROM group_messages m WHERE m.group_id = $1 ORDER BY m.created_at ASC",
        {group_id}
    );

    json msgs = json::array();
    for (int i = 0; i < PQntuples(msg_res.get()); ++i) {
        json msg;
        msg["id"] = PQgetvalue(msg_res.get(), i, 0);
        msg["sender_hash"] = PQgetvalue(msg_res.get(), i, 1);
        msg["encrypted_content"] = PQgetvalue(msg_res.get(), i, 2);
        msg["created_at"] = PQgetvalue(msg_res.get(), i, 3);
        msgs.push_back(msg);
    }

    return {{"messages", msgs}};
}

json DBManager::SendGroupMessage(const std::string& group_id, const std::string& sender_hash, const std::string& encrypted_content) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string sender = ResolveProfileHash(sender_hash);
    if (sender.empty()) {
        return {{"error", "Sender profile not found"}};
    }

    PGresultPtr mem_res = QueryParams(
        "SELECT role FROM group_members WHERE group_id = $1 AND user_hash = $2",
        {group_id, sender}
    );
    if (PQntuples(mem_res.get()) == 0) {
        return {{"error", "Access denied: not a group member"}};
    }

    PGresultPtr res = QueryParams(
        "INSERT INTO group_messages (group_id, sender_hash, encrypted_content) VALUES ($1, $2, $3) RETURNING id",
        {group_id, sender, encrypted_content}
    );
    if (PQntuples(res.get()) == 0) {
        return {{"error", "Failed to send message"}};
    }

    return {{"status", "success"}, {"message_id", PQgetvalue(res.get(), 0, 0)}};
}

json DBManager::GetUserGroups(const std::string& actor_hash) {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty()) {
        return json::array();
    }

    PGresultPtr res = QueryParams(
        "SELECT g.id, g.name, g.created_by, m.encrypted_group_key, m.role "
        "FROM groups g JOIN group_members m ON g.id = m.group_id "
        "WHERE m.user_hash = $1 ORDER BY g.created_at DESC",
        {actor}
    );

    json list = json::array();
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        json gp;
        gp["group_id"] = PQgetvalue(res.get(), i, 0);
        gp["name"] = PQgetvalue(res.get(), i, 1);
        gp["created_by"] = PQgetvalue(res.get(), i, 2);
        gp["encrypted_key"] = PQgetvalue(res.get(), i, 3);
        gp["role"] = PQgetvalue(res.get(), i, 4);
        list.push_back(gp);
    }
    return list;
}

json DBManager::RotateGroupKeys(const std::string& group_id, const std::string& actor_hash, const json& new_keys) {
    ConnLease lease(*pool_);
    PGconn* conn = lease.get();
    if (!conn) return {{"error", "Database connection unavailable"}};

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty()) {
        return {{"error", "Authorization failed"}};
    }

    PGresultPtr admin_res = QueryParams(
        "SELECT role FROM group_members WHERE group_id = $1 AND user_hash = $2",
        {group_id, actor}, conn);
    if (PQntuples(admin_res.get()) == 0 || std::string(PQgetvalue(admin_res.get(), 0, 0)) != "admin") {
        return {{"error", "Access denied: only group admins can rotate keys"}};
    }

    if (!new_keys.is_array()) {
        return {{"error", "new_keys must be an array"}};
    }

    Execute("BEGIN", conn);
    try {
        for (const auto& item : new_keys) {
            std::string user_hash = item.value("user_hash", "");
            std::string encrypted_key = item.value("encrypted_key", "");
            if (user_hash.empty() || encrypted_key.empty()) continue;

            QueryParams(
                "UPDATE group_members SET encrypted_group_key = $3 "
                "WHERE group_id = $1 AND user_hash = $2",
                {group_id, user_hash, encrypted_key},
                conn
            );
        }
        Execute("COMMIT", conn);
    } catch (const std::exception& e) {
        Execute("ROLLBACK", conn);
        return {{"error", std::string("Rotation failed: ") + e.what()}};
    }

    return {{"status", "success"}};
}

void DBManager::CreateModerationEvent(const std::string& actor_hash, const std::string& action,
                                      const std::string& summary, const std::string& target_hash,
                                      int64_t report_id, int64_t target_post_id,
                                      int64_t target_thread_id, const std::string& target_board_id) {
    if (actor_hash.empty() || action.empty() || summary.empty()) {
        return;
    }

    QueryParams(
        "INSERT INTO moderation_events (actor_hash, actor_label, actor_badge, action, summary, target_hash, target_label, target_badge, report_id, target_post_id, target_thread_id, target_board_id) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, NULLIF($9, '0')::BIGINT, NULLIF($10, '0')::BIGINT, NULLIF($11, '0')::BIGINT, $12)",
        {
            actor_hash,
            ResolveProfileUsername(actor_hash),
            GetRoleBadge(actor_hash),
            action,
            summary,
            target_hash,
            target_hash.empty() ? "" : ResolveProfileUsername(target_hash),
            GetRoleBadge(target_hash),
            std::to_string(report_id),
            std::to_string(target_post_id),
            std::to_string(target_thread_id),
            target_board_id
        }
    );
}
