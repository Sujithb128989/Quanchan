#ifndef DB_MANAGER_HPP
#define DB_MANAGER_HPP

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <cstdint>
#include <libpq-fe.h>
#include <nlohmann/json.hpp>
#include "secure_storage.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// RAII wrapper for PGresult* — guarantees PQclear() even if exceptions are
// thrown during JSON parsing or any other operation.
// ---------------------------------------------------------------------------
struct PGresultDeleter {
    void operator()(PGresult* res) const noexcept {
        if (res) PQclear(res);
    }
};
using PGresultPtr = std::unique_ptr<PGresult, PGresultDeleter>;

class DBManager {
public:
    // conn_info: libpq connection string, e.g.
    //   "host=db port=5432 dbname=quanchan user=quanchan password=xxx"
    DBManager(const std::string& conn_info, SecureStorage& secure_storage);
    ~DBManager();

    // --- Original PQC Encrypted Store (gRPC compatible) ---
    int64_t InsertMessage(const std::string& message);
    std::string GetMessage(int64_t id);
    void ReEncryptAll();

    // --- Imageboard API ---

    // Boards
    json GetAllBoards();
    json GetBoard(const std::string& board_id);

    // Threads
    json GetThreads(const std::string& board_id, int page = 1, int limit = 20, bool archived = false);
    json GetThread(int64_t thread_id);
    json CreateThread(const std::string& board_id, const std::string& subject,
                      const std::string& content, const std::string& name,
                      const std::string& image_url, const std::string& encrypted_content,
                      const std::string& author_hash = "");
    void ArchiveThread(int64_t thread_id);

    // Posts
    json CreatePost(int64_t thread_id, const std::string& content,
                    const std::string& name, const std::string& image_url,
                    const std::string& encrypted_content, bool sage,
                    const std::string& author_hash = "");

    // Stats
    json GetStats();

    // Social Layer (V4)
    json GetProfile(const std::string& pub_key_hash);
    void UpdateProfile(const std::string& pub_key_hash, const std::string& username,
                       const std::string& pqc_kem_public_key = "",
                       const std::string& identity_public_key = "",
                       const std::string& pqc_identity_public_key = "",
                       const std::string& pqc_identity_scheme = "",
                       const std::string& identity_binding_payload = "",
                       const std::string& identity_binding_signature = "",
                       const std::string& recovery_lookup_hash = "",
                       const std::string& recovery_bundle_ciphertext = "",
                       const std::string& recovery_bundle_iv = "");
    json GetRecoveryBundle(const std::string& recovery_lookup_hash);
    json ClaimFounderRole(const std::string& pub_key_hash, const std::string& founder_session_hash);
    json SetProfileRole(const std::string& actor_hash, const std::string& founder_session_hash,
                        const std::string& target_hash, const std::string& role);
    json InteractPost(int64_t post_id, const std::string& pub_key_hash, int type); // 1 = like, -1 = dislike
    json SendFriendRequest(const std::string& sender_hash, const std::string& receiver_hash);
    json AcceptFriendRequest(const std::string& sender_hash, const std::string& receiver_hash);
    json RejectFriendRequest(const std::string& sender_hash, const std::string& receiver_hash);
    json CancelFriendRequest(const std::string& sender_hash, const std::string& receiver_hash);
    json RemoveFriend(const std::string& user_hash, const std::string& peer_hash);
    json GetFriends(const std::string& pub_key_hash);
    json BlockUser(const std::string& blocker_hash, const std::string& blocked_hash);
    json UnblockUser(const std::string& blocker_hash, const std::string& blocked_hash);
    json CreateDirectMessage(const std::string& sender_hash, const std::string& receiver_hash,
                             const std::string& content, const std::string& image_url);
    json GetDirectMessages(const std::string& user_hash, const std::string& peer_hash);
    json GetDirectMessageInbox(const std::string& user_hash);
    json RespondToMessageRequest(const std::string& actor_hash, const std::string& requester_hash, const std::string& action);
    json GetNotifications(const std::string& user_hash, int limit = 50);
    json MarkNotificationsRead(const std::string& user_hash, const std::string& notification_id = "");
    json GetNotificationSummary(const std::string& user_hash);
    json CreateReport(const std::string& reporter_hash, const std::string& target_hash, const std::string& reason,
                      const std::string& target_kind = "user", int64_t target_post_id = 0,
                      int64_t target_thread_id = 0, const std::string& target_board_id = "",
                      const std::string& target_display_name = "", const std::string& context_link = "");
    json GetModerationReports(const std::string& actor_hash, const std::string& founder_session_hash, int limit = 50);
    json GetModerationAudit(const std::string& actor_hash, const std::string& founder_session_hash, int limit = 50);
    json BanUserAsModerator(const std::string& actor_hash, const std::string& founder_session_hash,
                            const std::string& target_hash, const std::string& reason);
    json UnbanUserAsModerator(const std::string& actor_hash, const std::string& founder_session_hash,
                              const std::string& target_hash);
    json ResolveModerationReport(const std::string& actor_hash, const std::string& founder_session_hash,
                                 int64_t report_id, const std::string& status, const std::string& note = "");
    json DeletePostAsModerator(const std::string& actor_hash, const std::string& founder_session_hash, int64_t post_id);

    // Seed default boards
    void SeedBoards();

private:
    PGconn* conn_;
    std::string conn_info_;
    SecureStorage& secure_storage_;
    std::recursive_mutex mutex_;

    void Init();
    void EnsureConnected();
    void Reconnect();

    // Execute SQL that returns no rows. Throws on error.
    void Execute(const std::string& sql);

    // Execute SQL and return an RAII-wrapped result. Throws on error.
    PGresultPtr Query(const std::string& sql);

    // Execute a parameterized query. Throws on error.
    PGresultPtr QueryParams(const std::string& sql,
                            const std::vector<std::string>& params);

    // Helper
    std::string GetBoardIdForThread(int64_t thread_id);
    std::string ResolveProfileHash(const std::string& value);
    std::string ResolveExistingProfileHash(const std::string& value, bool allow_admin = false);
    std::string ResolveProfileUsername(const std::string& value);
    std::string EnsureModeratorBadge(const std::string& profile_hash);
    std::string GetRoleBadge(const std::string& profile_hash, const std::string& role = "");
    bool IsProfileBanned(const std::string& profile_hash, std::string* reason = nullptr);
    bool HasAcceptedFriendship(const std::string& left_hash, const std::string& right_hash);
    bool HasAcceptedMessageChannel(const std::string& left_hash, const std::string& right_hash);
    std::string GetMessageRequestStatus(const std::string& requester_hash, const std::string& recipient_hash);
    bool IsBlockedEitherDirection(const std::string& left_hash, const std::string& right_hash, std::string* blocker_hash = nullptr);
    bool IsModeratorAuthorized(const std::string& actor_hash, const std::string& founder_session_hash, bool founder_only = false);
    void CreateNotification(const std::string& user_hash, const std::string& actor_hash,
                            const std::string& type, const std::string& title,
                            const std::string& body, const std::string& link = "");
    void CreateModerationEvent(const std::string& actor_hash, const std::string& action,
                               const std::string& summary, const std::string& target_hash = "",
                               int64_t report_id = 0, int64_t target_post_id = 0,
                               int64_t target_thread_id = 0, const std::string& target_board_id = "");
};

#endif // DB_MANAGER_HPP
