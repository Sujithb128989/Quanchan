import { create } from 'zustand';
import type { AppState, Thread, Post } from '../types';

const API_BASE = '/api';
const POST_CREATED_EVENT = 'quanchan:post-created';

// Backend already returns camelCase keys. Map to frontend types.
function mapBackendPost(raw: any, boardId: string, threadId?: number): Post {
    return {
        id: Number(raw.id) || 0,
        no: Number(raw.id) || 0,
        threadId: threadId || Number(raw.threadId) || 0,
        boardId: String(raw.boardId || boardId),
        content: String(raw.content || ''),
        encryptedContent: raw.encryptedContent || undefined,
        isEncrypted: Boolean(raw.isEncrypted),
        imageUrl: raw.imageUrl || undefined,
        timestamp: raw.createdAt ? new Date(raw.createdAt).getTime() : Date.now(),
        name: String(raw.name || 'Anonymous'),
        tripcode: raw.tripcode || undefined,
        sage: Boolean(raw.sage),
        replies: [],
    };
}

function mapBackendThread(raw: any, boardId: string): Thread {
    const threadId = Number(raw.id) || 0;
    const op = raw.op ? mapBackendPost(raw.op, boardId, threadId) : {
        id: 0, no: 0, threadId, boardId, content: '', isEncrypted: false,
        timestamp: Date.now(), name: 'Anonymous', sage: false, replies: []
    } as Post;

    const replies = Array.isArray(raw.replies)
        ? raw.replies.map((r: any) => mapBackendPost(r, boardId, threadId))
        : [];

    return {
        id: threadId,
        boardId: String(raw.boardId || boardId),
        subject: String(raw.subject || 'No Subject'),
        op,
        replyCount: Number(raw.replyCount) || replies.length,
        imageCount: Number(raw.imageCount) || 0,
        lastBump: raw.lastBump ? new Date(raw.lastBump).getTime() : Date.now(),
        sticky: Boolean(raw.sticky),
        locked: Boolean(raw.locked),
        archived: Boolean(raw.archived),
        replies,
    };
}

interface LiveAppState extends AppState {
    fetchBoards: () => Promise<void>;
    fetchThreads: (boardId: string) => Promise<void>;
    
    // V4 Social API
    getProfile: (hash: string) => Promise<any>;
    updateProfile: (
        hash: string,
        username: string,
        pqcKemPublicKey?: string,
        identityPublicKey?: string,
        pqcIdentityPublicKey?: string,
        pqcIdentityScheme?: string,
        identityBindingPayload?: string,
        identityBindingSignature?: string,
        recoveryLookupHash?: string,
        recoveryBundleCiphertext?: string,
        recoveryBundleIv?: string
    ) => Promise<void>;
    getRecoveryBundle: (recoveryLookupHash: string) => Promise<any>;
    claimFounder: (hash: string, phrase: string) => Promise<{ role: string; founder_token: string }>;
    setProfileRole: (actorHash: string, founderToken: string, targetHash: string, role: 'user' | 'moderator') => Promise<any>;
    interactPost: (postId: number, hash: string, type: 1 | -1) => Promise<{likes: number, dislikes: number}>;
    sendFriendRequest: (senderHash: string, receiverHash: string) => Promise<void>;
    acceptFriendRequest: (senderHash: string, receiverHash: string) => Promise<void>;
    rejectFriendRequest: (senderHash: string, receiverHash: string) => Promise<void>;
    cancelFriendRequest: (senderHash: string, receiverHash: string) => Promise<void>;
    removeFriend: (userHash: string, peerHash: string) => Promise<void>;
    blockUser: (blockerHash: string, blockedHash: string) => Promise<void>;
    unblockUser: (blockerHash: string, blockedHash: string) => Promise<void>;
    getFriends: (hash: string) => Promise<{friends: any[], pending_received: any[], pending_sent: any[], blocked: any[]}>;
    respondToMessageRequest: (actorHash: string, requesterHash: string, action: 'accept' | 'decline' | 'block') => Promise<any>;
    getNotifications: (hash: string, limit?: number) => Promise<{ notifications: any[] }>;
    markNotificationsRead: (hash: string, notificationId?: string) => Promise<void>;
    getNotificationSummary: (hash: string) => Promise<{ notificationsUnread: number; friendRequestsPending: number; messageRequestsPending: number; dmUnread: number; total: number }>;
    createReport: (
        reporterHash: string,
        targetHash: string,
        reason: string,
        options?: {
            targetKind?: 'user' | 'post';
            targetPostId?: number;
            targetThreadId?: number;
            targetBoardId?: string;
            targetDisplayName?: string;
            contextLink?: string;
        }
    ) => Promise<any>;
    getModerationReports: (actorHash: string, founderToken?: string, limit?: number) => Promise<{ reports: any[] }>;
    getModerationAudit: (actorHash: string, founderToken?: string, limit?: number) => Promise<{ events: any[] }>;
    resolveModerationReport: (reportId: number, actorHash: string, status: 'open' | 'resolved' | 'dismissed', note?: string, founderToken?: string) => Promise<any>;
    deletePostAsModerator: (postId: number, actorHash: string, founderToken?: string) => Promise<any>;
    banUserAsModerator: (targetHash: string, actorHash: string, reason?: string, founderToken?: string) => Promise<any>;
    unbanUserAsModerator: (targetHash: string, actorHash: string, founderToken?: string) => Promise<any>;
}

export const useStore = create<LiveAppState>((set, get) => ({
    boards: [],
    threads: [],
    boardsState: 'idle',
    boardsError: '',
    threadsState: 'idle',
    threadsError: '',
    nextPostNo: 0,
    nextThreadId: 0,
    encryptionState: { enabled: false },
    quantumModalVisible: false,
    pendingPost: null,
    isPhoenixFiring: false,

    // V4 Social Implementations
    getProfile: async (hash: string) => {
        const res = await fetch(`${API_BASE}/profile/${encodeURIComponent(hash)}`);
        return res.ok ? res.json() : null;
    },
    updateProfile: async (
        hash: string,
        username: string,
        pqcKemPublicKey = '',
        identityPublicKey = '',
        pqcIdentityPublicKey = '',
        pqcIdentityScheme = '',
        identityBindingPayload = '',
        identityBindingSignature = '',
        recoveryLookupHash = '',
        recoveryBundleCiphertext = '',
        recoveryBundleIv = ''
    ) => {
        const res = await fetch(`${API_BASE}/profile/update`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                pub_key_hash: hash,
                username,
                pqc_kem_public_key: pqcKemPublicKey,
                identity_public_key: identityPublicKey,
                pqc_identity_public_key: pqcIdentityPublicKey,
                pqc_identity_scheme: pqcIdentityScheme,
                identity_binding_payload: identityBindingPayload,
                identity_binding_signature: identityBindingSignature,
                recovery_lookup_hash: recoveryLookupHash,
                recovery_bundle_ciphertext: recoveryBundleCiphertext,
                recovery_bundle_iv: recoveryBundleIv,
            })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Profile update failed');
        }
    },
    getRecoveryBundle: async (recoveryLookupHash: string) => {
        const res = await fetch(`${API_BASE}/identity/recover`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ recovery_lookup_hash: recoveryLookupHash }),
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Recovery lookup failed');
        }
        return data;
    },
    claimFounder: async (hash: string, phrase: string) => {
        const res = await fetch(`${API_BASE}/admin/claim-founder`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ pub_key_hash: hash, phrase }),
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Founder claim failed');
        }
        return data;
    },
    setProfileRole: async (actorHash: string, founderToken: string, targetHash: string, role: 'user' | 'moderator') => {
        const res = await fetch(`${API_BASE}/admin/roles`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                actor_hash: actorHash,
                founder_token: founderToken,
                target_hash: targetHash,
                role,
            }),
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Role update failed');
        }
        return data;
    },
    interactPost: async (postId: number, hash: string, type: 1 | -1) => {
        const res = await fetch(`${API_BASE}/interact`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ post_id: postId, pub_key_hash: hash, type })
        });
        return res.ok ? res.json() : { likes: 0, dislikes: 0 };
    },
    sendFriendRequest: async (senderHash: string, receiverHash: string) => {
        const res = await fetch(`${API_BASE}/friends/request`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ sender_hash: senderHash, receiver_hash: receiverHash })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Friend request failed');
        }
    },
    acceptFriendRequest: async (senderHash: string, receiverHash: string) => {
        const res = await fetch(`${API_BASE}/friends/accept`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ sender_hash: senderHash, receiver_hash: receiverHash })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Accepting friend request failed');
        }
    },
    rejectFriendRequest: async (senderHash: string, receiverHash: string) => {
        const res = await fetch(`${API_BASE}/friends/reject`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ sender_hash: senderHash, receiver_hash: receiverHash })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Rejecting friend request failed');
        }
    },
    cancelFriendRequest: async (senderHash: string, receiverHash: string) => {
        const res = await fetch(`${API_BASE}/friends/cancel`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ sender_hash: senderHash, receiver_hash: receiverHash })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Canceling friend request failed');
        }
    },
    removeFriend: async (userHash: string, peerHash: string) => {
        const res = await fetch(`${API_BASE}/friends/remove`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ user_hash: userHash, peer_hash: peerHash })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Removing friend failed');
        }
    },
    blockUser: async (blockerHash: string, blockedHash: string) => {
        const res = await fetch(`${API_BASE}/friends/block`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ blocker_hash: blockerHash, blocked_hash: blockedHash })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Blocking user failed');
        }
    },
    unblockUser: async (blockerHash: string, blockedHash: string) => {
        const res = await fetch(`${API_BASE}/friends/unblock`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ blocker_hash: blockerHash, blocked_hash: blockedHash })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Unblocking user failed');
        }
    },
    getFriends: async (hash: string) => {
        const res = await fetch(`${API_BASE}/friends/${encodeURIComponent(hash)}`);
        return res.ok ? res.json() : { friends: [], pending_received: [], pending_sent: [], blocked: [] };
    },
    respondToMessageRequest: async (actorHash: string, requesterHash: string, action: 'accept' | 'decline' | 'block') => {
        const res = await fetch(`${API_BASE}/messages/requests/respond`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ actor_hash: actorHash, requester_hash: requesterHash, action })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Updating message request failed');
        }
        return data;
    },
    getNotifications: async (hash: string, limit = 50) => {
        const res = await fetch(`${API_BASE}/notifications/${encodeURIComponent(hash)}?limit=${encodeURIComponent(String(limit))}`);
        return res.ok ? res.json() : { notifications: [] };
    },
    markNotificationsRead: async (hash: string, notificationId = '') => {
        const res = await fetch(`${API_BASE}/notifications/read`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ user_hash: hash, notification_id: notificationId })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Marking notifications read failed');
        }
    },
    getNotificationSummary: async (hash: string) => {
        const res = await fetch(`${API_BASE}/notifications/summary/${encodeURIComponent(hash)}`);
        return res.ok
            ? res.json()
            : { notificationsUnread: 0, friendRequestsPending: 0, messageRequestsPending: 0, dmUnread: 0, total: 0 };
    },
    createReport: async (reporterHash: string, targetHash: string, reason: string, options = {}) => {
        const res = await fetch(`${API_BASE}/reports`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                reporter_hash: reporterHash,
                target_hash: targetHash,
                reason,
                target_kind: options.targetKind || 'user',
                target_post_id: options.targetPostId || 0,
                target_thread_id: options.targetThreadId || 0,
                target_board_id: options.targetBoardId || '',
                target_display_name: options.targetDisplayName || '',
                context_link: options.contextLink || '',
            })
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Creating report failed');
        }
        return data;
    },
    getModerationReports: async (actorHash: string, founderToken = '', limit = 50) => {
        const qs = new URLSearchParams();
        qs.set('limit', String(limit));
        if (founderToken) qs.set('founder_token', founderToken);
        const res = await fetch(`${API_BASE}/admin/reports/${encodeURIComponent(actorHash)}?${qs.toString()}`);
        const data = await res.json().catch(() => ({ reports: [] }));
        if (!res.ok) {
            throw new Error(data?.error || 'Loading moderation reports failed');
        }
        return data;
    },
    getModerationAudit: async (actorHash: string, founderToken = '', limit = 50) => {
        const qs = new URLSearchParams();
        qs.set('limit', String(limit));
        if (founderToken) qs.set('founder_token', founderToken);
        const res = await fetch(`${API_BASE}/admin/audit/${encodeURIComponent(actorHash)}?${qs.toString()}`);
        const data = await res.json().catch(() => ({ events: [] }));
        if (!res.ok) {
            throw new Error(data?.error || 'Loading moderation audit failed');
        }
        return data;
    },
    resolveModerationReport: async (reportId: number, actorHash: string, status: 'open' | 'resolved' | 'dismissed', note = '', founderToken = '') => {
        const res = await fetch(`${API_BASE}/admin/reports/${encodeURIComponent(String(reportId))}/resolve`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                actor_hash: actorHash,
                founder_token: founderToken,
                status,
                note,
            }),
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Updating report failed');
        }
        return data;
    },
    deletePostAsModerator: async (postId: number, actorHash: string, founderToken = '') => {
        const res = await fetch(`${API_BASE}/admin/posts/${encodeURIComponent(String(postId))}/delete`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                actor_hash: actorHash,
                founder_token: founderToken,
            }),
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Deleting post failed');
        }
        return data;
    },
    banUserAsModerator: async (targetHash: string, actorHash: string, reason = '', founderToken = '') => {
        const res = await fetch(`${API_BASE}/admin/users/ban`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                actor_hash: actorHash,
                founder_token: founderToken,
                target_hash: targetHash,
                reason,
            }),
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Banning user failed');
        }
        return data;
    },
    unbanUserAsModerator: async (targetHash: string, actorHash: string, founderToken = '') => {
        const res = await fetch(`${API_BASE}/admin/users/unban`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                actor_hash: actorHash,
                founder_token: founderToken,
                target_hash: targetHash,
            }),
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || 'Unbanning user failed');
        }
        return data;
    },

    setPhoenixFiring: (v) => set({ isPhoenixFiring: v }),

    setEncryptionState: (state) => set((s) => ({
        encryptionState: { ...s.encryptionState, ...state },
    })),

    setQuantumModalVisible: (v) => set({ quantumModalVisible: v }),
    setPendingPost: (p) => set({ pendingPost: p }),

    fetchBoards: async () => {
        set({ boardsState: 'loading', boardsError: '' });
        try {
            const res = await fetch(`${API_BASE}/boards`);
            const data = await res.json().catch(() => ([]));
            if (!res.ok) {
                throw new Error((data as any)?.error || 'Failed to load boards');
            }
            set({
                boards: (data as any[]).map((b: any) => ({
                    id: b.id, name: b.name, description: b.description,
                    icon: b.icon || '', threadCount: b.threadCount || 0,
                    postCount: b.postCount || 0, nsfw: Boolean(b.nsfw)
                })),
                boardsState: 'ready',
                boardsError: '',
            });
        } catch (e) {
            console.error('Failed to fetch boards:', e);
            set({
                boards: [],
                boardsState: 'error',
                boardsError: e instanceof Error ? e.message : 'Failed to load boards.',
            });
        }
    },

    fetchThreads: async (boardId: string) => {
        set({ threadsState: 'loading', threadsError: '' });
        try {
            const res = await fetch(`${API_BASE}/threads?board_id=${encodeURIComponent(boardId)}`);
            const data = await res.json().catch(() => ({}));
            if (!res.ok) {
                throw new Error((data as any)?.error || `Failed to load /${boardId}/ threads`);
            }
            const threadArray = Array.isArray(data) ? data : ((data as any).threads || []);
            const mapped = threadArray.map((t: any) => mapBackendThread(t, boardId));
            set({ threads: mapped, threadsState: 'ready', threadsError: '' });
        } catch (e) {
            console.error('Failed to fetch threads:', e);
            set({
                threads: [],
                threadsState: 'error',
                threadsError: e instanceof Error ? e.message : `Failed to load /${boardId}/ threads.`,
            });
        }
    },

    createThread: async (boardId, subject, content, imageUrl, name, authorHash = '', encryptedContent) => {
        try {
            const payload = {
                board_id: boardId,
                subject,
                name,
                author_hash: authorHash,
                content: encryptedContent ? '[Encrypted Post]' : content,
                encrypted_content: encryptedContent,
                image_url: imageUrl,
            };
            const res = await fetch(`${API_BASE}/threads`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (res.ok) {
                const createdThread = await res.json();
                await get().fetchThreads(boardId);
                window.dispatchEvent(new CustomEvent(POST_CREATED_EVENT, {
                    detail: {
                        kind: 'thread',
                        boardId,
                        threadId: Number(createdThread.id) || null,
                    },
                }));
            } else {
                const errText = await res.text();
                console.error('Failed to create thread', errText);
                throw new Error(errText || `Server error ${res.status}`);
            }
        } catch (e) {
            console.error('Error creating thread:', e);
            throw e;
        }
        return {} as unknown as Thread;
    },

    createReply: async (boardId, threadId, content, imageUrl, name, authorHash = '', encryptedContent) => {
        try {
            const payload = {
                thread_id: threadId,
                name,
                author_hash: authorHash,
                content: encryptedContent ? '[Encrypted Post]' : content,
                encrypted_content: encryptedContent,
                image_url: imageUrl,
            };
            const res = await fetch(`${API_BASE}/posts`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (res.ok) {
                await res.json();
                await get().fetchThreads(boardId);
                window.dispatchEvent(new CustomEvent(POST_CREATED_EVENT, {
                    detail: {
                        kind: 'reply',
                        boardId,
                        threadId,
                    },
                }));
            } else {
                const errText = await res.text();
                console.error('Failed to create reply', errText);
                throw new Error(errText || `Server error ${res.status}`);
            }
        } catch (e) {
            console.error('Error creating reply:', e);
            throw e;
        }
        return {} as unknown as Post;
    },

    archiveThread: (threadId) => {
        set((s) => ({
            threads: s.threads.map((t) => t.id === threadId ? { ...t, archived: true } : t),
        }));
    },
}));
