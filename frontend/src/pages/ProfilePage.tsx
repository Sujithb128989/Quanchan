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
import { useEffect, useState } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { useStore } from '../store/useStore';
import { useIdentity } from '../hooks/useIdentity';
import { buildSignedIdentityBinding, verifyIdentityBindingRecord } from '../utils/identityBinding';
import { getFounderToken, normalizeProfileRole } from '../utils/roleAuth';
import { motion } from 'framer-motion';
import { User, Shield, Activity, Clock, ArrowLeft, MessageCircle, UserPlus, ShieldCheck, ShieldAlert, Crown, Users } from 'lucide-react';
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
        selectTag,
        createPayment,
        simulatePaymentSuccess,
        giftUser,
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

    // Shop and Custom Badge states
    const [activeTab, setActiveTab] = useState<'activity' | 'shop' | 'connections'>('activity');
    const [selectedProduct, setSelectedProduct] = useState<{ id: string; name: string; price: number; type: 'tag' | 'sub' } | null>(null);
    const [payCurrency, setPayCurrency] = useState<'btc' | 'ltc' | 'xmr'>('btc');
    const [invoice, setInvoice] = useState<any>(null);
    const [paymentLoading, setPaymentLoading] = useState(false);
    const [paymentError, setPaymentError] = useState('');
    const [activeTagChangeLoading, setActiveTagChangeLoading] = useState(false);
    const [simulationStatus, setSimulationStatus] = useState('');

    // Founder Gifting states
    const [giftType, setGiftType] = useState<'tag' | 'subscription'>('tag');
    const [giftBadgeSelection, setGiftBadgeSelection] = useState<string>('queen');
    const [giftCustomBadge, setGiftCustomBadge] = useState<string>('');
    const [giftSubSelection, setGiftSubSelection] = useState<string>('circle');
    const [giftDurationDays, setGiftDurationDays] = useState<number>(30);
    const [giftLoading, setGiftLoading] = useState<boolean>(false);
    const [giftError, setGiftError] = useState<string>('');
    const [giftSuccess, setGiftSuccess] = useState<string>('');

    const customTags = [
        { id: 'queen', name: 'queen', price: 5.0, desc: 'Royal violet-magenta style' },
        { id: 'daddy', name: 'daddy', price: 5.0, desc: 'Sweet pink style' },
        { id: 'OG', name: 'OG', price: 5.0, desc: 'Classic gold style' },
        { id: 'LGBT', name: 'LGBT', price: 5.0, desc: 'Proud rainbow/emerald style' },
        { id: 'VIP', name: 'VIP', price: 5.0, desc: 'Premium blue style' },
        { id: 'CHAD', name: 'CHAD', price: 5.0, desc: 'Bold red style' },
        { id: 'DONOR', name: 'DONOR', price: 5.0, desc: 'Generous teal style' },
        { id: 'PREMIUM', name: 'PREMIUM', price: 5.0, desc: 'Sleek orange style' },
        { id: 'WAIFU', name: 'WAIFU', price: 5.0, desc: 'Fuchsia style' },
        { id: 'SIMP', name: 'SIMP', price: 5.0, desc: 'Loyal violet style' },
        { id: 'ELITE', name: 'ELITE', price: 5.0, desc: 'Aggressive rose-red style' },
        { id: 'BOOSTER', name: 'BOOSTER', price: 5.0, desc: 'Sky blue style' },
    ];

    const subscriptions = [
        { id: 'circle', name: 'Circle Tier', price: 10.0, desc: 'Unlock Group Rooms creation & more features' },
        { id: 'hermes', name: 'Hermes Tier', price: 25.0, desc: 'Full AI API keys & ML-KEM encrypted assistant access' },
    ];

    const initiatePurchase = (id: string, name: string, price: number, type: 'tag' | 'sub') => {
        setSelectedProduct({ id, name, price, type });
        setPayCurrency('btc');
        setInvoice(null);
        setPaymentError('');
        setSimulationStatus('');
    };

    const handleCreateInvoice = async () => {
        if (!selectedProduct || !hash) return;
        try {
            setPaymentLoading(true);
            setPaymentError('');
            const data = await createPayment(hash, selectedProduct.id, payCurrency);
            setInvoice(data);
        } catch (err) {
            console.error('Invoice creation failed:', err);
            setPaymentError(err instanceof Error ? err.message : 'Invoice creation failed.');
        } finally {
            setPaymentLoading(false);
        }
    };

    const handleSimulateSuccess = async () => {
        if (!invoice || !hash) return;
        try {
            setSimulationStatus('Simulating payment success...');
            const data = await simulatePaymentSuccess(invoice.order_id);
            setSimulationStatus(data?.message || 'Success simulated!');
            const refreshedProfile = await getProfile(hash);
            setProfile(refreshedProfile);
            window.setTimeout(() => {
                setInvoice(null);
                setSelectedProduct(null);
                setSimulationStatus('');
            }, 2000);
        } catch (err) {
            console.error('Simulation failed:', err);
            setSimulationStatus(err instanceof Error ? err.message : 'Simulation failed.');
        }
    };

    const handleEquipTag = async (tag: string) => {
        try {
            setActiveTagChangeLoading(true);
            await selectTag(hash!, tag);
            const refreshedProfile = await getProfile(hash!);
            setProfile(refreshedProfile);
        } catch (err) {
            console.error('Failed to change tag:', err);
        } finally {
            setActiveTagChangeLoading(false);
        }
    };

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

    const handleAcceptFriendHash = async (peerHash: string) => {
        if (!identity.identity?.displayHash) return;
        try {
            setRelationshipActionError('');
            await acceptFriendRequest(peerHash, identity.identity.displayHash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not accept friend request.');
        }
    };

    const handleRejectFriendHash = async (peerHash: string) => {
        if (!identity.identity?.displayHash) return;
        try {
            setRelationshipActionError('');
            await rejectFriendRequest(peerHash, identity.identity.displayHash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not reject friend request.');
        }
    };

    const handleCancelFriendHash = async (peerHash: string) => {
        if (!identity.identity?.displayHash) return;
        try {
            setRelationshipActionError('');
            await cancelFriendRequest(identity.identity.displayHash, peerHash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not cancel friend request.');
        }
    };

    const handleRemoveFriendHash = async (peerHash: string) => {
        if (!identity.identity?.displayHash) return;
        try {
            setRelationshipActionError('');
            await removeFriend(identity.identity.displayHash, peerHash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not remove friend.');
        }
    };

    const handleBlockUserHash = async (peerHash: string) => {
        if (!identity.identity?.displayHash) return;
        try {
            setRelationshipActionError('');
            await blockUser(identity.identity.displayHash, peerHash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not block user.');
        }
    };

    const handleUnblockUserHash = async (peerHash: string) => {
        if (!identity.identity?.displayHash) return;
        try {
            setRelationshipActionError('');
            await unblockUser(identity.identity.displayHash, peerHash);
            setFriendsInfo(await getFriends(identity.identity.displayHash));
        } catch (e) {
            console.error(e);
            setRelationshipActionError(e instanceof Error ? e.message : 'Could not unblock user.');
        }
    };

    const handleSendGift = async () => {
        if (!identity.identity?.displayHash || !hash) return;
        setGiftLoading(true);
        setGiftError('');
        setGiftSuccess('');
        try {
            let giftValue = '';
            if (giftType === 'tag') {
                if (giftBadgeSelection === 'custom') {
                    giftValue = giftCustomBadge.trim();
                } else {
                    giftValue = giftBadgeSelection;
                }
            } else {
                giftValue = giftSubSelection;
            }

            await giftUser(
                identity.identity.displayHash,
                hash,
                giftType,
                giftValue,
                giftType === 'subscription' ? giftDurationDays : 0
            );

            setGiftSuccess(`Successfully gifted ${giftType === 'tag' ? `tag "${giftValue || 'Clear'}"` : `${giftValue} sub`}`);
            
            // Refresh target profile
            const refreshedProfile = await getProfile(hash);
            setProfile(refreshedProfile);
        } catch (err) {
            console.error('Gifting failed:', err);
            setGiftError(err instanceof Error ? err.message : 'Gifting failed');
        } finally {
            setGiftLoading(false);
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

    const unlockedArray = workingProfile?.unlocked_tags
        ? workingProfile.unlocked_tags.split(',').map((t: string) => t.trim()).filter(Boolean)
        : [];

    const renderRecentActivity = () => {
        return (
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
                                        onClick={() => navigate(`/b/${post.boardId}/${post.threadId}`)}
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
        );
    };

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
                                    {workingProfile?.custom_badge && (
                                        <span className={`identity-badge px-2.5 py-1 text-xs font-bold border rounded ${
                                            workingProfile.custom_badge === 'queen' ? 'text-fuchsia-400 border-violet-500 bg-violet-950/30' :
                                            workingProfile.custom_badge === 'daddy' ? 'text-pink-400 border-pink-500 bg-pink-950/30' :
                                            workingProfile.custom_badge === 'OG' ? 'text-amber-400 border-amber-500 bg-amber-950/30' :
                                            workingProfile.custom_badge === 'LGBT' ? 'text-emerald-400 border-emerald-500 bg-emerald-950/30' :
                                            workingProfile.custom_badge === 'VIP' ? 'text-blue-400 border-blue-500 bg-blue-950/30' :
                                            workingProfile.custom_badge === 'CHAD' ? 'text-red-400 border-red-500 bg-red-950/30' :
                                            workingProfile.custom_badge === 'DONOR' ? 'text-teal-400 border-teal-500 bg-teal-950/30' :
                                            workingProfile.custom_badge === 'PREMIUM' ? 'text-orange-400 border-orange-500 bg-orange-950/30' :
                                            workingProfile.custom_badge === 'WAIFU' ? 'text-fuchsia-400 border-fuchsia-500 bg-fuchsia-950/30' :
                                            workingProfile.custom_badge === 'SIMP' ? 'text-violet-400 border-violet-500 bg-violet-950/30' :
                                            workingProfile.custom_badge === 'ELITE' ? 'text-rose-500 border-rose-600 bg-rose-950/30' :
                                            workingProfile.custom_badge === 'BOOSTER' ? 'text-sky-400 border-sky-500 bg-sky-950/30' :
                                            'text-zinc-400 border-zinc-500 bg-zinc-950/30'
                                        }`}>
                                            {workingProfile.custom_badge}
                                        </span>
                                    )}
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

            {isMe && (
                <div style={{ display: 'flex', gap: '8px', marginBottom: '20px', borderBottom: '1px solid var(--border)', paddingBottom: '10px' }}>
                    <button
                        onClick={() => setActiveTab('activity')}
                        style={{
                            padding: '10px 18px',
                            background: activeTab === 'activity' ? 'var(--surface-3)' : 'transparent',
                            borderColor: activeTab === 'activity' ? 'var(--cyan)' : 'transparent',
                            color: activeTab === 'activity' ? 'var(--cyan)' : 'var(--text-dim)',
                            borderBottom: activeTab === 'activity' ? '2px solid var(--cyan)' : 'none',
                            cursor: 'pointer',
                            fontWeight: 'bold',
                            transition: 'all 0.2s',
                        }}
                    >
                        Recent Activity
                    </button>
                    <button
                        onClick={() => setActiveTab('shop')}
                        style={{
                            padding: '10px 18px',
                            background: activeTab === 'shop' ? 'var(--surface-3)' : 'transparent',
                            borderColor: activeTab === 'shop' ? 'var(--cyan)' : 'transparent',
                            color: activeTab === 'shop' ? 'var(--cyan)' : 'var(--text-dim)',
                            borderBottom: activeTab === 'shop' ? '2px solid var(--cyan)' : 'none',
                            cursor: 'pointer',
                            fontWeight: 'bold',
                            transition: 'all 0.2s',
                        }}
                    >
                        Premium Shop & Badges
                    </button>
                    <button
                        onClick={() => setActiveTab('connections')}
                        style={{
                            padding: '10px 18px',
                            background: activeTab === 'connections' ? 'var(--surface-3)' : 'transparent',
                            borderColor: activeTab === 'connections' ? 'var(--cyan)' : 'transparent',
                            color: activeTab === 'connections' ? 'var(--cyan)' : 'var(--text-dim)',
                            borderBottom: activeTab === 'connections' ? '2px solid var(--cyan)' : 'none',
                            cursor: 'pointer',
                            fontWeight: 'bold',
                            transition: 'all 0.2s',
                        }}
                    >
                        Connections & Friends
                    </button>
                </div>
            )}

            {!isMe ? (
                <div>
                    {!isMe && viewerRole === 'founder' && (
                        <div className="flat-card" style={{ padding: '20px', marginBottom: '18px', border: '1px solid var(--violet-500)', background: 'linear-gradient(to bottom, var(--surface-1), var(--violet-950)/10)' }}>
                            <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px' }}>
                                <Crown size={16} style={{ color: 'var(--gold)' }} />
                                <h2 className="text-lg font-bold" style={{ color: 'var(--text)' }}>Founder's Gift Box</h2>
                            </div>
                            
                            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(220px, 1fr))', gap: '16px', marginBottom: '16px' }}>
                                <div>
                                    <label style={{ display: 'block', fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '6px' }}>Gift Type</label>
                                    <select
                                        className="v2-input w-full text-sm"
                                        value={giftType}
                                        onChange={e => setGiftType(e.target.value as 'tag' | 'subscription')}
                                        style={{ padding: '8px 10px' }}
                                    >
                                        <option value="tag">Badge / Custom Tag</option>
                                        <option value="subscription">Subscription Tier</option>
                                    </select>
                                </div>

                                {giftType === 'tag' ? (
                                    <>
                                        <div>
                                            <label style={{ display: 'block', fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '6px' }}>Select Badge</label>
                                            <select
                                                className="v2-input w-full text-sm"
                                                value={giftBadgeSelection}
                                                onChange={e => setGiftBadgeSelection(e.target.value)}
                                                style={{ padding: '8px 10px' }}
                                            >
                                                <option value="queen">queen (New! Purple/Magenta)</option>
                                                <option value="daddy">daddy (Pink)</option>
                                                <option value="OG">OG (Gold)</option>
                                                <option value="LGBT">LGBT (Rainbow)</option>
                                                <option value="VIP">VIP (Blue)</option>
                                                <option value="CHAD">CHAD (Red)</option>
                                                <option value="DONOR">DONOR (Teal)</option>
                                                <option value="PREMIUM">PREMIUM (Orange)</option>
                                                <option value="WAIFU">WAIFU (Fuchsia)</option>
                                                <option value="SIMP">SIMP (Violet)</option>
                                                <option value="ELITE">ELITE (Rose)</option>
                                                <option value="BOOSTER">BOOSTER (Sky Blue)</option>
                                                <option value="clear">Clear Active Badge</option>
                                                <option value="custom">Custom Badge Name...</option>
                                            </select>
                                        </div>
                                        {giftBadgeSelection === 'custom' && (
                                            <div>
                                                <label style={{ display: 'block', fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '6px' }}>Custom Badge Name</label>
                                                <input
                                                    type="text"
                                                    className="v2-input w-full text-sm"
                                                    value={giftCustomBadge}
                                                    onChange={e => setGiftCustomBadge(e.target.value)}
                                                    placeholder="e.g. FOUNDER"
                                                    style={{ padding: '8px 10px' }}
                                                />
                                            </div>
                                        )}
                                    </>
                                ) : (
                                    <>
                                        <div>
                                            <label style={{ display: 'block', fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '6px' }}>Select Tier</label>
                                            <select
                                                className="v2-input w-full text-sm"
                                                value={giftSubSelection}
                                                onChange={e => setGiftSubSelection(e.target.value)}
                                                style={{ padding: '8px 10px' }}
                                            >
                                                <option value="circle">Circle Tier</option>
                                                <option value="hermes">Hermes Tier</option>
                                            </select>
                                        </div>
                                        <div>
                                            <label style={{ display: 'block', fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '6px' }}>Duration (Days)</label>
                                            <input
                                                type="number"
                                                className="v2-input w-full text-sm"
                                                value={giftDurationDays}
                                                onChange={e => setGiftDurationDays(Number(e.target.value))}
                                                min={1}
                                                style={{ padding: '8px 10px' }}
                                            />
                                        </div>
                                    </>
                                )}
                            </div>

                            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: '12px' }}>
                                <div>
                                    {giftError && <p style={{ color: 'var(--red)', fontSize: '0.85rem' }}>{giftError}</p>}
                                    {giftSuccess && <p style={{ color: 'var(--green)', fontSize: '0.85rem' }}>{giftSuccess}</p>}
                                </div>
                                <button
                                    onClick={handleSendGift}
                                    disabled={giftLoading}
                                    className="btn-v2-accent"
                                    style={{ padding: '10px 20px', border: '1px solid var(--violet-500)' }}
                                >
                                    {giftLoading ? 'Sending Gift...' : 'Gift User'}
                                </button>
                            </div>
                        </div>
                    )}
                    {renderRecentActivity()}
                </div>
            ) : activeTab === 'activity' ? (
                renderRecentActivity()
            ) : activeTab === 'connections' ? (
                <div style={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
                    {/* Incoming Requests */}
                    <div className="flat-card" style={{ padding: '20px' }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px' }}>
                            <UserPlus size={16} style={{ color: 'var(--cyan)' }} />
                            <h2 className="text-lg font-bold" style={{ color: 'var(--text)' }}>Incoming Friend Requests</h2>
                        </div>
                        {friendsInfo.pending_received.length === 0 ? (
                            <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem' }}>No pending incoming friend requests.</p>
                        ) : (
                            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '12px' }}>
                                {friendsInfo.pending_received.map((entry: any) => (
                                    <div key={entry.hash} style={{ padding: '14px', borderRadius: '8px', background: 'var(--surface-2)', border: '1px solid var(--border)', display: 'flex', flexDirection: 'column', gap: '10px', justifyContent: 'space-between' }}>
                                        <div>
                                            <div style={{ fontWeight: 'bold', color: 'var(--text)', wordBreak: 'break-all' }}>{entry.username || 'Anonymous'}</div>
                                            <div style={{ fontSize: '0.75rem', color: 'var(--text-dim)', fontFamily: 'var(--font-mono)', overflow: 'hidden', textOverflow: 'ellipsis' }}>{entry.hash}</div>
                                        </div>
                                        <div style={{ display: 'flex', gap: '8px' }}>
                                            <button onClick={() => handleAcceptFriendHash(entry.hash)} className="btn-v2-accent text-xs" style={{ padding: '6px 12px', flex: 1 }}>Accept</button>
                                            <button onClick={() => handleRejectFriendHash(entry.hash)} className="btn-v2 text-xs" style={{ padding: '6px 12px', flex: 1 }}>Decline</button>
                                        </div>
                                    </div>
                                ))}
                            </div>
                        )}
                    </div>

                    {/* Sent Requests */}
                    <div className="flat-card" style={{ padding: '20px' }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px' }}>
                            <Clock size={16} style={{ color: 'var(--cyan)' }} />
                            <h2 className="text-lg font-bold" style={{ color: 'var(--text)' }}>Sent Friend Requests</h2>
                        </div>
                        {friendsInfo.pending_sent.length === 0 ? (
                            <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem' }}>No pending sent requests.</p>
                        ) : (
                            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '12px' }}>
                                {friendsInfo.pending_sent.map((entry: any) => (
                                    <div key={entry.hash} style={{ padding: '14px', borderRadius: '8px', background: 'var(--surface-2)', border: '1px solid var(--border)', display: 'flex', flexDirection: 'column', gap: '10px', justifyContent: 'space-between' }}>
                                        <div>
                                            <div style={{ fontWeight: 'bold', color: 'var(--text)', wordBreak: 'break-all' }}>{entry.username || 'Anonymous'}</div>
                                            <div style={{ fontSize: '0.75rem', color: 'var(--text-dim)', fontFamily: 'var(--font-mono)', overflow: 'hidden', textOverflow: 'ellipsis' }}>{entry.hash}</div>
                                        </div>
                                        <button onClick={() => handleCancelFriendHash(entry.hash)} className="btn-v2 text-xs" style={{ padding: '6px 12px' }}>Cancel Request</button>
                                    </div>
                                ))}
                            </div>
                        )}
                    </div>

                    {/* Friends */}
                    <div className="flat-card" style={{ padding: '20px' }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px' }}>
                            <Users size={16} style={{ color: 'var(--cyan)' }} />
                            <h2 className="text-lg font-bold" style={{ color: 'var(--text)' }}>My Friends ({friendsInfo.friends.length})</h2>
                        </div>
                        {friendsInfo.friends.length === 0 ? (
                            <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem' }}>You haven't added any friends yet.</p>
                        ) : (
                            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '12px' }}>
                                {friendsInfo.friends.map((entry: any) => (
                                    <div key={entry.hash} style={{ padding: '14px', borderRadius: '8px', background: 'var(--surface-2)', border: '1px solid var(--border)', display: 'flex', flexDirection: 'column', gap: '10px', justifyContent: 'space-between' }}>
                                        <div>
                                            <div style={{ fontWeight: 'bold', color: 'var(--text)', cursor: 'pointer', wordBreak: 'break-all' }} onClick={() => navigate(`/u/${entry.hash}`)}>{entry.username || 'Anonymous'}</div>
                                            <div style={{ fontSize: '0.75rem', color: 'var(--text-dim)', fontFamily: 'var(--font-mono)', overflow: 'hidden', textOverflow: 'ellipsis' }}>{entry.hash}</div>
                                        </div>
                                        <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px' }}>
                                            <button onClick={() => navigate(`/dm?to=${encodeURIComponent(entry.hash)}`)} className="btn-v2-accent text-xs" style={{ padding: '6px 10px', flex: '1 1 auto' }}>Chat</button>
                                            <button onClick={() => handleRemoveFriendHash(entry.hash)} className="btn-v2 text-xs" style={{ padding: '6px 10px', flex: '1 1 auto' }}>Unfriend</button>
                                            <button onClick={() => handleBlockUserHash(entry.hash)} className="btn-v2 text-xs" style={{ padding: '6px 10px', flex: '1 1 auto', color: 'var(--red)' }}>Block</button>
                                        </div>
                                    </div>
                                ))}
                            </div>
                        )}
                    </div>

                    {/* Blocked Users */}
                    <div className="flat-card" style={{ padding: '20px' }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px' }}>
                            <ShieldAlert size={16} style={{ color: 'var(--red)' }} />
                            <h2 className="text-lg font-bold" style={{ color: 'var(--text)' }}>Blocked Users</h2>
                        </div>
                        {friendsInfo.blocked.length === 0 ? (
                            <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem' }}>No blocked users.</p>
                        ) : (
                            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '12px' }}>
                                {friendsInfo.blocked.map((entry: any) => (
                                    <div key={entry.hash} style={{ padding: '14px', borderRadius: '8px', background: 'var(--surface-2)', border: '1px solid var(--border)', display: 'flex', flexDirection: 'column', gap: '10px', justifyContent: 'space-between' }}>
                                        <div>
                                            <div style={{ fontWeight: 'bold', color: 'var(--text)', wordBreak: 'break-all' }}>{entry.username || 'Anonymous'}</div>
                                            <div style={{ fontSize: '0.75rem', color: 'var(--text-dim)', fontFamily: 'var(--font-mono)', overflow: 'hidden', textOverflow: 'ellipsis' }}>{entry.hash}</div>
                                        </div>
                                        <button onClick={() => handleUnblockUserHash(entry.hash)} className="btn-v2 text-xs" style={{ padding: '6px 12px' }}>Unblock</button>
                                    </div>
                                ))}
                            </div>
                        )}
                    </div>
                </div>
            ) : (
                <div style={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
                    {/* Active Badge Settings */}
                    <div className="flat-card" style={{ padding: '20px' }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px' }}>
                            <Shield size={16} style={{ color: 'var(--cyan)' }} />
                            <h2 className="text-lg font-bold" style={{ color: 'var(--text)' }}>Active Custom Tag Settings</h2>
                        </div>
                        {unlockedArray.length === 0 ? (
                            <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem' }}>
                                You haven't unlocked any custom tags yet. Purchase one below to equip it!
                            </p>
                        ) : (
                            <div>
                                <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem', marginBottom: '14px' }}>
                                    Select which of your unlocked tags you want to display next to your username in posts:
                                </p>
                                <div style={{ display: 'flex', flexWrap: 'wrap', gap: '10px' }}>
                                    {unlockedArray.map((tag: string) => {
                                        const isActive = workingProfile.custom_badge === tag;
                                        return (
                                            <div
                                                key={tag}
                                                style={{
                                                    display: 'flex',
                                                    alignItems: 'center',
                                                    gap: '12px',
                                                    padding: '10px 14px',
                                                    borderRadius: '8px',
                                                    background: 'var(--surface-2)',
                                                    border: isActive ? '1px solid var(--cyan)' : '1px solid var(--border)',
                                                }}
                                            >
                                                <span className={`identity-badge px-2.5 py-1 text-xs font-bold border rounded ${
                                                    tag === 'queen' ? 'text-fuchsia-400 border-violet-500 bg-violet-950/30' :
                                                    tag === 'daddy' ? 'text-pink-400 border-pink-500 bg-pink-950/30' :
                                                    tag === 'OG' ? 'text-amber-400 border-amber-500 bg-amber-950/30' :
                                                    tag === 'LGBT' ? 'text-emerald-400 border-emerald-500 bg-emerald-950/30' :
                                                    tag === 'VIP' ? 'text-blue-400 border-blue-500 bg-blue-950/30' :
                                                    tag === 'CHAD' ? 'text-red-400 border-red-500 bg-red-950/30' :
                                                    tag === 'DONOR' ? 'text-teal-400 border-teal-500 bg-teal-950/30' :
                                                    tag === 'PREMIUM' ? 'text-orange-400 border-orange-500 bg-orange-950/30' :
                                                    tag === 'WAIFU' ? 'text-fuchsia-400 border-fuchsia-500 bg-fuchsia-950/30' :
                                                    tag === 'SIMP' ? 'text-violet-400 border-violet-500 bg-violet-950/30' :
                                                    tag === 'ELITE' ? 'text-rose-500 border-rose-600 bg-rose-950/30' :
                                                    tag === 'BOOSTER' ? 'text-sky-400 border-sky-500 bg-sky-950/30' :
                                                    'text-zinc-400 border-zinc-500 bg-zinc-950/30'
                                                }`}>
                                                    {tag}
                                                </span>
                                                <button
                                                    onClick={() => handleEquipTag(isActive ? '' : tag)}
                                                    className={isActive ? 'btn-v2' : 'btn-v2-accent'}
                                                    style={{ padding: '6px 12px', fontSize: '0.8rem' }}
                                                    disabled={activeTagChangeLoading}
                                                >
                                                    {isActive ? 'Unequip' : 'Equip'}
                                                </button>
                                            </div>
                                        );
                                    })}
                                </div>
                            </div>
                        )}
                    </div>

                    {/* Shop Purchase Options */}
                    <div className="flat-card" style={{ padding: '20px' }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px' }}>
                            <Crown size={16} style={{ color: 'var(--gold)' }} />
                            <h2 className="text-lg font-bold" style={{ color: 'var(--text)' }}>Premium Subscriptions</h2>
                        </div>
                        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '16px', marginBottom: '24px' }}>
                            {subscriptions.map(sub => (
                                <div
                                    key={sub.id}
                                    style={{
                                        padding: '20px',
                                        borderRadius: '12px',
                                        background: 'var(--surface-2)',
                                        border: '1px solid var(--border)',
                                        display: 'flex',
                                        flexDirection: 'column',
                                        justifyContent: 'space-between',
                                        minHeight: '160px',
                                    }}
                                >
                                    <div style={{ marginBottom: '16px' }}>
                                        <h3 className="text-md font-bold" style={{ color: 'var(--text)', marginBottom: '4px' }}>{sub.name}</h3>
                                        <p style={{ color: 'var(--text-dim)', fontSize: '0.85rem' }}>{sub.desc}</p>
                                    </div>
                                    <div style={{ marginTop: 'auto', display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
                                        <span className="text-lg font-bold" style={{ color: 'var(--cyan)' }}>${sub.price.toFixed(2)} USD</span>
                                        <button
                                            onClick={() => initiatePurchase(sub.id, sub.name, sub.price, 'sub')}
                                            className="btn-v2-accent"
                                            style={{ padding: '8px 16px' }}
                                        >
                                            Buy Tier
                                        </button>
                                    </div>
                                </div>
                            ))}
                        </div>

                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px', marginTop: '12px' }}>
                            <Crown size={16} style={{ color: 'var(--gold)' }} />
                            <h2 className="text-lg font-bold" style={{ color: 'var(--text)' }}>Custom Badges & Display Tags</h2>
                        </div>
                        <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem', marginBottom: '16px' }}>
                            Stand out in the catalog and threads with a colorful display badge next to your name. All tags are a one-time purchase of $5.00 USD.
                        </p>
                        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))', gap: '14px' }}>
                            {customTags.map(tag => {
                                const isUnlocked = unlockedArray.includes(tag.id);
                                return (
                                    <div
                                        key={tag.id}
                                        style={{
                                            padding: '16px',
                                            borderRadius: '10px',
                                            background: 'var(--surface-2)',
                                            border: '1px solid var(--border)',
                                            display: 'flex',
                                            flexDirection: 'column',
                                            alignItems: 'center',
                                            gap: '12px',
                                        }}
                                    >
                                        <span className={`identity-badge px-2.5 py-1 text-xs font-bold border rounded ${
                                            tag.id === 'queen' ? 'text-fuchsia-400 border-violet-500 bg-violet-950/30' :
                                            tag.id === 'daddy' ? 'text-pink-400 border-pink-500 bg-pink-950/30' :
                                            tag.id === 'OG' ? 'text-amber-400 border-amber-500 bg-amber-950/30' :
                                            tag.id === 'LGBT' ? 'text-emerald-400 border-emerald-500 bg-emerald-950/30' :
                                            tag.id === 'VIP' ? 'text-blue-400 border-blue-500 bg-blue-950/30' :
                                            tag.id === 'CHAD' ? 'text-red-400 border-red-500 bg-red-950/30' :
                                            tag.id === 'DONOR' ? 'text-teal-400 border-teal-500 bg-teal-950/30' :
                                            tag.id === 'PREMIUM' ? 'text-orange-400 border-orange-500 bg-orange-950/30' :
                                            tag.id === 'WAIFU' ? 'text-fuchsia-400 border-fuchsia-500 bg-fuchsia-950/30' :
                                            tag.id === 'SIMP' ? 'text-violet-400 border-violet-500 bg-violet-950/30' :
                                            tag.id === 'ELITE' ? 'text-rose-500 border-rose-600 bg-rose-950/30' :
                                            tag.id === 'BOOSTER' ? 'text-sky-400 border-sky-500 bg-sky-950/30' :
                                            'text-zinc-400 border-zinc-500 bg-zinc-950/30'
                                        }`}>
                                            {tag.name}
                                        </span>
                                        <p style={{ color: 'var(--text-dim)', fontSize: '0.75rem', textAlign: 'center' }}>{tag.desc}</p>
                                        <div style={{ width: '100%', display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginTop: 'auto', paddingTop: '8px' }}>
                                            <span style={{ fontSize: '0.85rem', fontWeight: 'bold', color: 'var(--text)' }}>$5.00</span>
                                            {isUnlocked ? (
                                                <span className="text-xs font-semibold" style={{ color: 'var(--green)' }}>Unlocked</span>
                                            ) : (
                                                <button
                                                    onClick={() => initiatePurchase(tag.id, tag.name, tag.price, 'tag')}
                                                    className="btn-v2 text-xs"
                                                    style={{ padding: '6px 12px' }}
                                                >
                                                    Buy Tag
                                                </button>
                                            )}
                                        </div>
                                    </div>
                                );
                            })}
                        </div>
                    </div>

                    {/* Invoice Panel / Overlay */}
                    {selectedProduct && (
                        <div
                            style={{
                                position: 'fixed',
                                top: 0,
                                left: 0,
                                right: 0,
                                bottom: 0,
                                zIndex: 100,
                                background: 'rgba(0,0,0,0.6)',
                                backdropFilter: 'blur(4px)',
                                display: 'flex',
                                alignItems: 'center',
                                justifyContent: 'center',
                                padding: '16px',
                            }}
                            onClick={() => {
                                if (!paymentLoading) {
                                    setSelectedProduct(null);
                                    setInvoice(null);
                                }
                            }}
                        >
                            <div
                                style={{
                                    background: 'var(--surface-1)',
                                    border: '1px solid var(--border)',
                                    borderRadius: '16px',
                                    padding: '24px',
                                    maxWidth: '480px',
                                    width: '100%',
                                    boxShadow: '0 20px 40px rgba(0,0,0,0.4)',
                                }}
                                onClick={e => e.stopPropagation()}
                            >
                                <h3 className="text-lg font-bold" style={{ color: 'var(--text)', marginBottom: '8px' }}>
                                    Purchase: {selectedProduct.name}
                                </h3>
                                <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem', marginBottom: '20px' }}>
                                    Amount Due: <span style={{ color: 'var(--cyan)', fontWeight: 'bold' }}>${selectedProduct.price.toFixed(2)} USD</span>
                                </p>

                                {!invoice ? (
                                    <div>
                                        <label style={{ display: 'block', color: 'var(--text-dim)', fontSize: '0.8rem', marginBottom: '8px', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
                                            Choose Payment Cryptocurrency
                                        </label>
                                        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '8px', marginBottom: '20px' }}>
                                            {(['btc', 'ltc', 'xmr'] as const).map(curr => (
                                                <button
                                                    key={curr}
                                                    onClick={() => setPayCurrency(curr)}
                                                    style={{
                                                        padding: '12px 8px',
                                                        borderRadius: '8px',
                                                        background: payCurrency === curr ? 'var(--surface-3)' : 'var(--surface-2)',
                                                        border: payCurrency === curr ? '1px solid var(--cyan)' : '1px solid var(--border)',
                                                        color: payCurrency === curr ? 'var(--cyan)' : 'var(--text)',
                                                        textTransform: 'uppercase',
                                                        fontWeight: 'bold',
                                                        cursor: 'pointer',
                                                    }}
                                                >
                                                    {curr}
                                                </button>
                                            ))}
                                        </div>

                                        {paymentError && (
                                            <p style={{ color: 'var(--red)', fontSize: '0.85rem', marginBottom: '12px' }}>{paymentError}</p>
                                        )}

                                        <div style={{ display: 'flex', gap: '8px', justifyContent: 'flex-end' }}>
                                            <button
                                                onClick={() => setSelectedProduct(null)}
                                                className="btn-v2"
                                                style={{ padding: '10px 16px' }}
                                                disabled={paymentLoading}
                                            >
                                                Cancel
                                            </button>
                                            <button
                                                onClick={handleCreateInvoice}
                                                className="btn-v2-accent"
                                                style={{ padding: '10px 16px' }}
                                                disabled={paymentLoading}
                                            >
                                                {paymentLoading ? 'Generating Invoice...' : 'Generate Invoice'}
                                            </button>
                                        </div>
                                    </div>
                                ) : (
                                    <div>
                                        <div
                                            style={{
                                                padding: '16px',
                                                borderRadius: '8px',
                                                background: 'var(--surface-2)',
                                                border: '1px dashed var(--border)',
                                                marginBottom: '20px',
                                            }}
                                        >
                                            <p style={{ fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '4px' }}>
                                                Send exactly:
                                            </p>
                                            <p style={{ fontSize: '1.2rem', fontFamily: 'var(--font-mono)', fontWeight: 'bold', color: 'var(--text)', marginBottom: '12px' }}>
                                                {invoice.pay_amount} <span style={{ textTransform: 'uppercase' }}>{invoice.pay_currency}</span>
                                            </p>
                                            <p style={{ fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '4px' }}>
                                                To Address:
                                            </p>
                                            <p
                                                style={{
                                                    fontSize: '0.78rem',
                                                    fontFamily: 'var(--font-mono)',
                                                    background: 'var(--surface-3)',
                                                    padding: '8px 10px',
                                                    borderRadius: '4px',
                                                    wordBreak: 'break-all',
                                                    color: 'var(--cyan)',
                                                }}
                                            >
                                                {invoice.pay_address}
                                            </p>
                                        </div>

                                        <div
                                            style={{
                                                padding: '12px',
                                                background: 'rgba(234, 179, 8, 0.05)',
                                                border: '1px solid rgba(234, 179, 8, 0.2)',
                                                borderRadius: '8px',
                                                marginBottom: '20px',
                                            }}
                                        >
                                            <p style={{ fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '8px' }}>
                                                <strong>Sandbox Testing:</strong> Click below to simulate a successful cryptocurrency confirmation immediately:
                                            </p>
                                            <button
                                                onClick={handleSimulateSuccess}
                                                className="btn-v2-accent w-full"
                                                style={{ padding: '8px 12px', fontSize: '0.85rem' }}
                                            >
                                                Simulate Payment Success
                                            </button>
                                            {simulationStatus && (
                                                <p style={{ marginTop: '8px', fontSize: '0.8rem', color: 'var(--green)', fontWeight: 'bold' }}>
                                                    {simulationStatus}
                                                </p>
                                            )}
                                        </div>

                                        <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
                                            <button
                                                onClick={() => {
                                                    setSelectedProduct(null);
                                                    setInvoice(null);
                                                }}
                                                className="btn-v2"
                                                style={{ padding: '10px 16px' }}
                                            >
                                                Close Window
                                            </button>
                                        </div>
                                    </div>
                                )}
                            </div>
                        </div>
                    )}
                </div>
            )}
        </motion.div>
    );
}
