import { useEffect, useState } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { useStore } from '../store/useStore';
import { useIdentity } from '../hooks/useIdentity';
import { buildSignedIdentityBinding, verifyIdentityBindingRecord } from '../utils/identityBinding';
import { getFounderToken, normalizeProfileRole } from '../utils/roleAuth';
import { motion } from 'framer-motion';
import { User, Shield, Activity, Clock, ArrowLeft, MessageCircle, UserPlus, ShieldCheck, ShieldAlert, Crown } from 'lucide-react';
import Post from '../components/Post';
import type { Post as PostType } from '../types';

export default function ProfilePage() {
    const { hash } = useParams<{ hash: string }>();
    const navigate = useNavigate();
    const {
        getProfile,
        getFriends,
        sendFriendRequest,
        acceptFriendRequest,
        rejectFriendRequest,
        cancelFriendRequest,
        removeFriend,
        blockUser,
        unblockUser,
        createReport,
        updateProfile,
        setProfileRole,
        banUserAsModerator,
        unbanUserAsModerator,
    } = useStore();
    const identity = useIdentity();

    const [profile, setProfile] = useState<any>(null);
    const [friendsInfo, setFriendsInfo] = useState<{ friends: any[]; pending_received: any[]; pending_sent: any[]; blocked: any[] }>({ friends: [], pending_received: [], pending_sent: [], blocked: [] });
    const [loading, setLoading] = useState(true);
    const [requestSent, setRequestSent] = useState(false);
    const [isEditingName, setIsEditingName] = useState(false);
    const [editName, setEditName] = useState('');
    const [bindingStatus, setBindingStatus] = useState<{ valid: boolean; reason: string } | null>(null);
    const [viewerRole, setViewerRole] = useState<'user' | 'moderator' | 'founder'>('user');
    const [roleActionLoading, setRoleActionLoading] = useState(false);
    const [roleActionError, setRoleActionError] = useState('');
    const [banActionLoading, setBanActionLoading] = useState(false);
    const [relationshipActionError, setRelationshipActionError] = useState('');
    const [reportReason, setReportReason] = useState('');
    const [reportStatus, setReportStatus] = useState('');

    useEffect(() => {
        if (!hash) return;

        async function load() {
            setLoading(true);
            try {
                setRequestSent(false);
                const loadedProfile = await getProfile(hash!);
                const loadedFriends = identity.identity?.displayHash
                    ? await getFriends(identity.identity.displayHash)
                    : { friends: [], pending_received: [], pending_sent: [], blocked: [] };
                setProfile(loadedProfile);
                setFriendsInfo(loadedFriends || { friends: [], pending_received: [], pending_sent: [], blocked: [] });
                if (loadedProfile?.identity_binding_payload && loadedProfile?.identity_binding_signature) {
                    setBindingStatus(await verifyIdentityBindingRecord(loadedProfile));
                } else {
                    setBindingStatus(null);
                }
                if (identity.identity?.displayHash) {
                    const viewerProfile = await getProfile(identity.identity.displayHash);
                    setViewerRole(normalizeProfileRole(viewerProfile?.role));
                } else {
                    setViewerRole('user');
                }
            } catch (e) {
                console.error(e);
            } finally {
                setLoading(false);
            }
        }

        load();
    }, [getFriends, getProfile, hash, identity.identity?.displayHash]);

    useEffect(() => {
        const onRoleUpdate = (event: Event) => {
            const role = (event as CustomEvent<{ role?: string }>).detail?.role;
            setViewerRole(normalizeProfileRole(role));
        };

        window.addEventListener('quanchan:self-role', onRoleUpdate as EventListener);
        return () => window.removeEventListener('quanchan:self-role', onRoleUpdate as EventListener);
    }, []);

    const handleAddFriend = async () => {
        if (!identity.identity?.displayHash || !hash) return;
        try {
            setRelationshipActionError('');
            await sendFriendRequest(identity.identity.displayHash, hash);
            setRequestSent(true);
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not send friend request.');
        }
    };

    const handleAcceptFriend = async () => {
        if (!identity.identity?.displayHash || !hash) return;
        try {
            setRelationshipActionError('');
            await acceptFriendRequest(hash, identity.identity.displayHash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not accept friend request.');
        }
    };

    const handleRejectFriend = async () => {
        if (!identity.identity?.displayHash || !hash) return;
        try {
            setRelationshipActionError('');
            await rejectFriendRequest(hash, identity.identity.displayHash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not reject friend request.');
        }
    };

    const handleCancelFriend = async () => {
        if (!identity.identity?.displayHash || !hash) return;
        try {
            setRelationshipActionError('');
            await cancelFriendRequest(identity.identity.displayHash, hash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not cancel friend request.');
        }
    };

    const handleRemoveFriend = async () => {
        if (!identity.identity?.displayHash || !hash) return;
        try {
            setRelationshipActionError('');
            await removeFriend(identity.identity.displayHash, hash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not remove friend.');
        }
    };

    const handleBlockToggle = async () => {
        if (!identity.identity?.displayHash || !hash) return;
        try {
            setRelationshipActionError('');
            if (friendsInfo.blocked.some(entry => entry.hash === hash)) {
                await unblockUser(identity.identity.displayHash, hash);
            } else {
                await blockUser(identity.identity.displayHash, hash);
            }
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not update block status.');
        }
    };

    const handleReport = async () => {
        if (!identity.identity?.displayHash || !hash || !reportReason.trim()) return;
        try {
            setReportStatus('');
            await createReport(identity.identity.displayHash, hash, reportReason.trim());
            setReportReason('');
            setReportStatus('Report submitted.');
        } catch (e) {
            console.error(e);
            setReportStatus(e instanceof Error ? e.message : 'Could not submit report.');
        }
    };

    const handleSaveName = async () => {
        if (!identity.identity || !editName.trim()) return;
        try {
            const cleaned = editName.trim().replace(/\s+/g, '_');
            const binding = await buildSignedIdentityBinding({ ...identity.identity, username: cleaned });
            await updateProfile(
                identity.identity.displayHash,
                cleaned,
                identity.identity.pqcKemPublicKey,
                identity.identity.publicKey,
                identity.identity.pqcIdentityPublicKey,
                identity.identity.pqcIdentityScheme,
                binding.payload,
                binding.signature
            );
            identity.setUsername(cleaned);
            if (profile) {
                const nextProfile = {
                    ...profile,
                    username: cleaned,
                    pqc_identity_public_key: identity.identity.pqcIdentityPublicKey,
                    pqc_identity_scheme: identity.identity.pqcIdentityScheme,
                    identity_binding_payload: binding.payload,
                    identity_binding_signature: binding.signature,
                };
                setProfile(nextProfile);
                setBindingStatus(await verifyIdentityBindingRecord(nextProfile));
            }
            setIsEditingName(false);
        } catch (e) {
            console.error(e);
        }
    };

    const handleRoleChange = async (nextRole: 'user' | 'moderator') => {
        if (!identity.identity?.displayHash || !hash) return;
        try {
            setRoleActionLoading(true);
            setRoleActionError('');
            await setProfileRole(identity.identity.displayHash, getFounderToken(), hash, nextRole);
            const refreshedProfile = await getProfile(hash);
            setProfile(refreshedProfile);
        } catch (error) {
            console.error('Role update failed:', error);
            setRoleActionError(error instanceof Error ? error.message : 'Role update failed.');
        } finally {
            setRoleActionLoading(false);
        }
    };

    const handleBanToggle = async () => {
        if (!identity.identity?.displayHash || !hash) return;
        try {
            setBanActionLoading(true);
            setRoleActionError('');
            if (workingProfile?.banned) {
                await unbanUserAsModerator(hash, identity.identity.displayHash, viewerRole === 'founder' ? getFounderToken() : '');
            } else {
                await banUserAsModerator(hash, identity.identity.displayHash, 'Restricted by moderation.', viewerRole === 'founder' ? getFounderToken() : '');
            }
            const refreshedProfile = await getProfile(hash);
            setProfile(refreshedProfile);
        } catch (error) {
            console.error('Ban toggle failed:', error);
            setRoleActionError(error instanceof Error ? error.message : 'Moderation action failed.');
        } finally {
            setBanActionLoading(false);
        }
    };

    if (loading) {
        return (
            <div style={{ padding: '20px', maxWidth: '960px', margin: '0 auto' }}>
                <div className="flat-card" style={{ minHeight: '220px', marginBottom: '16px' }} />
                <div className="flat-card" style={{ minHeight: '260px' }} />
            </div>
        );
    }

    const isMe = identity.identity?.displayHash === hash;
    const workingProfile = profile || (isMe ? {
        username: identity.identity?.username || 'Anonymous',
        displayHash: hash,
        recent_posts: [],
        role: viewerRole,
        role_assigned_by: '',
        role_assigned_by_username: '',
    } : null);

    if (!workingProfile) {
        return (
            <div style={{ padding: '24px', maxWidth: '960px', margin: '0 auto' }}>
                <div className="flat-card" style={{ padding: '20px', color: 'var(--red)', textAlign: 'center' }}>
                    Profile missing or not found.
                </div>
            </div>
        );
    }

    const safeFriends = Array.isArray(friendsInfo) ? friendsInfo : (friendsInfo?.friends || []);
    const isFriend = safeFriends.some(friend => friend.hash === hash);
    const incomingFriendRequest = friendsInfo.pending_received.some(friend => friend.hash === hash);
    const outgoingFriendRequest = friendsInfo.pending_sent.some(friend => friend.hash === hash) || requestSent;
    const isBlocked = friendsInfo.blocked.some(friend => friend.hash === hash);
    const profileRole = normalizeProfileRole(workingProfile?.role);
    const founderCanManage = viewerRole === 'founder' && Boolean(getFounderToken()) && !isMe && profileRole !== 'founder';
    const moderatorCanBan = (viewerRole === 'founder' || viewerRole === 'moderator') && !isMe && profileRole === 'user';
    const roleBadgeColor = profileRole === 'founder' ? 'var(--gold)' : profileRole === 'moderator' ? 'var(--cyan)' : 'var(--text-dim)';
    const roleLabel = workingProfile?.role_badge || (profileRole === 'founder' ? 'FOUNDER' : profileRole === 'moderator' ? 'MOD' : 'USER');

    return (
        <motion.div initial={{ opacity: 0, y: 20 }} animate={{ opacity: 1, y: 0 }} style={{ padding: '20px', maxWidth: '960px', margin: '0 auto 120px' }}>
            <button onClick={() => navigate(-1)} className="btn-v2 flex items-center gap-2" style={{ padding: '10px 14px', marginBottom: '16px' }}>
                <ArrowLeft size={14} /> Back
            </button>

            <div className="flat-card" style={{ padding: '22px', marginBottom: '18px' }}>
                <div
                    style={{
                        display: 'flex',
                        flexWrap: 'wrap',
                        alignItems: 'flex-start',
                        justifyContent: 'space-between',
                        gap: '18px',
                    }}
                >
                    <div style={{ display: 'flex', alignItems: 'flex-start', gap: '18px', flex: '1 1 420px' }}>
                        <div
                            style={{
                                width: '82px',
                                height: '82px',
                                borderRadius: '18px',
                                display: 'flex',
                                alignItems: 'center',
                                justifyContent: 'center',
                                background: 'var(--surface-2)',
                                border: '1px solid var(--border)',
                                flexShrink: 0,
                            }}
                        >
                            <User size={36} style={{ color: 'var(--text-dim)' }} />
                        </div>

                        <div style={{ minWidth: 0, flex: 1 }}>
                            {isEditingName ? (
                                <div style={{ display: 'flex', gap: '8px', flexWrap: 'wrap', alignItems: 'center' }}>
                                    <input
                                        className="v2-input"
                                        style={{ padding: '10px 12px', minWidth: '220px' }}
                                        value={editName}
                                        onChange={e => setEditName(e.target.value)}
                                        autoFocus
                                    />
                                    <button onClick={handleSaveName} className="btn-v2-accent text-sm" style={{ padding: '10px 14px' }}>Save</button>
                                    <button onClick={() => setIsEditingName(false)} className="btn-v2 text-sm" style={{ padding: '10px 14px' }}>Cancel</button>
                                </div>
                            ) : (
                                <div style={{ display: 'flex', alignItems: 'center', gap: '10px', flexWrap: 'wrap' }}>
                                    <h1 style={{ color: 'var(--text)', fontFamily: 'var(--font-heading)', fontSize: 'clamp(1.5rem, 4vw, 2.4rem)', lineHeight: 1.1 }}>
                                        {workingProfile.username || 'Anonymous'}
                                    </h1>
                                    <span className="identity-badge" style={{ padding: '6px 10px', color: roleBadgeColor }}>
                                        <span style={{ display: 'inline-flex', alignItems: 'center', gap: '6px' }}>
                                            <Crown size={12} /> {roleLabel}
                                        </span>
                                    </span>
                                    {workingProfile?.banned && (
                                        <span className="identity-badge" style={{ padding: '6px 10px', color: 'var(--red)' }}>
                                            RESTRICTED
                                        </span>
                                    )}
                                    {isMe && (
                                        <span className="identity-badge" style={{ padding: '6px 10px' }}>
                                            YOU
                                        </span>
                                    )}
                                </div>
                            )}

                            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '10px', marginTop: '12px' }}>
                                <span className="stat-pill text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                                    <span style={{ display: 'inline-flex', alignItems: 'center', gap: '6px' }}>
                                        <Shield size={12} /> {hash}
                                    </span>
                                </span>
                                <span className="stat-pill text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                                    <span style={{ display: 'inline-flex', alignItems: 'center', gap: '6px' }}>
                                        <Activity size={12} /> {workingProfile.recent_posts?.length || 0} recent posts
                                    </span>
                                </span>
                                <span className="stat-pill text-xs font-mono" style={{ color: bindingStatus?.valid ? 'var(--green)' : bindingStatus ? 'var(--red)' : 'var(--text-dim)' }}>
                                    <span style={{ display: 'inline-flex', alignItems: 'center', gap: '6px' }}>
                                        {bindingStatus?.valid ? <ShieldCheck size={12} /> : bindingStatus ? <ShieldAlert size={12} /> : <Shield size={12} />}
                                        {bindingStatus?.valid ? 'PQC identity bound' : bindingStatus ? 'PQC identity invalid' : 'No PQC identity proof'}
                                    </span>
                                </span>
                                {workingProfile.pqc_identity_scheme && (
                                    <span className="stat-pill text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                                        <span style={{ display: 'inline-flex', alignItems: 'center', gap: '6px' }}>
                                            <Shield size={12} /> {workingProfile.pqc_identity_scheme}
                                        </span>
                                    </span>
                                )}
                                {workingProfile.last_active && (
                                    <span className="stat-pill text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                                        <span style={{ display: 'inline-flex', alignItems: 'center', gap: '6px' }}>
                                            <Clock size={12} /> Recently active
                                        </span>
                                    </span>
                                )}
                            </div>

                            {bindingStatus && (
                                <p style={{ marginTop: '12px', color: bindingStatus.valid ? 'var(--green)' : 'var(--red)', fontSize: '0.8rem', lineHeight: 1.5 }}>
                                    {bindingStatus.reason}
                                </p>
                            )}
                            {workingProfile.role_assigned_by && profileRole !== 'user' && (
                                <p style={{ marginTop: '8px', color: 'var(--text-dim)', fontSize: '0.78rem', lineHeight: 1.5 }}>
                                    Assigned by {workingProfile.role_assigned_by_username || workingProfile.role_assigned_by}
                                </p>
                            )}

                            {isMe && !isEditingName && !identity.identity?.username && (
                                <button
                                    onClick={() => { setEditName(workingProfile.username || ''); setIsEditingName(true); }}
                                    className="btn-v2 text-xs"
                                    style={{ padding: '8px 10px', marginTop: '14px' }}
                                >
                                    Set Display Name
                                </button>
                            )}
                        </div>
                    </div>

                    {!isMe && hash && (
                        <div style={{ display: 'flex', flexDirection: 'column', gap: '10px', minWidth: '190px', flex: '0 0 auto' }}>
                            {isBlocked ? (
                                <button onClick={handleBlockToggle} className="btn-v2 text-sm" style={{ padding: '11px 16px' }}>
                                    Unblock User
                                </button>
                            ) : isFriend ? (
                                <>
                                    <button className="btn-v2 text-sm" style={{ padding: '11px 16px', color: 'var(--green)' }}>
                                        Connected
                                    </button>
                                    <button onClick={handleRemoveFriend} className="btn-v2 text-sm" style={{ padding: '11px 16px' }}>
                                        Remove Friend
                                    </button>
                                </>
                            ) : incomingFriendRequest ? (
                                <>
                                    <button onClick={handleAcceptFriend} className="btn-v2-accent text-sm" style={{ padding: '11px 16px' }}>
                                        Accept Friend Request
                                    </button>
                                    <button onClick={handleRejectFriend} className="btn-v2 text-sm" style={{ padding: '11px 16px' }}>
                                        Decline Friend Request
                                    </button>
                                </>
                            ) : outgoingFriendRequest ? (
                                <>
                                    <button className="btn-v2 text-sm" style={{ padding: '11px 16px' }}>
                                        Request Sent
                                    </button>
                                    <button onClick={handleCancelFriend} className="btn-v2 text-sm" style={{ padding: '11px 16px' }}>
                                        Cancel Request
                                    </button>
                                </>
                            ) : (
                                <button
                                    onClick={handleAddFriend}
                                    className="btn-v2-accent text-sm"
                                    style={{ padding: '11px 16px' }}
                                >
                                    <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                                        <UserPlus size={14} /> Add Friend
                                    </span>
                                </button>
                            )}

                            <button onClick={() => navigate(`/dm?to=${encodeURIComponent(hash)}`)} className="btn-v2 text-sm" style={{ padding: '11px 16px' }}>
                                <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                                    <MessageCircle size={14} /> Direct Message
                                </span>
                            </button>
                            {!isBlocked && (
                                <button onClick={handleBlockToggle} className="btn-v2 text-sm" style={{ padding: '11px 16px' }}>
                                    Block User
                                </button>
                            )}
                            {founderCanManage && (
                                <>
                                    {profileRole !== 'moderator' ? (
                                        <button
                                            onClick={() => handleRoleChange('moderator')}
                                            disabled={roleActionLoading}
                                            className="btn-v2-accent text-sm"
                                            style={{ padding: '11px 16px' }}
                                        >
                                            <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                                                <Crown size={14} /> {roleActionLoading ? 'Updating...' : 'Make Moderator'}
                                            </span>
                                        </button>
                                    ) : (
                                        <button
                                            onClick={() => handleRoleChange('user')}
                                            disabled={roleActionLoading}
                                            className="btn-v2 text-sm"
                                            style={{ padding: '11px 16px' }}
                                        >
                                            <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                                                <Crown size={14} /> {roleActionLoading ? 'Updating...' : 'Remove Moderator'}
                                            </span>
                                        </button>
                                    )}
                                </>
                            )}
                            {moderatorCanBan && (
                                <button
                                    onClick={() => handleBanToggle()}
                                    disabled={banActionLoading}
                                    className="btn-v2 text-sm"
                                    style={{ padding: '11px 16px', color: workingProfile?.banned ? 'var(--green)' : 'var(--red)' }}
                                >
                                    <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                                        <ShieldAlert size={14} /> {banActionLoading ? 'Updating...' : workingProfile?.banned ? 'Lift Restriction' : 'Restrict User'}
                                    </span>
                                </button>
                            )}
                            {roleActionError && (
                                <p style={{ color: 'var(--red)', fontSize: '0.78rem', lineHeight: 1.45 }}>
                                    {roleActionError}
                                </p>
                            )}
                            {relationshipActionError && (
                                <p style={{ color: 'var(--red)', fontSize: '0.78rem', lineHeight: 1.45 }}>
                                    {relationshipActionError}
                                </p>
                            )}
                            <div className="flat-card" style={{ padding: '12px', background: 'rgba(255,255,255,0.03)' }}>
                                <div className="text-xs uppercase tracking-wider" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono)', marginBottom: '8px' }}>
                                    Report User
                                </div>
                                <textarea
                                    className="v2-input w-full text-xs"
                                    style={{ minHeight: '70px', padding: '10px 12px', resize: 'vertical' }}
                                    placeholder="Explain what happened..."
                                    value={reportReason}
                                    onChange={e => setReportReason(e.target.value)}
                                />
                                <button onClick={handleReport} className="btn-v2 text-xs mt-2 w-full" style={{ padding: '9px 12px' }} disabled={!reportReason.trim()}>
                                    Submit Report
                                </button>
                                {reportStatus && (
                                    <p style={{ color: reportStatus.includes('submitted') ? 'var(--green)' : 'var(--red)', fontSize: '0.78rem', marginTop: '8px', lineHeight: 1.45 }}>
                                        {reportStatus}
                                    </p>
                                )}
                            </div>
                        </div>
                    )}
                </div>
            </div>

            <div className="flat-card" style={{ padding: '20px' }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px' }}>
                    <Activity size={16} style={{ color: 'var(--cyan)' }} />
                    <h2 className="text-lg font-bold" style={{ color: 'var(--text)' }}>Recent Activity</h2>
                </div>

                {(!workingProfile.recent_posts || workingProfile.recent_posts.length === 0) ? (
                    <div style={{ padding: '24px', textAlign: 'center', color: 'var(--text-dim)' }}>
                        No recent activity found.
                    </div>
                ) : (
                    <div className="space-y-4">
                        {workingProfile.recent_posts.map((recentPost: any) => {
                            const post: PostType = {
                                id: Number(recentPost.id),
                                no: Number(recentPost.id),
                                threadId: Number(recentPost.thread_id),
                                boardId: recentPost.board_id,
                                content: recentPost.content,
                                isEncrypted: false,
                                imageUrl: recentPost.image_url,
                                timestamp: new Date(recentPost.created_at).getTime(),
                                name: recentPost.name,
                                sage: false,
                                replies: [],
                            };

                            return (
                                <div key={post.id} style={{ position: 'relative' }}>
                                    <button
                                        onClick={() => navigate(`/${recentPost.board_id}/thread/${recentPost.thread_id}#p${post.id}`)}
                                        className="btn-v2 text-xs"
                                        style={{ position: 'absolute', top: '12px', right: '12px', zIndex: 4, padding: '7px 10px' }}
                                    >
                                        View Thread
                                    </button>
                                    <Post post={post} isOP={false} depth={0} allPosts={[]} />
                                </div>
                            );
                        })}
                    </div>
                )}
            </div>
        </motion.div>
    );
}
