import { Outlet, Link, useLocation, useNavigate, matchPath } from 'react-router-dom';
import { useStore } from '../store/useStore';
import { useIdentity } from '../hooks/useIdentity';
import { useIsPhone } from '../hooks/useIsPhone';
import { encryptPost } from '../utils/crypto';
import { buildSignedIdentityBinding } from '../utils/identityBinding';
import { getFounderToken, normalizeProfileRole } from '../utils/roleAuth';
import { encryptRecoveryVault } from '../utils/recoveryVault';
import { subscribeToLiveEvent } from '../utils/liveEvents';
import QuantumModal from './QuantumModal';
import { useState, useEffect, type ReactNode } from 'react';
import { Hash, Shield, Eye, EyeOff, Copy, X, MessageCircle, Sun, Moon, Palette, User, ArrowLeft, Menu, Crown, Bell, House } from 'lucide-react';

const SELF_ROLE_STORAGE_KEY = 'quanchan_self_role';

function shortHash(value: string) {
    if (!value) return '';
    if (value.length <= 14) return value;
    return `${value.slice(0, 10)}...`;
}

function truncateTitle(value: string, limit = 28) {
    if (value.length <= limit) return value;
    return `${value.slice(0, limit - 3)}...`;
}

export default function Layout() {
    const {
        quantumModalVisible, setQuantumModalVisible,
        pendingPost, setPendingPost,
        createThread, createReply,
        boards,
    } = useStore();
    const threads = useStore(s => s.threads);
    const boardsState = useStore(s => s.boardsState);
    const boardsError = useStore(s => s.boardsError);
    const fetchBoards = useStore(s => (s as any).fetchBoards);
    const getNotificationSummary = useStore(s => (s as any).getNotificationSummary as (hash: string) => Promise<{ notificationsUnread: number; friendRequestsPending: number; messageRequestsPending: number; dmUnread: number; total: number }>);
    const { identity, setUsername, restoreIdentityFromRecoveryPhrase } = useIdentity();
    const [usernameInput, setUsernameInput] = useState(identity?.username || '');
    const [usernameStatus, setUsernameStatus] = useState('');
    const location = useLocation();
    const navigate = useNavigate();
    const isPhone = useIsPhone();

    const [stats, setStats] = useState({ boards: 0, threads: 0, posts: 0, encrypted: 0, users: 0, namedUsers: 0 });
    const [statsState, setStatsState] = useState<'idle' | 'loading' | 'ready' | 'error'>('idle');
    const [statsError, setStatsError] = useState('');
    const [notificationSummary, setNotificationSummary] = useState({ notificationsUnread: 0, friendRequestsPending: 0, messageRequestsPending: 0, dmUnread: 0, total: 0 });
    const [identityModalOpen, setIdentityModalOpen] = useState(false);
    const [seedVisible, setSeedVisible] = useState(false);
    const [copied, setCopied] = useState('');
    const [mobileDmLabel, setMobileDmLabel] = useState('');
    const [mobileUtilityOpen, setMobileUtilityOpen] = useState(false);
    const [selfRole, setSelfRole] = useState<'user' | 'moderator' | 'founder'>('user');
    const [restorePhrase, setRestorePhrase] = useState('');
    const [restoreStatus, setRestoreStatus] = useState('');
    const [restoreLoading, setRestoreLoading] = useState(false);
    const [identitySyncState, setIdentitySyncState] = useState<'idle' | 'syncing' | 'synced' | 'error'>('idle');
    const [identitySyncError, setIdentitySyncError] = useState('');
    const [founderTokenVersion, setFounderTokenVersion] = useState(0);
    const [theme, setTheme] = useState<'dark' | 'cream' | 'light'>(() => {
        return (localStorage.getItem('quanchan_theme') as 'dark' | 'cream' | 'light') || 'dark';
    });
    const founderToken = getFounderToken();

    function toggleTheme() {
        const cycle: Array<'dark' | 'cream' | 'light'> = ['dark', 'cream', 'light'];
        const idx = cycle.indexOf(theme);
        const next = cycle[(idx + 1) % cycle.length];
        setTheme(next);
        localStorage.setItem('quanchan_theme', next);
        document.documentElement.setAttribute('data-theme', next);
    }

    const themeIcon = theme === 'dark' ? <Moon size={12} /> : theme === 'cream' ? <Palette size={12} /> : <Sun size={12} />;
    const themeLabel = theme === 'dark' ? 'Dark' : theme === 'cream' ? 'Cream' : 'Light';

    useEffect(() => {
        document.documentElement.setAttribute('data-theme', theme);
    }, []);

    useEffect(() => {
        const onFounderTokenUpdate = () => {
            setFounderTokenVersion(version => version + 1);
        };
        window.addEventListener('quanchan:founder-token', onFounderTokenUpdate as EventListener);
        return () => {
            window.removeEventListener('quanchan:founder-token', onFounderTokenUpdate as EventListener);
        };
    }, []);

    useEffect(() => {
        setUsernameInput(identity?.username || '');
    }, [identity?.username]);

    useEffect(() => {
        if (fetchBoards) fetchBoards();
    }, [fetchBoards]);

    useEffect(() => {
        let cancelled = false;
        setStatsState('loading');
        setStatsError('');
        fetch('/api/stats')
            .then(async r => {
                const data = await r.json().catch(() => null);
                if (!r.ok) {
                    throw new Error(data?.error || 'Failed to load network stats.');
                }
                return data;
            })
            .then(d => {
                if (!cancelled && d) {
                    setStats(d);
                    setStatsState('ready');
                }
            })
            .catch(error => {
                if (!cancelled) {
                    setStatsState('error');
                    setStatsError(error instanceof Error ? error.message : 'Failed to load network stats.');
                }
            });
        return () => {
            cancelled = true;
        };
    }, []);
    useEffect(() => {
        if (!identity) return;
        const store = useStore.getState() as any;
        if (!store.updateProfile) return;
        let cancelled = false;
        let retryTimer: number | null = null;

        const syncIdentityProfile = async (attempt: number) => {
            if (cancelled) return;
            setIdentitySyncState('syncing');
            setIdentitySyncError('');

            try {
                const [binding, recoveryBundle] = await Promise.all([
                    buildSignedIdentityBinding(identity),
                    encryptRecoveryVault(identity, founderToken, identity.seedPhrase),
                ]);
                await store.updateProfile(
                    identity.displayHash,
                    identity.username || '',
                    identity.pqcKemPublicKey,
                    identity.publicKey,
                    identity.pqcIdentityPublicKey,
                    identity.pqcIdentityScheme,
                    binding.payload,
                    binding.signature,
                    recoveryBundle.recovery_lookup_hash,
                    recoveryBundle.recovery_bundle_ciphertext,
                    recoveryBundle.recovery_bundle_iv
                );
                if (!cancelled) {
                    setIdentitySyncState('synced');
                    setIdentitySyncError('');
                }
            } catch (e: unknown) {
                console.error('Failed to sync identity profile:', e);
                if (cancelled) return;
                if (attempt < 4) {
                    retryTimer = window.setTimeout(() => {
                        syncIdentityProfile(attempt + 1).catch(err => console.error('Identity retry failed:', err));
                    }, 1500 * (attempt + 1));
                    return;
                }
                setIdentitySyncState('error');
                setIdentitySyncError(e instanceof Error ? e.message : 'Identity sync failed.');
            }
        };

        syncIdentityProfile(0).catch(e => console.error('Identity sync failed:', e));

        return () => {
            cancelled = true;
            if (retryTimer !== null) {
                window.clearTimeout(retryTimer);
            }
        };
    }, [
        identity?.displayHash,
        identity?.username,
        identity?.pqcKemPublicKey,
        identity?.publicKey,
        identity?.pqcIdentityPublicKey,
        identity?.pqcIdentitySecretKey,
        founderToken,
    ]);

    useEffect(() => {
        if (!identity?.displayHash) {
            setSelfRole('user');
            localStorage.setItem(SELF_ROLE_STORAGE_KEY, 'user');
            window.dispatchEvent(new CustomEvent('quanchan:self-role', { detail: { role: 'user' } }));
            return;
        }

        let cancelled = false;
        fetch(`/api/profile/${encodeURIComponent(identity.displayHash)}`)
            .then(res => res.ok ? res.json() : null)
            .then(profile => {
                if (!cancelled) {
                    const normalizedRole = normalizeProfileRole(profile?.role);
                    setSelfRole(normalizedRole);
                    localStorage.setItem(SELF_ROLE_STORAGE_KEY, normalizedRole);
                    window.dispatchEvent(new CustomEvent('quanchan:self-role', { detail: { role: normalizedRole } }));
                }
            })
            .catch(() => {
                if (!cancelled) {
                    setSelfRole('user');
                    localStorage.setItem(SELF_ROLE_STORAGE_KEY, 'user');
                    window.dispatchEvent(new CustomEvent('quanchan:self-role', { detail: { role: 'user' } }));
                }
            });

        return () => {
            cancelled = true;
        };
    }, [identity?.displayHash, founderTokenVersion]);

    useEffect(() => {
        if (!identity?.displayHash || !getNotificationSummary) {
            setNotificationSummary({ notificationsUnread: 0, friendRequestsPending: 0, messageRequestsPending: 0, dmUnread: 0, total: 0 });
            return;
        }

        let cancelled = false;
        const refresh = async () => {
            try {
                const summary = await getNotificationSummary(identity.displayHash);
                if (!cancelled) {
                    setNotificationSummary(summary);
                }
            } catch (error) {
                if (!cancelled) {
                    console.error('Failed to load notification summary:', error);
                }
            }
        };

        refresh().catch(err => console.error('Notification summary failed:', err));

        const unsubscribe = subscribeToLiveEvent<{
            notificationsUnread: number;
            friendRequestsPending: number;
            messageRequestsPending: number;
            dmUnread: number;
            total: number;
        }>(`/api/live/notifications/summary/${encodeURIComponent(identity.displayHash)}`, {
            onUpdate: payload => {
                if (!cancelled) {
                    setNotificationSummary({
                        notificationsUnread: Number(payload.notificationsUnread) || 0,
                        friendRequestsPending: Number(payload.friendRequestsPending) || 0,
                        messageRequestsPending: Number(payload.messageRequestsPending) || 0,
                        dmUnread: Number(payload.dmUnread) || 0,
                        total: Number(payload.total) || 0,
                    });
                }
            },
            onError: () => {
                refresh().catch(err => console.error('Notification summary failed:', err));
            },
        });

        const fallbackInterval = !unsubscribe ? window.setInterval(() => {
            refresh().catch(err => console.error('Notification summary failed:', err));
        }, 15000) : null;

        return () => {
            cancelled = true;
            if (fallbackInterval !== null) {
                window.clearInterval(fallbackInterval);
            }
            unsubscribe?.();
        };
    }, [getNotificationSummary, identity?.displayHash]);

    async function handleEncryptionComplete(passphrase: string) {
        if (!pendingPost) {
            setQuantumModalVisible(false);
            return;
        }
        try {
            const encrypted = await encryptPost(pendingPost.content, passphrase);
            if (pendingPost.threadId) {
                await createReply(pendingPost.boardId, pendingPost.threadId, '[Encrypted Post]', pendingPost.imageUrl, pendingPost.name, pendingPost.authorHash, encrypted);
            } else {
                await createThread(pendingPost.boardId, pendingPost.subject, '[Encrypted Post]', pendingPost.imageUrl, pendingPost.name, pendingPost.authorHash, encrypted);
            }
        } catch (e) {
            console.error('Encryption failed:', e);
        }
        setPendingPost(null);
        setQuantumModalVisible(false);
    }

    function handleEncryptionCancel() {
        setPendingPost(null);
        setQuantumModalVisible(false);
    }

    function copyText(text: string, label: string) {
        navigator.clipboard.writeText(text);
        setCopied(label);
        setTimeout(() => setCopied(''), 2000);
    }

    async function handleIdentityRestore() {
        if (!restorePhrase.trim()) return;
        try {
            setRestoreLoading(true);
            setRestoreStatus('');
            const restored = await restoreIdentityFromRecoveryPhrase(restorePhrase);
            setUsernameInput(restored.username || '');
            setRestorePhrase('');
            setSeedVisible(false);
            setRestoreStatus('Identity restored on this browser.');
        } catch (error) {
            console.error('Identity restore failed:', error);
            setRestoreStatus(error instanceof Error ? error.message : 'Identity restore failed.');
        } finally {
            setRestoreLoading(false);
        }
    }

    async function saveUsername() {
        if (!identity) return;
        if (identity.username) return;

        const cleaned = usernameInput.trim().replace(/\s+/g, '_');
        if (!cleaned) {
            setUsernameStatus('Enter a display name first.');
            return;
        }

        setUsernameStatus('');
        try {
            const store = useStore.getState() as any;
            if (store.updateProfile) {
                const binding = await buildSignedIdentityBinding({ ...identity, username: cleaned });
                await store.updateProfile(
                    identity.displayHash,
                    cleaned,
                    identity.pqcKemPublicKey,
                    identity.publicKey,
                    identity.pqcIdentityPublicKey,
                    identity.pqcIdentityScheme,
                    binding.payload,
                    binding.signature
                );
            }
            setUsername(cleaned);
            setUsernameStatus('Display name saved.');
            setIdentityModalOpen(false);
        } catch (e) {
            console.error('Failed to save display name:', e);
            setUsernameStatus(e instanceof Error ? e.message : 'Failed to save display name.');
        }
    }

    const isLanding = location.pathname === '/';
    const directoryMatch = location.pathname === '/directory';
    const threadMatch = matchPath('/:board/thread/:id', location.pathname);
    const dmConversationMatch = matchPath('/dm/:hash', location.pathname);
    const boardMatch = matchPath('/:board', location.pathname);
    const boardId = threadMatch?.params.board
        || (boardMatch && boards.some(board => board.id === boardMatch.params.board) ? boardMatch.params.board : '');
    const threadId = Number(threadMatch?.params.id || 0);
    const storeThread = threads.find(thread => thread.id === threadId && thread.boardId === boardId);
    const locationState = (location.state || {}) as { threadSubject?: string; dmLabel?: string };
    const isDM = location.pathname === '/dm' || Boolean(dmConversationMatch);

    useEffect(() => {
        if (!isPhone || !dmConversationMatch?.params.hash) {
            setMobileDmLabel('');
            return;
        }

        const hash = dmConversationMatch.params.hash;
        const stateLabel = locationState.dmLabel?.trim();
        if (hash === 'admin') {
            setMobileDmLabel('Support Bot');
            return;
        }
        if (stateLabel) {
            setMobileDmLabel(stateLabel);
            return;
        }

        let cancelled = false;
        fetch(`/api/profile/${encodeURIComponent(hash)}`)
            .then(res => res.ok ? res.json() : null)
            .then(profile => {
                if (!cancelled) {
                    const username = profile?.username ? String(profile.username).trim() : '';
                    setMobileDmLabel(username || shortHash(hash));
                }
            })
            .catch(() => {
                if (!cancelled) {
                    setMobileDmLabel(shortHash(hash));
                }
            });

        return () => {
            cancelled = true;
        };
    }, [dmConversationMatch?.params.hash, isPhone, locationState.dmLabel]);

    useEffect(() => {
        setMobileUtilityOpen(false);
    }, [isPhone, location.pathname]);

    function handlePhoneDmBack() {
        const historyIdx = window.history.state?.idx;
        if (typeof historyIdx === 'number' && historyIdx > 0) {
            navigate(-1);
            return;
        }
        navigate('/directory');
    }

    const mobileHeader = !isPhone || isLanding ? null : (() => {
        if (dmConversationMatch?.params.hash) {
            return {
                title: truncateTitle(mobileDmLabel || shortHash(dmConversationMatch.params.hash)),
                leftLabel: 'Messages',
                leftTo: '/dm',
                rightLabel: '',
                rightTo: '',
            };
        }

        if (location.pathname === '/dm') {
            const historyIdx = window.history.state?.idx;
            return {
                title: 'Messages',
                leftLabel: typeof historyIdx === 'number' && historyIdx > 0 ? 'Back' : 'Directory',
                onLeftClick: handlePhoneDmBack,
                rightLabel: '',
                rightTo: '',
            };
        }

        if (threadMatch?.params.board && threadId) {
            const rawTitle = locationState.threadSubject || storeThread?.subject || `Thread No.${threadId}`;
            return {
                title: truncateTitle(rawTitle),
                leftLabel: `/${threadMatch.params.board}/`,
                leftTo: `/${threadMatch.params.board}`,
                rightLabel: 'Messages',
                rightTo: '/dm',
            };
        }

        if (boardId) {
            return {
                title: `/${boardId}/`,
                leftLabel: 'Directory',
                leftTo: '/directory',
                rightLabel: 'Messages',
                rightTo: '/dm',
            };
        }

        if (directoryMatch) {
            return {
                title: 'Directory',
                leftLabel: '',
                leftTo: '',
                rightLabel: 'Messages',
                rightTo: '/dm',
            };
        }

        return {
            title: 'QuanChan',
            leftLabel: 'Directory',
            leftTo: '/directory',
            rightLabel: 'Messages',
            rightTo: '/dm',
        };
    })();

    const mobileUtilityLinks = [
        { to: '/directory', label: 'Directory' },
        { to: '/about', label: 'About' },
        { to: '/crypto', label: 'Crypto' },
        { to: '/faq', label: 'FAQ' },
        { to: '/rules', label: 'Rules' },
        { to: '/contact', label: 'Contact' },
        ...(selfRole !== 'user' ? [{ to: '/moderation', label: 'Moderation' }] : []),
        ...(identity ? [{ to: `/u/${identity.displayHash}`, label: 'My Profile' }] : []),
    ];

    const mobileDockLinks: Array<{ to: string; label: string; icon: ReactNode; badge?: number }> = [
        { to: '/directory', label: 'Boards', icon: <Hash size={16} /> },
        { to: '/notifications', label: 'Alerts', icon: <Bell size={16} />, badge: notificationSummary.total },
        { to: '/dm', label: 'DMs', icon: <MessageCircle size={16} />, badge: notificationSummary.dmUnread + notificationSummary.messageRequestsPending },
        identity
            ? { to: `/u/${identity.displayHash}`, label: 'Me', icon: <User size={16} /> }
            : { to: '/', label: 'Home', icon: <House size={16} /> },
    ];

    if (isLanding) {
        return (
            <div style={{ background: 'var(--bg)', minHeight: '100vh' }}>
                <Outlet />
                <QuantumModal visible={quantumModalVisible} onComplete={handleEncryptionComplete} onCancel={handleEncryptionCancel} />
            </div>
        );
    }

    return (
        <div
            className="layout-grid"
            style={{
                gridTemplateColumns: isPhone ? '1fr' : (isDM ? '200px 1fr' : '200px 1fr 240px'),
                height: isDM ? '100vh' : 'auto',
            }}
        >
            {isPhone && mobileUtilityOpen && (
                <>
                    <button
                        type="button"
                        className="mobile-utility-backdrop"
                        aria-label="Close mobile menu"
                        onClick={() => setMobileUtilityOpen(false)}
                    />
                    <div className="mobile-utility-sheet">
                        <div className="mobile-utility-sheet__header">
                            <span>More</span>
                            <button type="button" className="mobile-utility-sheet__close" onClick={() => setMobileUtilityOpen(false)}>
                                <X size={16} />
                            </button>
                        </div>
                        <button
                            type="button"
                            className="mobile-utility-sheet__item"
                            onClick={() => {
                                toggleTheme();
                                setMobileUtilityOpen(false);
                            }}
                        >
                            <span className="flex items-center gap-2">{themeIcon} Theme</span>
                            <span className="text-xs font-mono" style={{ color: 'var(--text-dim)' }}>{themeLabel}</span>
                        </button>
                        {mobileUtilityLinks.map(link => (
                            <Link
                                key={link.to}
                                to={link.to}
                                className="mobile-utility-sheet__item"
                                onClick={() => setMobileUtilityOpen(false)}
                            >
                                {link.label}
                            </Link>
                        ))}
                    </div>
                </>
            )}

            {!isPhone && (
            <aside className="sidebar flex flex-col" style={{ borderRight: '1px solid var(--border)', zIndex: 50 }}>
                <div style={{ padding: '16px', borderBottom: '1px solid var(--border)' }}>
                    <Link to="/" style={{ textDecoration: 'none' }}>
                        <div className="font-black text-lg" style={{ fontFamily: 'var(--font-heading)', color: 'var(--text)', letterSpacing: '0.05em' }}>
                            QUANCHAN
                        </div>
                    </Link>
                    <div className="flex items-center justify-between">
                        <div className="text-xs" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono)', marginTop: '2px' }}>
                            v2 / cypherpunk
                        </div>
                        <button onClick={toggleTheme} className="theme-toggle flex items-center gap-1" title={`Current: ${themeLabel}. Click to switch.`}>
                            {themeIcon} <span className="text-xs">{themeLabel}</span>
                        </button>
                    </div>
                </div>

                <div style={{ padding: '8px 0', flex: 1, overflowY: 'auto' }}>
                    <div className="text-xs uppercase tracking-wider" style={{ color: 'var(--text-dim)', padding: '8px 16px 4px', fontFamily: 'var(--font-mono)' }}>
                        Boards
                    </div>
                    {boardsState === 'loading' && (
                        <div style={{ padding: '6px 16px', fontSize: '12px', color: 'var(--text-dim)', fontFamily: 'var(--font-mono)' }}>
                            Loading boards...
                        </div>
                    )}
                    {boardsState === 'error' && (
                        <div style={{ padding: '6px 16px', fontSize: '12px', color: 'var(--red)', fontFamily: 'var(--font-mono)', lineHeight: 1.5 }}>
                            {boardsError || 'Boards are unavailable right now.'}
                        </div>
                    )}
                    {boards.map(board => (
                        <Link
                            key={board.id}
                            to={`/${board.id}`}
                            className="hover:bg-black/10 transition-colors"
                            style={{
                                display: 'flex',
                                alignItems: 'center',
                                gap: '8px',
                                padding: '6px 16px',
                                fontSize: '13px',
                                color: location.pathname === `/${board.id}` ? 'var(--text)' : 'var(--text-muted)',
                                textDecoration: 'none',
                                fontFamily: 'var(--font-mono)',
                                background: location.pathname === `/${board.id}` ? 'var(--surface-2)' : 'transparent',
                                borderLeft: location.pathname === `/${board.id}` ? '2px solid var(--accent)' : '2px solid transparent',
                            }}
                        >
                            <Hash size={12} className="flex-shrink-0" />
                            <span className="flex-shrink-0">{board.id}</span>
                            <span className="truncate text-right w-full" style={{ color: 'var(--text-dim)', fontSize: '11px', marginLeft: 'auto' }}>
                                {board.name}
                            </span>
                        </Link>
                    ))}

                    <div className="text-xs uppercase tracking-wider" style={{ color: 'var(--text-dim)', padding: '16px 16px 4px', fontFamily: 'var(--font-mono)' }}>
                        Direct Messages
                    </div>
                    <Link
                        to="/dm"
                        style={{
                            display: 'flex',
                            alignItems: 'center',
                            justifyContent: 'space-between',
                            gap: '8px',
                            padding: '6px 16px',
                            fontSize: '13px',
                            color: location.pathname === '/dm' ? '#fff' : 'var(--text-muted)',
                            textDecoration: 'none',
                            fontFamily: 'var(--font-mono)',
                            background: location.pathname === '/dm' ? 'var(--surface-2)' : 'transparent',
                            borderLeft: location.pathname === '/dm' ? '2px solid var(--accent)' : '2px solid transparent',
                        }}
                    >
                        <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                            <MessageCircle size={12} /> Messages
                        </span>
                        {(notificationSummary.dmUnread + notificationSummary.messageRequestsPending) > 0 && (
                            <span className="identity-badge" style={{ padding: '2px 6px', color: 'var(--gold)' }}>
                                {notificationSummary.dmUnread + notificationSummary.messageRequestsPending}
                            </span>
                        )}
                    </Link>
                </div>

                <div style={{ borderTop: '1px solid var(--border)', padding: '8px' }}>
                    <div className="text-xs uppercase tracking-wider" style={{ color: 'var(--text-dim)', padding: '4px 8px 6px', fontFamily: 'var(--font-mono)' }}>
                        Utility
                    </div>
                    {[
                        { to: '/directory', label: '/directory/' },
                        { to: '/about', label: '/about/' },
                        { to: '/crypto', label: '/crypto/' },
                        { to: '/faq', label: '/faq/' },
                        { to: '/rules', label: '/rules/' },
                        { to: '/contact', label: '/contact/' },
                    ].map(link => {
                        const isActive = location.pathname === link.to || location.pathname.startsWith(`${link.to}/`);
                        return (
                            <Link
                                key={link.to}
                                to={link.to}
                                style={{
                                    display: 'block',
                                    padding: '4px 8px',
                                    fontSize: '12px',
                                    color: isActive ? 'var(--text)' : 'var(--text-muted)',
                                    background: isActive ? 'var(--surface-2)' : 'transparent',
                                    borderRadius: '4px',
                                    textDecoration: 'none',
                                    fontFamily: 'var(--font-mono)',
                                }}
                            >
                                {link.label}
                            </Link>
                        );
                    })}
                </div>
            </aside>
            )}

            <main className={`layout-main ${isDM ? 'layout-main--dm' : ''} ${isPhone && !isDM ? 'layout-main--with-dock' : ''}`} style={{ minHeight: '100vh', height: isDM ? '100vh' : 'auto', overflow: isDM ? 'hidden' : 'auto', display: 'flex', flexDirection: 'column' }}>
                {mobileHeader && (
                    <div className="mobile-topbar">
                        {mobileHeader.leftLabel ? (
                            mobileHeader.onLeftClick ? (
                                <button type="button" className="mobile-topbar__action" onClick={mobileHeader.onLeftClick}>
                                    <ArrowLeft size={14} />
                                    <span>{mobileHeader.leftLabel}</span>
                                </button>
                            ) : (
                                <Link to={mobileHeader.leftTo} className="mobile-topbar__action">
                                    <ArrowLeft size={14} />
                                    <span>{mobileHeader.leftLabel}</span>
                                </Link>
                            )
                        ) : (
                            <div className="mobile-topbar__spacer" />
                        )}
                        <div className="mobile-topbar__title">
                            {mobileHeader.title}
                        </div>
                        <div className="mobile-topbar__actions">
                            {mobileHeader.rightLabel ? (
                                <Link to={mobileHeader.rightTo} className="mobile-topbar__action mobile-topbar__action--right">
                                    <MessageCircle size={14} />
                                    <span>{mobileHeader.rightLabel}</span>
                                </Link>
                            ) : null}
                            <button
                                type="button"
                                className="mobile-topbar__icon"
                                aria-label="Open more options"
                                onClick={() => setMobileUtilityOpen(true)}
                            >
                                <Menu size={16} />
                            </button>
                        </div>
                    </div>
                )}
                <Outlet />
                {isPhone && !isDM && (
                    <nav className="mobile-bottom-dock" aria-label="Primary">
                        {mobileDockLinks.map(link => {
                            const isActive = location.pathname === link.to || (link.to !== '/' && location.pathname.startsWith(`${link.to}/`));
                            return (
                                <Link
                                    key={link.to}
                                    to={link.to}
                                    className={`mobile-bottom-dock__item${isActive ? ' mobile-bottom-dock__item--active' : ''}`}
                                >
                                    <span className="mobile-bottom-dock__icon">
                                        {link.icon}
                                        {link.badge ? (
                                            <span className="mobile-bottom-dock__badge">
                                                {link.badge > 99 ? '99+' : link.badge}
                                            </span>
                                        ) : null}
                                    </span>
                                    <span>{link.label}</span>
                                </Link>
                            );
                        })}
                    </nav>
                )}
            </main>

            {!isDM && !isPhone && (
                <aside className="sidebar sidebar-right flex flex-col" style={{ borderLeft: '1px solid var(--border)' }}>
                    <div style={{ padding: '16px', borderBottom: '1px solid var(--border)' }}>
                        <div className="text-xs uppercase tracking-wider" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono)', marginBottom: '12px' }}>
                            Network Stats
                        </div>
                        {statsState === 'error' ? (
                            <div className="text-xs" style={{ color: 'var(--red)', lineHeight: 1.5 }}>
                                {statsError || 'Network stats are unavailable right now.'}
                            </div>
                        ) : (
                            <div className="space-y-2">
                                {[
                                    { label: 'Total Posts', value: stats.posts },
                                    { label: 'Active Threads', value: stats.threads },
                                    { label: 'Boards', value: stats.boards },
                                    { label: 'Named Users', value: stats.namedUsers || stats.users },
                                ].map(stat => (
                                    <div key={stat.label} className="stat-pill flex justify-between items-center">
                                        <span className="text-xs" style={{ color: 'var(--text-muted)' }}>{stat.label}</span>
                                        <span className="text-sm font-bold font-mono" style={{ color: 'var(--text)' }}>
                                            {statsState === 'loading' ? '...' : stat.value.toLocaleString()}
                                        </span>
                                    </div>
                                ))}
                            </div>
                        )}
                    </div>

                    <div style={{ padding: '16px' }}>
                        <div className="text-xs uppercase tracking-wider flex items-center gap-2" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono)', marginBottom: '12px' }}>
                            <Shield size={12} /> Identity Vault
                        </div>
                        {identity ? (
                            <div className="space-y-2">
                                <button
                                    onClick={() => setIdentityModalOpen(true)}
                                    className="btn-v2 w-full text-sm flex items-center justify-center gap-2"
                                    style={{ padding: '10px', fontFamily: 'var(--font-mono)' }}
                                >
                                    <Shield size={14} /> Manage Identity
                                </button>
                                <Link to={`/u/${identity.displayHash}`} className="btn-v2 hover:bg-black/10 transition-colors w-full text-sm flex items-center justify-start gap-2" style={{ padding: '8px 12px', fontFamily: 'var(--font-mono)' }}>
                                    <User size={14} /> My Profile
                                </Link>
                                <Link to="/notifications" className="btn-v2 hover:bg-black/10 transition-colors w-full text-sm flex items-center justify-start gap-2" style={{ padding: '8px 12px', fontFamily: 'var(--font-mono)' }}>
                                    <Bell size={14} /> Notifications
                                    {notificationSummary.total > 0 && (
                                        <span className="identity-badge" style={{ marginLeft: 'auto', padding: '2px 6px', color: 'var(--gold)' }}>
                                            {notificationSummary.total}
                                        </span>
                                    )}
                                </Link>
                                <Link to="/dm?to=admin" className="btn-v2 hover:bg-black/10 transition-colors w-full text-sm flex items-center justify-start gap-2" style={{ padding: '8px 12px', fontFamily: 'var(--font-mono)' }}>
                                    <MessageCircle size={14} /> Support Bot
                                </Link>
                                {selfRole !== 'user' && (
                                    <Link to="/moderation" className="btn-v2 hover:bg-black/10 transition-colors w-full text-sm flex items-center justify-start gap-2" style={{ padding: '8px 12px', fontFamily: 'var(--font-mono)' }}>
                                        <Shield size={14} /> Moderation Queue
                                    </Link>
                                )}
                            </div>
                        ) : (
                            <div className="text-xs" style={{ color: 'var(--text-dim)', fontStyle: 'italic' }}>
                                Generating keypair...
                            </div>
                        )}
                    </div>
                </aside>
            )}

            {identityModalOpen && identity && (
                <div className="fixed inset-0 z-50 flex items-start justify-center overflow-y-auto p-4" style={{ background: 'rgba(0,0,0,0.85)' }} onClick={() => { setIdentityModalOpen(false); setSeedVisible(false); }}>
                    <div className="flat-card" style={{ width: '100%', maxWidth: '440px', maxHeight: 'calc(100dvh - 32px)', overflow: 'hidden', margin: 'auto 0' }} onClick={e => e.stopPropagation()}>
                        <div className="p-4 flex items-center justify-between" style={{ borderBottom: '1px solid var(--border)' }}>
                            <div>
                                <h3 className="font-semibold flex items-center gap-2 text-sm" style={{ color: 'var(--text)', fontFamily: 'var(--font-mono)' }}>
                                    <Shield size={16} style={{ color: 'var(--cyan)' }} /> Manage Identity
                                </h3>
                                <p className="text-xs mt-1" style={{ color: 'var(--text-dim)' }}>
                                    Local identity, recovery phrase, and role status
                                </p>
                            </div>
                            <button onClick={() => { setIdentityModalOpen(false); setSeedVisible(false); }} style={{ background: 'none', border: 'none', color: 'var(--text-dim)', cursor: 'pointer' }}>
                                <X size={16} />
                            </button>
                        </div>

                        <div className="p-4 space-y-4" style={{ overflowY: 'auto', maxHeight: 'calc(100dvh - 116px)' }}>
                            <div style={{ background: 'rgba(255,255,255,0.025)', border: '1px solid var(--border)', borderRadius: '8px', padding: '12px' }}>
                                <div className="text-xs uppercase tracking-wider mb-2" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono)' }}>
                                    Display Name
                                </div>
                                <div className="flex items-center gap-2">
                                    <User size={14} style={{ color: 'var(--text-dim)', flexShrink: 0 }} />
                                    <input
                                        className="v2-input flex-1 text-sm disabled:opacity-50 disabled:cursor-not-allowed"
                                        style={{ padding: '8px 12px' }}
                                        placeholder="Choose a public name..."
                                        value={usernameInput}
                                        onChange={e => {
                                            setUsernameInput(e.target.value.replace(/\s+/g, '_'));
                                            if (usernameStatus) setUsernameStatus('');
                                        }}
                                        disabled={!!identity?.username}
                                    />
                                    {!identity?.username && (
                                        <button
                                            type="button"
                                            className="btn-v2-accent text-xs"
                                            style={{ padding: '8px 12px', whiteSpace: 'nowrap' }}
                                            onClick={saveUsername}
                                        >
                                            Save
                                        </button>
                                    )}
                                </div>
                                <div className="flex items-center justify-between mt-3">
                                    <p className="text-xs" style={{ color: 'var(--text-dim)', lineHeight: 1.45, maxWidth: '220px' }}>
                                        Founder, moderator, and duplicate names are blocked.
                                    </p>
                                    <Link
                                        to={`/u/${identity.displayHash}`}
                                        className="btn-v2 text-xs flex items-center gap-1"
                                        style={{ padding: '4px 8px' }}
                                        onClick={() => setIdentityModalOpen(false)}
                                    >
                                        View Profile
                                    </Link>
                                </div>
                                {usernameStatus && (
                                    <p className="text-xs mt-2" style={{ color: usernameStatus.includes('saved') ? 'var(--green)' : 'var(--red)', lineHeight: 1.45 }}>
                                        {usernameStatus}
                                    </p>
                                )}
                            </div>

                            <div style={{ background: 'rgba(255,255,255,0.025)', border: '1px solid var(--border)', borderRadius: '8px', padding: '12px' }}>
                                <div className="text-xs uppercase tracking-wider mb-2" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono)' }}>
                                    Identity Hash
                                </div>
                                <div className="flex items-center gap-2">
                                    <div className="identity-badge flex-1" style={{ padding: '8px 12px', wordBreak: 'break-all' }}>
                                        {identity.displayHash}
                                    </div>
                                    <button onClick={() => copyText(identity.displayHash, 'hash')} className="btn-v2 p-2" title="Copy hash">
                                        <Copy size={14} />
                                    </button>
                                </div>
                                {copied === 'hash' && (
                                    <span className="text-xs font-mono" style={{ color: 'var(--green)' }}>Copied!</span>
                                )}
                                <p className="text-xs mt-2" style={{ color: 'var(--text-dim)', lineHeight: 1.5 }}>
                                    Derived from your recovery phrase. The same phrase recreates this identity.
                                </p>
                                <p className="text-xs mt-2" style={{ color: identitySyncState === 'error' ? 'var(--red)' : identitySyncState === 'synced' ? 'var(--green)' : 'var(--text-dim)', lineHeight: 1.5 }}>
                                    {identitySyncState === 'syncing' && 'Publishing PQC messaging keys for this browser...'}
                                    {identitySyncState === 'synced' && 'Identity profile is published.'}
                                    {identitySyncState === 'error' && (identitySyncError || 'Identity profile could not be published right now.')}
                                    {identitySyncState === 'idle' && 'Preparing identity data...'}
                                </p>
                            </div>

                            <div style={{ background: 'rgba(255,255,255,0.025)', border: '1px solid var(--border)', borderRadius: '8px', padding: '12px' }}>
                                <div className="text-xs uppercase tracking-wider mb-2 flex items-center gap-2" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono)' }}>
                                    <Crown size={12} /> Role
                                </div>
                                <div className="flex items-center gap-2 flex-wrap">
                                    <span className="identity-badge" style={{ padding: '6px 10px', color: selfRole === 'founder' ? 'var(--gold)' : selfRole === 'moderator' ? 'var(--cyan)' : 'var(--text-dim)' }}>
                                        {selfRole.toUpperCase()}
                                    </span>
                                    {selfRole === 'founder' && founderToken && (
                                        <span className="text-xs font-mono" style={{ color: 'var(--green)' }}>
                                            Founder session active
                                        </span>
                                    )}
                                </div>
                                <p className="text-xs mt-2" style={{ color: 'var(--text-dim)', lineHeight: 1.45 }}>
                                    Roles are tied to this identity hash. Restore the same phrase to recover the same founder session here.
                                </p>
                                {selfRole === 'founder' && !founderToken && (
                                    <p className="text-xs mt-2" style={{ color: 'var(--gold)', lineHeight: 1.45 }}>
                                        Founder role exists, but this browser is missing the founder session token. Restore the phrase here to recover moderator controls.
                                    </p>
                                )}
                                {selfRole !== 'user' && (
                                    <p className="text-xs mt-2" style={{ color: 'var(--text-dim)', lineHeight: 1.45 }}>
                                        Moderation controls are available from post menus and the moderation queue.
                                    </p>
                                )}
                            </div>

                            <div style={{ background: 'rgba(255,255,255,0.025)', border: '1px solid var(--border)', borderRadius: '8px', padding: '12px' }}>
                                <div className="text-xs uppercase tracking-wider mb-2" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono)' }}>
                                    Restore Identity
                                </div>
                                <textarea
                                    className="v2-input w-full text-sm"
                                    style={{ padding: '10px 12px', minHeight: '78px', resize: 'vertical' }}
                                    placeholder="Enter your 12-word recovery phrase to replace the current identity in this browser..."
                                    value={restorePhrase}
                                    onChange={e => {
                                        setRestorePhrase(e.target.value);
                                        if (restoreStatus) setRestoreStatus('');
                                    }}
                                />
                                <button
                                    type="button"
                                    className="btn-v2-accent text-xs mt-3 w-full"
                                    style={{ padding: '10px 12px' }}
                                    onClick={handleIdentityRestore}
                                    disabled={restoreLoading || !restorePhrase.trim()}
                                >
                                    {restoreLoading ? 'Restoring...' : 'Restore This Browser From Recovery Phrase'}
                                </button>
                                <p className="text-xs mt-2" style={{ color: restoreStatus ? (restoreStatus.includes('restored') ? 'var(--green)' : 'var(--red)') : 'var(--text-dim)', lineHeight: 1.45 }}>
                                    {restoreStatus || 'This browser will switch to the identity derived from the phrase you enter.'}
                                </p>
                            </div>

                            <div style={{ background: 'rgba(255,255,255,0.025)', border: '1px solid var(--border)', borderRadius: '8px', padding: '12px' }}>
                                <div className="text-xs uppercase tracking-wider mb-2 flex items-center justify-between" style={{ color: 'var(--text-dim)', fontFamily: 'var(--font-mono)' }}>
                                    Recovery Phrase
                                    <button onClick={() => setSeedVisible(!seedVisible)} style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--text-dim)', padding: '2px' }}>
                                        {seedVisible ? <EyeOff size={12} /> : <Eye size={12} />}
                                    </button>
                                </div>
                                <div
                                    onClick={() => !seedVisible && setSeedVisible(true)}
                                    className="stat-pill font-mono text-xs"
                                    style={{
                                        padding: '12px',
                                        color: seedVisible ? 'var(--gold)' : 'var(--text-dim)',
                                        wordBreak: 'break-all',
                                        lineHeight: 1.6,
                                        filter: seedVisible ? 'none' : 'blur(5px)',
                                        userSelect: seedVisible ? 'text' : 'none',
                                        cursor: seedVisible ? 'text' : 'pointer',
                                    }}
                                >
                                    {identity.seedPhrase}
                                </div>
                                {!seedVisible && (
                                    <p className="text-xs mt-1" style={{ color: 'var(--text-dim)', textAlign: 'center' }}>Click to reveal</p>
                                )}
                                {seedVisible && (
                                    <>
                                        <button
                                            className="btn-v2 text-xs mt-2 flex items-center gap-1 w-full"
                                            style={{ padding: '6px', justifyContent: 'center' }}
                                            onClick={() => copyText(identity.seedPhrase, 'seed')}
                                        >
                                            <Copy size={10} /> {copied === 'seed' ? 'Copied!' : 'Copy Recovery Phrase'}
                                        </button>
                                        <p className="text-xs mt-2" style={{ color: 'var(--text-dim)', lineHeight: 1.45 }}>
                                            This phrase recreates the same account hash, messaging keys, and founder authority.
                                        </p>
                                    </>
                                )}
                            </div>

                            <div style={{ background: 'rgba(239,68,68,0.08)', border: '1px solid rgba(239,68,68,0.2)', borderRadius: '2px', padding: '10px 12px' }}>
                                <p className="text-xs" style={{ color: 'var(--red)', lineHeight: 1.5 }}>
                                    Never share your recovery phrase. Anyone with these 12 words can recreate your identity.
                                </p>
                            </div>
                        </div>
                    </div>
                </div>
            )}

            <QuantumModal visible={quantumModalVisible} onComplete={handleEncryptionComplete} onCancel={handleEncryptionCancel} />
        </div>
    );
}
