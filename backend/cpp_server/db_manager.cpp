#include "db_manager.hpp"
#include "logger.hpp"
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <unordered_set>

namespace {

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

DBManager::DBManager(const std::string& conn_info, SecureStorage& secure_storage)
    : conn_(nullptr), conn_info_(conn_info), secure_storage_(secure_storage) {
    conn_ = PQconnectdb(conn_info_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string err = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("PostgreSQL connection failed: " + err);
    }
    Logger::Info("Connected to PostgreSQL.");
    Init();
}

DBManager::~DBManager() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

// =============================================================================
// Internal Helpers (all return RAII-wrapped PGresultPtr)
// =============================================================================

void DBManager::Reconnect() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }

    Logger::Warn("PostgreSQL connection lost. Reconnecting...");
    conn_ = PQconnectdb(conn_info_.c_str());
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        const std::string err = conn_ ? PQerrorMessage(conn_) : "PQconnectdb returned null";
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
        throw std::runtime_error("PostgreSQL reconnect failed: " + err);
    }

    Logger::Info("Reconnected to PostgreSQL.");
}

void DBManager::EnsureConnected() {
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        Reconnect();
    }
}

void DBManager::Execute(const std::string& sql) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        EnsureConnected();

        PGresultPtr res(PQexec(conn_, sql.c_str()));
        ExecStatusType status = res ? PQresultStatus(res.get()) : PGRES_FATAL_ERROR;
        if (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK) {
            return;
        }

        const bool should_retry = conn_ && PQstatus(conn_) != CONNECTION_OK && attempt == 0;
        const std::string err = conn_ ? PQerrorMessage(conn_) : "null PostgreSQL connection";
        if (!should_retry) {
            throw std::runtime_error("SQL Execute error: " + err + " | SQL: " + sql);
        }

        Reconnect();
    }
}

PGresultPtr DBManager::Query(const std::string& sql) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        EnsureConnected();

        PGresultPtr res(PQexec(conn_, sql.c_str()));
        ExecStatusType status = res ? PQresultStatus(res.get()) : PGRES_FATAL_ERROR;
        if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) {
            return res;
        }

        const bool should_retry = conn_ && PQstatus(conn_) != CONNECTION_OK && attempt == 0;
        const std::string err = conn_ ? PQerrorMessage(conn_) : "null PostgreSQL connection";
        if (!should_retry) {
            throw std::runtime_error("SQL Query error: " + err + " | SQL: " + sql);
        }

        Reconnect();
    }

    throw std::runtime_error("SQL Query error: retry loop exhausted | SQL: " + sql);
}

PGresultPtr DBManager::QueryParams(const std::string& sql,
                                    const std::vector<std::string>& params) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        EnsureConnected();

        std::vector<const char*> values;
        values.reserve(params.size());
        for (const auto& p : params) {
            values.push_back(p.c_str());
        }

        PGresultPtr res(PQexecParams(conn_, sql.c_str(),
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

        const bool should_retry = conn_ && PQstatus(conn_) != CONNECTION_OK && attempt == 0;
        const std::string err = conn_ ? PQerrorMessage(conn_) : "null PostgreSQL connection";
        if (!should_retry) {
            throw std::runtime_error("SQL QueryParams error: " + err + " | SQL: " + sql);
        }

        Reconnect();
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

    Execute(
        "CREATE TABLE IF NOT EXISTS bans ("
        "  target_hash TEXT PRIMARY KEY,"
        "  reason TEXT NOT NULL DEFAULT '',"
        "  banned_by_hash TEXT NOT NULL,"
        "  banned_by_label TEXT DEFAULT '',"
        "  banned_by_badge TEXT DEFAULT '',"
        "  created_at TIMESTAMPTZ DEFAULT NOW(),"
        "  updated_at TIMESTAMPTZ DEFAULT NOW()"
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
    Execute("CREATE INDEX IF NOT EXISTS idx_notifications_user ON notifications(user_hash, read_at, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_reports_target ON reports(target_hash, status, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_reports_post ON reports(target_post_id, status, created_at DESC);");
    Execute("CREATE INDEX IF NOT EXISTS idx_bans_target ON bans(target_hash);");
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
}

// =============================================================================
// Original PQC Encrypted Store (gRPC-compatible)
// =============================================================================

int64_t DBManager::InsertMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string encrypted = secure_storage_.Encrypt(message);

    // Use PQexecParams with binary data for the BYTEA blob
    const char* paramValues[1] = { encrypted.data() };
    int paramLengths[1] = { static_cast<int>(encrypted.size()) };
    int paramFormats[1] = { 1 }; // binary

    PGresultPtr res(PQexecParams(conn_,
        "INSERT INTO messages (data) VALUES ($1) RETURNING id",
        1, nullptr, paramValues, paramLengths, paramFormats, 0));

    if (PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
        throw std::runtime_error("InsertMessage failed: " + std::string(PQerrorMessage(conn_)));
    }

    return std::stoll(PQgetvalue(res.get(), 0, 0));
}

std::string DBManager::GetMessage(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);
    Logger::Info("Starting database re-encryption...");

    // 1. Read all data
    PGresultPtr select_res = Query("SELECT id, data FROM messages");
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

    Execute("BEGIN");
    try {
        for (const auto& row : encrypted_rows) {
            const char* paramValues[2];
            std::string id_str = std::to_string(row.first);
            paramValues[0] = row.second.data();
            paramValues[1] = id_str.c_str();
            int paramLengths[2] = { static_cast<int>(row.second.size()), 0 };
            int paramFormats[2] = { 1, 0 }; // blob binary, id text

            PGresultPtr upd(PQexecParams(conn_,
                "UPDATE messages SET data = $1 WHERE id = $2",
                2, nullptr, paramValues, paramLengths, paramFormats, 0));
            if (PQresultStatus(upd.get()) != PGRES_COMMAND_OK) {
                throw std::runtime_error("Failed to update row " + id_str);
            }
        }
        Execute("COMMIT");
    } catch (...) {
        Execute("ROLLBACK");
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
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
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
            "SELECT id, content, encrypted_content, is_encrypted, image_url, name, created_at "
            "FROM posts WHERE thread_id = $1 AND is_op = TRUE LIMIT 1", {tid});

        if (PQntuples(op_res.get()) > 0) {
            json op;
            op["id"]               = std::stoll(PQgetvalue(op_res.get(), 0, 0));
            op["content"]          = PQgetvalue(op_res.get(), 0, 1);
            op["encryptedContent"] = PQgetisnull(op_res.get(), 0, 2) ? nullptr : json(PQgetvalue(op_res.get(), 0, 2));
            op["isEncrypted"]      = std::string(PQgetvalue(op_res.get(), 0, 3)) == "t";
            op["imageUrl"]         = PQgetisnull(op_res.get(), 0, 4) ? nullptr : json(PQgetvalue(op_res.get(), 0, 4));
            op["name"]             = PQgetvalue(op_res.get(), 0, 5);
            op["createdAt"]        = PQgetvalue(op_res.get(), 0, 6);
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
    std::lock_guard<std::mutex> lock(mutex_);
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
        "SELECT id, content, encrypted_content, is_encrypted, image_url, name, "
        "  tripcode, sage, is_op, created_at "
        "FROM posts WHERE thread_id = $1 ORDER BY created_at ASC", {tid});

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
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string author = ResolveExistingProfileHash(author_hash);
    std::string ban_reason;
    if (!author.empty() && IsProfileBanned(author, &ban_reason)) {
        throw std::runtime_error(ban_reason.empty() ? "This identity is banned from posting" : ban_reason);
    }

    std::string subj = subject.empty() ? "No Subject" : subject;
    PGresultPtr thread_res = QueryParams(
        "INSERT INTO threads (board_id, subject) VALUES ($1, $2) RETURNING id",
        {board_id, subj});
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

    PGresultPtr post_res(PQexecParams(conn_,
        "INSERT INTO posts (thread_id, board_id, content, encrypted_content, is_encrypted, image_url, name, author_hash, is_op) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, COALESCE($8, ''), $9) RETURNING id",
        9, nullptr, pv, nullptr, nullptr, 0));

    if (PQresultStatus(post_res.get()) != PGRES_TUPLES_OK) {
        throw std::runtime_error("Failed to insert OP post: " + std::string(PQerrorMessage(conn_)));
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
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);

    std::string bid = GetBoardIdForThread(thread_id);
    if (bid.empty()) throw std::runtime_error("Thread not found");
    const std::string author = ResolveExistingProfileHash(author_hash);
    std::string ban_reason;
    if (!author.empty() && IsProfileBanned(author, &ban_reason)) {
        throw std::runtime_error(ban_reason.empty() ? "This identity is banned from posting" : ban_reason);
    }

    // Check locked
    std::string tid = std::to_string(thread_id);
    PGresultPtr lock_res = QueryParams("SELECT locked FROM threads WHERE id = $1", {tid});
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

    PGresultPtr res(PQexecParams(conn_,
        "INSERT INTO posts (thread_id, board_id, content, encrypted_content, is_encrypted, image_url, name, sage, author_hash) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, COALESCE($9, '')) RETURNING id",
        9, nullptr, pv, nullptr, nullptr, 0));

    if (PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
        throw std::runtime_error("Failed to insert post: " + std::string(PQerrorMessage(conn_)));
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
    std::lock_guard<std::mutex> lock(mutex_);
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
            "       COALESCE((EXTRACT(EPOCH FROM role_assigned_at) * 1000)::BIGINT::TEXT, '') "
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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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

json DBManager::SetProfileRole(const std::string& actor_hash, const std::string& founder_session_hash,
                               const std::string& target_hash, const std::string& role) {
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

    std::string me = ResolveExistingProfileHash(user_hash, true);
    if (me.empty()) {
        return {
            {"conversations", json::array()},
            {"received_requests", json::array()},
            {"sent_requests", json::array()}
        };
    }

    PGresultPtr res = QueryParams(
        "SELECT id, sender_hash, receiver_hash, COALESCE(content, ''), COALESCE(image_url, ''), "
        "       (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT "
        "FROM direct_messages "
        "WHERE sender_hash = $1 OR receiver_hash = $1 "
        "ORDER BY created_at DESC, id DESC",
        {me}
    );

    std::unordered_set<std::string> seen;
    json conversations = json::array();
    int rows = PQntuples(res.get());
    for (int i = 0; i < rows; ++i) {
        std::string sender = PQgetvalue(res.get(), i, 1);
        std::string receiver = PQgetvalue(res.get(), i, 2);
        std::string peer = sender == me ? receiver : sender;

        if (seen.count(peer) > 0) {
            continue;
        }
        const bool accepted_channel = peer == "admin"
            || HasAcceptedFriendship(me, peer)
            || HasAcceptedMessageChannel(me, peer);
        if (!accepted_channel) {
            continue;
        }
        seen.insert(peer);

        const std::string stored_content = PQgetvalue(res.get(), i, 3);
        const std::string stored_image_url = PQgetvalue(res.get(), i, 4);
        std::string preview = BuildDirectMessagePreview(sender, receiver, stored_content, stored_image_url, secure_storage_);

        PGresultPtr unread_res = QueryParams(
            "SELECT COUNT(*) FROM direct_messages WHERE sender_hash = $1 AND receiver_hash = $2 AND read_at IS NULL",
            {peer, me}
        );
        const int unread_count = std::stoi(PQgetvalue(unread_res.get(), 0, 0));

        conversations.push_back({
            {"hash", peer},
            {"username", ResolveProfileUsername(peer)},
            {"lastMessage", preview},
            {"lastTimestamp", std::stoll(PQgetvalue(res.get(), i, 5))},
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

    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

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

json DBManager::BanUserAsModerator(const std::string& actor_hash, const std::string& founder_session_hash,
                                   const std::string& target_hash, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    const std::string target = ResolveExistingProfileHash(target_hash);
    const std::string trimmed_reason = TrimCopy(reason);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_hash)) {
        return {{"error", "Moderator authorization failed"}};
    }
    if (target.empty()) {
        return {{"error", "Target profile not found"}};
    }

    PGresultPtr target_role = QueryParams(
        "SELECT COALESCE(role, 'user') FROM profiles WHERE pub_key_hash = $1 LIMIT 1",
        {target}
    );
    const std::string normalized_target_role = PQntuples(target_role.get()) > 0
        ? NormalizeRole(PQgetvalue(target_role.get(), 0, 0))
        : "user";
    if (normalized_target_role == "founder" || normalized_target_role == "moderator") {
        return {{"error", "Moderator identities cannot be banned from the UI"}};
    }

    QueryParams(
        "INSERT INTO bans (target_hash, reason, banned_by_hash, banned_by_label, banned_by_badge, created_at, updated_at) "
        "VALUES ($1, $2, $3, $4, $5, NOW(), NOW()) "
        "ON CONFLICT (target_hash) DO UPDATE SET "
        "reason = EXCLUDED.reason, banned_by_hash = EXCLUDED.banned_by_hash, "
        "banned_by_label = EXCLUDED.banned_by_label, banned_by_badge = EXCLUDED.banned_by_badge, updated_at = NOW()",
        {target, trimmed_reason, actor, ResolveProfileUsername(actor), GetRoleBadge(actor)}
    );

    CreateNotification(target, actor, "moderation_ban", "Account restricted",
                       trimmed_reason.empty() ? "Your identity has been restricted by moderation." : trimmed_reason,
                       "/u/" + target);
    CreateModerationEvent(actor, "ban_user",
                          "Banned " + ResolveProfileUsername(target) + ".",
                          target);
    return {{"status", "banned"}, {"target_hash", target}};
}

json DBManager::UnbanUserAsModerator(const std::string& actor_hash, const std::string& founder_session_hash,
                                     const std::string& target_hash) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    const std::string target = ResolveExistingProfileHash(target_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_hash)) {
        return {{"error", "Moderator authorization failed"}};
    }
    if (target.empty()) {
        return {{"error", "Target profile not found"}};
    }

    QueryParams("DELETE FROM bans WHERE target_hash = $1", {target});
    CreateNotification(target, actor, "moderation_unban", "Account restriction lifted",
                       "Your identity is no longer restricted by moderation.",
                       "/u/" + target);
    CreateModerationEvent(actor, "unban_user",
                          "Lifted moderation restrictions for " + ResolveProfileUsername(target) + ".",
                          target);
    return {{"status", "unbanned"}, {"target_hash", target}};
}

json DBManager::DeletePostAsModerator(const std::string& actor_hash, const std::string& founder_session_hash, int64_t post_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty() || !IsModeratorAuthorized(actor, founder_session_hash)) {
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

bool DBManager::IsModeratorAuthorized(const std::string& actor_hash, const std::string& founder_session_hash, bool founder_only) {
    const std::string actor = ResolveProfileHash(actor_hash);
    if (actor.empty()) {
        return false;
    }

    PGresultPtr actor_res = QueryParams(
        "SELECT COALESCE(role, 'user'), COALESCE(founder_session_hash, '') "
        "FROM profiles WHERE pub_key_hash = $1 LIMIT 1",
        {actor}
    );
    if (PQntuples(actor_res.get()) == 0) {
        return false;
    }

    const std::string actor_role = NormalizeRole(PQgetvalue(actor_res.get(), 0, 0));
    const std::string stored_founder_session_hash = PQgetvalue(actor_res.get(), 0, 1);
    if (actor_role == "founder") {
        return !founder_session_hash.empty() && stored_founder_session_hash == founder_session_hash;
    }
    if (founder_only) {
        return false;
    }
    return actor_role == "moderator";
}

void DBManager::CreateNotification(const std::string& user_hash, const std::string& actor_hash,
                                   const std::string& type, const std::string& title,
                                   const std::string& body, const std::string& link) {
    if (user_hash.empty() || user_hash == "admin") {
        return;
    }

    QueryParams(
        "INSERT INTO notifications (user_hash, actor_hash, type, title, body, link) "
        "VALUES ($1, $2, $3, $4, $5, $6)",
        {user_hash, actor_hash, type, title, body, link}
    );
}

// =============================================================================
// Board Seeding
// =============================================================================

void DBManager::SeedBoards() {
    PGresultPtr res = Query("SELECT COUNT(*) FROM boards");
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
        PGresultPtr ins(PQexecParams(conn_,
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

    PGresultPtr res = QueryParams(
        "SELECT COALESCE(reason, '') FROM bans WHERE target_hash = $1 LIMIT 1",
        {profile_hash}
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
