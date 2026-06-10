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
import React, { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Clock, MoreVertical, ShieldCheck, ShieldAlert, Key, Calendar, MessageCircle, Reply, ThumbsUp, ThumbsDown, Share2, Link2, Flag, Trash2, X } from 'lucide-react';
import type { Post as PostType } from '../types';
import { useStore } from '../store/useStore';
import { useIdentity } from '../hooks/useIdentity';
import { decryptPost, isEncryptedPostPayload } from '../utils/crypto';
import { parsePublicIdentityMarker } from '../utils/publicIdentity';
import { getFounderToken, normalizeProfileRole } from '../utils/roleAuth';

interface PostProps {
    post: PostType;
    isOP?: boolean;
    verified?: boolean | null;
    cipherSuite?: string;
    depth?: number;
    allPosts?: PostType[];
    onReplyTo?: (postNo: number) => void;
}

function formatTime(ts: number): string {
    const d = new Date(ts);
    return d.toLocaleDateString('en-US', { month: '2-digit', day: '2-digit', year: '2-digit' }) +
        ' ' + d.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false });
}

function shortHash(value: string): string {
    if (!value) return '';
    if (value.length <= 12) return value;
    return `${value.slice(0, 8)}...`;
}

function Identicon({ seed }: { seed: string }) {
    let hash = 0;
    for (let i = 0; i < seed.length; i++) hash = seed.charCodeAt(i) + ((hash << 5) - hash);
    const c = (hash & 0x00FFFFFF).toString(16).toUpperCase();
    const color = '#' + '00000'.substring(0, 6 - c.length) + c;
    const grid = [];
    for (let row = 0; row < 5; row++) {
        const rHash = hash >> (row * 3);
        grid.push([(rHash & 1) === 1, ((rHash >> 1) & 1) === 1, ((rHash >> 2) & 1) === 1, ((rHash >> 1) & 1) === 1, (rHash & 1) === 1]);
    }
    return (
        <svg width="18" height="18" viewBox="0 0 5 5" style={{ borderRadius: '3px', background: 'var(--surface-2)', border: '1px solid var(--border)' }}>
            {grid.map((row, y) => row.map((active, x) => (
                active ? <rect key={`${x}-${y}`} fill={color} x={x} y={y} width="1" height="1" /> : null
            )))}
        </svg>
    );
}

function parseContent(content: string): React.ReactNode[] {
    return content.split('\n').map((line, i) => {
        const parts: React.ReactNode[] = [];
        const blRegex = />>(\d+)/g;
        let match;
        let lastIndex = 0;
        let key = 0;

        while ((match = blRegex.exec(line)) !== null) {
            if (match.index > lastIndex) parts.push(line.substring(lastIndex, match.index));
            const id = match[1];
            parts.push(<a key={`bl-${i}-${key++}`} href={`#p${id}`} className="backlink">{`>>${id}`}</a>);
            lastIndex = match.index + match[0].length;
        }
        if (lastIndex < line.length) parts.push(line.substring(lastIndex));

        const isGreentext = line.trimStart().startsWith('>') && !line.trimStart().startsWith('>>');
        return (
            <div key={i} className={isGreentext ? 'greentext' : ''}>
                {parts.length > 0 ? parts : line}
            </div>
        );
    });
}

function hasBacklinkToPost(content: string, postNo: number): boolean {
    const backlinkRegex = />>(\d+)/g;
    let match: RegExpExecArray | null;

    while ((match = backlinkRegex.exec(content)) !== null) {
        if (Number(match[1]) === postNo) return true;
    }

    return false;
}

export default function Post({
    post, isOP = false, verified = null, cipherSuite = 'AES-256-GCM',
    depth = 0, allPosts = [], onReplyTo,
}: PostProps) {
    const navigate = useNavigate();
    const { interactPost, createReport, deletePostAsModerator } = useStore();
    const identity = useIdentity();

    const [menuOpen, setMenuOpen] = useState(false);
    const [modalOpen, setModalOpen] = useState(false);
    const [reportOpen, setReportOpen] = useState(false);
    const [imgExpanded, setImgExpanded] = useState(false);
    const [likes, setLikes] = useState(0);
    const [dislikes, setDislikes] = useState(0);
    const [decryptPassphrase, setDecryptPassphrase] = useState('');
    const [decryptedContent, setDecryptedContent] = useState('');
    const [decryptError, setDecryptError] = useState('');
    const [decrypting, setDecrypting] = useState(false);
    const [shareStatus, setShareStatus] = useState('');
    const [reportReason, setReportReason] = useState('');
    const [reportStatus, setReportStatus] = useState('');
    const [reportSubmitting, setReportSubmitting] = useState(false);
    const [moderationStatus, setModerationStatus] = useState('');
    const [moderating, setModerating] = useState(false);
    const [hiddenState, setHiddenState] = useState<'none' | 'post' | 'thread'>('none');
    const [viewerRole, setViewerRole] = useState<'user' | 'moderator' | 'founder'>(() => normalizeProfileRole(localStorage.getItem('quanchan_self_role') || undefined));

    const parsedIdentity = parsePublicIdentityMarker(post.name, post.tripcode);
    let displayName = post.name || 'Anonymous';
    let tripcode = post.tripcode || '';
    let dmTarget: string | null = null;

    if (parsedIdentity.isIdentity) {
        displayName = parsedIdentity.label;
        tripcode = parsedIdentity.stableHash ? `#${shortHash(parsedIdentity.stableHash)}` : '#Identity';
        dmTarget = parsedIdentity.target;
    } else if (displayName.includes('!')) {
        const parts = displayName.split('!');
        displayName = parts[0];
        tripcode = '!' + parts[1];
    } else if (tripcode && !tripcode.startsWith('!')) {
        tripcode = '!' + tripcode;
    }

    const seedStr = tripcode || displayName;
    const isNotAnonymous = dmTarget || (post.name !== 'Anonymous' && post.name !== '' && !post.name.startsWith('Anonymous'));
    const canModerate = Boolean(identity.identity?.displayHash) && (viewerRole === 'founder' || viewerRole === 'moderator');

    const children = allPosts.filter(p =>
        p.id !== post.id && hasBacklinkToPost(p.content, post.no)
    );

    useEffect(() => {
        const onRoleUpdate = (event: Event) => {
            const role = (event as CustomEvent<{ role?: string }>).detail?.role;
            setViewerRole(normalizeProfileRole(role));
        };

        window.addEventListener('quanchan:self-role', onRoleUpdate as EventListener);
        return () => {
            window.removeEventListener('quanchan:self-role', onRoleUpdate as EventListener);
        };
    }, []);

    async function sharePostLink() {
        const postUrl = `${window.location.origin}/${encodeURIComponent(post.boardId)}/thread/${post.threadId}#p${post.no}`;
        try {
            if (navigator.share) {
                await navigator.share({
                    title: `QuanChan /${post.boardId}/ No.${post.no}`,
                    text: `QuanChan post No.${post.no}`,
                    url: postUrl,
                });
                setShareStatus('Shared');
            } else {
                await navigator.clipboard.writeText(postUrl);
                setShareStatus('Link copied');
            }
        } catch (error) {
            if (error instanceof Error && error.name === 'AbortError') {
                return;
            }
            try {
                await navigator.clipboard.writeText(postUrl);
                setShareStatus('Link copied');
            } catch {
                setShareStatus('Unable to share');
            }
        } finally {
            window.setTimeout(() => setShareStatus(''), 2000);
        }
    }

    async function handleDecrypt() {
        if (!post.encryptedContent) return;
        if (!isEncryptedPostPayload(post.encryptedContent)) {
            setDecryptError('This encrypted post uses an older unsupported format.');
            return;
        }

        try {
            setDecrypting(true);
            setDecryptError('');
            const plaintext = await decryptPost(post.encryptedContent, decryptPassphrase);
            setDecryptedContent(plaintext);
        } catch {
            setDecryptError('Decryption failed. Check the passphrase and try again.');
            setDecryptedContent('');
        } finally {
            setDecrypting(false);
        }
    }

    async function handleReportSubmit() {
        if (!identity.identity?.displayHash || !reportReason.trim()) return;
        try {
            setReportSubmitting(true);
            setReportStatus('');
            await createReport(identity.identity.displayHash, dmTarget || '', reportReason.trim(), {
                targetKind: 'post',
                targetPostId: post.id,
                targetThreadId: post.threadId,
                targetBoardId: post.boardId,
                targetDisplayName: displayName,
                contextLink: `/${post.boardId}/thread/${post.threadId}#p${post.no}`,
            });
            setReportReason('');
            setReportStatus('Report submitted.');
        } catch (error) {
            setReportStatus(error instanceof Error ? error.message : 'Could not submit report.');
        } finally {
            setReportSubmitting(false);
        }
    }

    async function handleDeletePost() {
        if (!identity.identity?.displayHash || !canModerate) return;
        try {
            setModerating(true);
            setModerationStatus('');
            const result = await deletePostAsModerator(post.id, identity.identity.displayHash, viewerRole === 'founder' ? getFounderToken() : '');
            setHiddenState(result?.threadDeleted ? 'thread' : 'post');
            setModerationStatus(result?.threadDeleted ? 'Thread removed.' : 'Post removed.');
            setMenuOpen(false);
        } catch (error) {
            setModerationStatus(error instanceof Error ? error.message : 'Could not remove post.');
        } finally {
            setModerating(false);
        }
    }

    if (hiddenState !== 'none') {
        return (
            <div className="flat-card" style={{ padding: '14px 16px', color: 'var(--text-dim)', marginBottom: '12px' }}>
                {hiddenState === 'thread' ? 'Thread removed by moderator.' : 'Post removed by moderator.'}
            </div>
        );
    }

    return (
        <div>
            <div className={`post-card ${isOP ? 'is-op' : ''}`} id={`p${post.no}`}>
                <div className="flex flex-wrap items-center justify-between gap-2 mb-2">
                    <div className="flex items-center gap-2 flex-wrap">
                        <Identicon seed={seedStr} />
                        <span className="text-sm font-semibold" style={{ color: 'var(--text)' }}>
                            {displayName}
                        </span>
                        {tripcode && (<span className="identity-badge">{tripcode}</span>)}
                        {post.subscriptionTier === 'circle' && (
                            <span className="identity-badge px-2 py-0.5 text-xs font-bold text-cyan-400 border border-cyan-400 rounded bg-cyan-950/30">
                                Circle
                            </span>
                        )}
                        {post.subscriptionTier === 'hermes' && (
                            <span className="identity-badge px-2 py-0.5 text-xs font-bold text-purple-400 border border-purple-400 rounded bg-purple-950/30">
                                Hermes
                            </span>
                        )}
                        {post.customBadge && (
                            <span className={`identity-badge px-2 py-0.5 text-xs font-bold border rounded ${
                                post.customBadge === 'queen' ? 'text-fuchsia-400 border-violet-500 bg-violet-950/30' :
                                post.customBadge === 'daddy' ? 'text-pink-400 border-pink-500 bg-pink-950/30' :
                                post.customBadge === 'OG' ? 'text-amber-400 border-amber-500 bg-amber-950/30' :
                                post.customBadge === 'LGBT' ? 'text-emerald-400 border-emerald-500 bg-emerald-950/30' :
                                post.customBadge === 'VIP' ? 'text-blue-400 border-blue-500 bg-blue-950/30' :
                                post.customBadge === 'CHAD' ? 'text-red-400 border-red-500 bg-red-950/30' :
                                post.customBadge === 'DONOR' ? 'text-teal-400 border-teal-500 bg-teal-950/30' :
                                post.customBadge === 'PREMIUM' ? 'text-orange-400 border-orange-500 bg-orange-950/30' :
                                post.customBadge === 'WAIFU' ? 'text-fuchsia-400 border-fuchsia-500 bg-fuchsia-950/30' :
                                post.customBadge === 'SIMP' ? 'text-violet-400 border-violet-500 bg-violet-950/30' :
                                post.customBadge === 'ELITE' ? 'text-rose-500 border-rose-600 bg-rose-950/30' :
                                post.customBadge === 'BOOSTER' ? 'text-sky-400 border-sky-500 bg-sky-950/30' :
                                'text-zinc-400 border-zinc-500 bg-zinc-950/30'
                            }`}>
                                {post.customBadge}
                            </span>
                        )}

                        {isNotAnonymous && dmTarget && (
                            <button
                                className="btn-amber"
                                onClick={() => navigate(`/dm?to=${encodeURIComponent(dmTarget)}`)}
                            >
                                Message
                            </button>
                        )}

                        <span className="text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                            No.{post.no}
                        </span>
                        <span className="flex items-center gap-1 text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                            <Clock size={10} />
                            {formatTime(post.timestamp)}
                        </span>
                        {isOP && (
                            <span className="text-xs font-bold px-2 py-1" style={{ background: 'var(--surface-2)', color: 'var(--accent)', border: '1px solid var(--border)', borderRadius: '4px' }}>
                                OP
                            </span>
                        )}
                    </div>

                    <div className="flex items-center gap-1">
                        {onReplyTo && (
                            <button
                                onClick={() => onReplyTo(post.no)}
                                className="btn-v2 flex items-center gap-1 text-xs"
                                style={{ padding: '3px 8px' }}
                                title="Reply to this post"
                            >
                                <Reply size={12} /> Reply
                            </button>
                        )}
                        <button
                            onClick={sharePostLink}
                            className="btn-v2 flex items-center gap-1 text-xs"
                            style={{ padding: '3px 8px' }}
                            title="Share this post"
                        >
                            <Share2 size={12} /> Share
                        </button>

                        <div className="relative">
                            <button onClick={() => setMenuOpen(!menuOpen)} style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--text-dim)', padding: '4px' }}>
                                <MoreVertical size={14} />
                            </button>
                            {menuOpen && (
                                <div className="absolute z-20" style={{ right: 0, top: '100%', marginTop: '4px', width: '180px', background: 'var(--surface)', border: '1px solid var(--border)', borderRadius: '6px', overflow: 'hidden' }}>
                                    <button
                                        onClick={() => { setModalOpen(true); setMenuOpen(false); }}
                                        className="w-full text-sm flex items-center gap-2"
                                        style={{ textAlign: 'left', padding: '8px 12px', color: 'var(--text-muted)', background: 'none', border: 'none', cursor: 'pointer' }}
                                        onMouseEnter={e => (e.currentTarget.style.background = 'var(--surface-2)')}
                                        onMouseLeave={e => (e.currentTarget.style.background = 'none')}
                                    >
                                        <ShieldCheck size={14} style={{ color: 'var(--cyan)' }} />
                                        Security Details
                                    </button>
                                    {dmTarget && (
                                        <button
                                            onClick={() => { navigate(`/dm?to=${encodeURIComponent(dmTarget)}`); setMenuOpen(false); }}
                                            className="w-full text-sm flex items-center gap-2"
                                            style={{ textAlign: 'left', padding: '8px 12px', color: 'var(--text-muted)', background: 'none', border: 'none', cursor: 'pointer' }}
                                            onMouseEnter={e => (e.currentTarget.style.background = 'var(--surface-2)')}
                                            onMouseLeave={e => (e.currentTarget.style.background = 'none')}
                                        >
                                            <MessageCircle size={14} style={{ color: 'var(--accent)' }} />
                                            Direct Message
                                        </button>
                                    )}
                                    <button
                                        onClick={() => { sharePostLink().catch(() => {}); setMenuOpen(false); }}
                                        className="w-full text-sm flex items-center gap-2"
                                        style={{ textAlign: 'left', padding: '8px 12px', color: 'var(--text-muted)', background: 'none', border: 'none', cursor: 'pointer' }}
                                        onMouseEnter={e => (e.currentTarget.style.background = 'var(--surface-2)')}
                                        onMouseLeave={e => (e.currentTarget.style.background = 'none')}
                                    >
                                        <Link2 size={14} style={{ color: 'var(--green)' }} />
                                        Copy Post Link
                                    </button>
                                    <button
                                        onClick={() => { setReportOpen(true); setMenuOpen(false); }}
                                        className="w-full text-sm flex items-center gap-2"
                                        style={{ textAlign: 'left', padding: '8px 12px', color: 'var(--text-muted)', background: 'none', border: 'none', cursor: 'pointer' }}
                                        onMouseEnter={e => (e.currentTarget.style.background = 'var(--surface-2)')}
                                        onMouseLeave={e => (e.currentTarget.style.background = 'none')}
                                    >
                                        <Flag size={14} style={{ color: 'var(--gold)' }} />
                                        Report Post
                                    </button>
                                    {canModerate && (
                                        <button
                                            onClick={() => { handleDeletePost().catch(() => {}); }}
                                            className="w-full text-sm flex items-center gap-2"
                                            style={{ textAlign: 'left', padding: '8px 12px', color: 'var(--red)', background: 'none', border: 'none', cursor: 'pointer' }}
                                            onMouseEnter={e => (e.currentTarget.style.background = 'var(--surface-2)')}
                                            onMouseLeave={e => (e.currentTarget.style.background = 'none')}
                                        >
                                            <Trash2 size={14} />
                                            {moderating ? 'Removing...' : (isOP ? 'Delete Thread' : 'Delete Post')}
                                        </button>
                                    )}
                                </div>
                            )}
                        </div>
                    </div>
                </div>
                {shareStatus && (
                    <div className="mb-2 text-xs font-mono" style={{ color: shareStatus.includes('Unable') ? 'var(--red)' : 'var(--green)' }}>
                        {shareStatus}
                    </div>
                )}

                {post.imageUrl && (
                    <div className="mb-2 float-left mr-3">
                        <img
                            src={post.imageUrl} alt=""
                            className="expand-img"
                            style={{
                                maxWidth: imgExpanded ? '100%' : '150px',
                                maxHeight: imgExpanded ? 'none' : '150px',
                                border: '1px solid var(--border)',
                                cursor: 'pointer', borderRadius: '4px',
                            }}
                            onClick={() => setImgExpanded(!imgExpanded)}
                            onError={e => (e.currentTarget.style.display = 'none')}
                        />
                    </div>
                )}

                <div style={{ fontFamily: 'var(--font-sans)', fontSize: '0.875rem', lineHeight: 1.7, color: 'var(--text)' }}>
                    {post.isEncrypted && post.encryptedContent ? (
                        <div style={{ display: 'grid', gap: '10px' }}>
                            <div style={{ color: 'var(--cyan)', fontFamily: 'var(--font-mono)', fontSize: '0.8rem' }}>
                                Encrypted post payload. Enter the shared passphrase to decrypt locally.
                            </div>
                            {decryptedContent ? (
                                <div>
                                    {parseContent(decryptedContent)}
                                </div>
                            ) : (
                                <>
                                    <div style={{ display: 'flex', gap: '8px', flexWrap: 'wrap' }}>
                                        <input
                                            type="password"
                                            className="v2-input"
                                            style={{ padding: '8px 10px', minWidth: '220px', flex: 1 }}
                                            placeholder="Passphrase"
                                            value={decryptPassphrase}
                                            onChange={e => setDecryptPassphrase(e.target.value)}
                                        />
                                        <button
                                            type="button"
                                            className="btn-v2-accent"
                                            style={{ padding: '8px 12px' }}
                                            onClick={handleDecrypt}
                                            disabled={decrypting}
                                        >
                                            {decrypting ? 'Decrypting...' : 'Decrypt'}
                                        </button>
                                    </div>
                                    {decryptError && (
                                        <div style={{ color: 'var(--red)', fontSize: '0.78rem' }}>
                                            {decryptError}
                                        </div>
                                    )}
                                </>
                            )}
                        </div>
                    ) : (
                        parseContent(post.content)
                    )}
                </div>
                <div style={{ clear: 'both' }} />

                <div className="mt-4 flex items-center gap-4 border-t border-black/20 pt-2" style={{ flexWrap: 'wrap' }}>
                    <button
                        onClick={async () => {
                            if (!identity.identity?.displayHash) return;
                            const res = await interactPost(post.id, identity.identity.displayHash, 1);
                            setLikes(res.likes);
                            setDislikes(res.dislikes);
                        }}
                        className="flex items-center gap-1.5 text-xs text-gray-400 hover:text-green-500 transition-colors"
                    >
                        <ThumbsUp size={14} /> {likes > 0 && likes}
                    </button>
                    <button
                        onClick={async () => {
                            if (!identity.identity?.displayHash) return;
                            const res = await interactPost(post.id, identity.identity.displayHash, -1);
                            setLikes(res.likes);
                            setDislikes(res.dislikes);
                        }}
                        className="flex items-center gap-1.5 text-xs text-gray-400 hover:text-red-500 transition-colors"
                    >
                        <ThumbsDown size={14} /> {dislikes > 0 && dislikes}
                    </button>
                    <button
                        onClick={() => setReportOpen(true)}
                        className="flex items-center gap-1.5 text-xs text-gray-400 hover:text-yellow-400 transition-colors"
                    >
                        <Flag size={14} /> Report
                    </button>
                </div>
                {moderationStatus && (
                    <div className="mt-2 text-xs font-mono" style={{ color: moderationStatus.includes('removed') ? 'var(--green)' : 'var(--red)' }}>
                        {moderationStatus}
                    </div>
                )}

                {modalOpen && (
                    <div className="fixed inset-0 z-50 flex items-start justify-center overflow-y-auto p-4" style={{ background: 'rgba(0,0,0,0.7)' }} onClick={() => setModalOpen(false)}>
                        <div className="flat-card" style={{ width: '100%', maxWidth: '380px', maxHeight: 'calc(100dvh - 32px)', overflowY: 'auto', margin: 'auto 0' }} onClick={e => e.stopPropagation()}>
                            <div className="p-4 flex items-center justify-between" style={{ borderBottom: '1px solid var(--border)' }}>
                                <h3 className="font-semibold flex items-center gap-2 text-sm" style={{ color: 'var(--text)' }}>
                                    <ShieldCheck size={16} style={{ color: 'var(--cyan)' }} /> Security Details
                                </h3>
                                <button onClick={() => setModalOpen(false)} style={{ background: 'none', border: 'none', color: 'var(--text-dim)', cursor: 'pointer', padding: 0 }}>
                                    <X size={16} />
                                </button>
                            </div>
                            <div className="p-4 space-y-4 font-mono text-sm">
                                <div>
                                    <p className="text-xs uppercase tracking-wider mb-1" style={{ color: 'var(--text-dim)' }}>Dilithium5 Signature</p>
                                    <div className="flex items-center gap-2">
                                        {verified === true ? (
                                            <><ShieldCheck size={14} style={{ color: 'var(--green)' }} /> <span style={{ color: 'var(--green)' }}>Verified by WASM</span></>
                                        ) : verified === false ? (
                                            <><ShieldAlert size={14} style={{ color: 'var(--red)' }} /> <span style={{ color: 'var(--red)' }}>Verification Failed</span></>
                                        ) : (
                                            <><ShieldCheck size={14} style={{ color: 'var(--text-dim)' }} /> <span style={{ color: 'var(--text-dim)' }}>Unverified (Fallback)</span></>
                                        )}
                                    </div>
                                </div>
                                <div>
                                    <p className="text-xs uppercase tracking-wider mb-1" style={{ color: 'var(--text-dim)' }}>Session Transport</p>
                                    <div className="flex items-start gap-2" style={{ color: 'var(--text-muted)', overflowWrap: 'anywhere', wordBreak: 'break-word' }}>
                                        <Key size={14} style={{ color: 'var(--cyan)', marginTop: '2px', flexShrink: 0 }} /> {cipherSuite}
                                    </div>
                                </div>
                                <div>
                                    <p className="text-xs uppercase tracking-wider mb-1" style={{ color: 'var(--text-dim)' }}>Timestamp</p>
                                    <div className="flex items-center gap-2 text-xs" style={{ color: 'var(--text-dim)' }}>
                                        <Calendar size={12} /> {formatTime(post.timestamp)}
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                )}

                {reportOpen && (
                    <div className="fixed inset-0 z-50 flex items-start justify-center overflow-y-auto p-4" style={{ background: 'rgba(0,0,0,0.7)' }} onClick={() => setReportOpen(false)}>
                        <div className="flat-card" style={{ width: '100%', maxWidth: '420px', maxHeight: 'calc(100dvh - 32px)', overflowY: 'auto', margin: 'auto 0' }} onClick={e => e.stopPropagation()}>
                            <div className="p-4 flex items-center justify-between" style={{ borderBottom: '1px solid var(--border)' }}>
                                <h3 className="font-semibold flex items-center gap-2 text-sm" style={{ color: 'var(--text)' }}>
                                    <Flag size={16} style={{ color: 'var(--gold)' }} /> Report Post
                                </h3>
                                <button onClick={() => setReportOpen(false)} style={{ background: 'none', border: 'none', color: 'var(--text-dim)', cursor: 'pointer', padding: 0 }}>
                                    <X size={16} />
                                </button>
                            </div>
                            <div className="p-4 space-y-3">
                                <p className="text-xs" style={{ color: 'var(--text-dim)', lineHeight: 1.5 }}>
                                    Reporting post #{post.no} on /{post.boardId}/. Moderators will get a direct link to this post.
                                </p>
                                <textarea
                                    className="v2-input w-full text-sm"
                                    style={{ minHeight: '108px', padding: '10px 12px', resize: 'vertical' }}
                                    placeholder="Explain what happened..."
                                    value={reportReason}
                                    onChange={e => {
                                        setReportReason(e.target.value);
                                        if (reportStatus) setReportStatus('');
                                    }}
                                />
                                <button
                                    type="button"
                                    className="btn-v2-accent text-sm w-full"
                                    style={{ padding: '10px 12px' }}
                                    onClick={() => handleReportSubmit().catch(() => {})}
                                    disabled={reportSubmitting || !identity.identity?.displayHash || !reportReason.trim()}
                                >
                                    {reportSubmitting ? 'Submitting...' : 'Submit Report'}
                                </button>
                                <p className="text-xs" style={{ color: reportStatus ? (reportStatus.includes('submitted') ? 'var(--green)' : 'var(--red)') : 'var(--text-dim)', lineHeight: 1.5 }}>
                                    {reportStatus || 'Signed-in identity required to file a report.'}
                                </p>
                            </div>
                        </div>
                    </div>
                )}
            </div>

            {children.length > 0 && depth < 5 && (
                <div className="reply-thread">
                    {children.map(child => (
                        <div key={child.id} className="mt-2">
                            <Post
                                post={child}
                                verified={verified}
                                cipherSuite={cipherSuite}
                                depth={depth + 1}
                                allPosts={allPosts}
                                onReplyTo={onReplyTo}
                            />
                        </div>
                    ))}
                </div>
            )}
        </div>
    );
}
