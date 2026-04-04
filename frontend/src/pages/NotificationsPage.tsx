import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { useStore } from '../store/useStore';
import { useIdentity } from '../hooks/useIdentity';
import { useIsPhone } from '../hooks/useIsPhone';
import { subscribeToLiveEvent } from '../utils/liveEvents';
import { Bell, CheckCheck, ArrowRight } from 'lucide-react';

function formatTS(ts: number) {
    return new Date(ts).toLocaleString();
}

export default function NotificationsPage() {
    const { identity } = useIdentity();
    const { getNotifications, markNotificationsRead } = useStore();
    const isPhone = useIsPhone();
    const [items, setItems] = useState<any[]>([]);
    const [loading, setLoading] = useState(true);

    async function loadNotifications() {
        if (!identity?.displayHash) return;
        setLoading(true);
        try {
            const data = await getNotifications(identity.displayHash, 100);
            setItems(Array.isArray(data.notifications) ? data.notifications : []);
        } finally {
            setLoading(false);
        }
    }

    useEffect(() => {
        loadNotifications().catch(err => console.error('Failed to load notifications:', err));
    }, [identity?.displayHash]);

    useEffect(() => {
        if (!identity?.displayHash) return;

        const unsubscribe = subscribeToLiveEvent<{ notifications: any[] }>(
            `/api/live/notifications/feed/${encodeURIComponent(identity.displayHash)}?limit=100`,
            {
                onUpdate: payload => {
                    setItems(Array.isArray(payload.notifications) ? payload.notifications : []);
                    setLoading(false);
                },
                onError: () => {
                    loadNotifications().catch(err => console.error('Failed to load notifications:', err));
                },
            }
        );

        return () => {
            unsubscribe?.();
        };
    }, [identity?.displayHash]);

    async function markAllRead() {
        if (!identity?.displayHash) return;
        await markNotificationsRead(identity.displayHash);
        await loadNotifications();
    }

    return (
        <div style={{ padding: isPhone ? '14px' : '20px', maxWidth: '860px', margin: '0 auto 120px' }}>
            <div className="flat-card" style={{ padding: '20px' }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: '12px', marginBottom: '16px', flexWrap: 'wrap' }}>
                    <div>
                        <h1 style={{ color: 'var(--text)', fontFamily: 'var(--font-heading)', fontSize: 'clamp(1.4rem, 4vw, 2rem)' }}>
                            Notifications
                        </h1>
                        <p style={{ color: 'var(--text-dim)', fontSize: '0.88rem', marginTop: '6px' }}>
                            Friend requests, message requests, role updates, and moderation signals live here.
                        </p>
                    </div>
                    <button onClick={markAllRead} className="btn-v2 text-sm" style={{ padding: isPhone ? '9px 12px' : '10px 14px', width: isPhone ? '100%' : 'auto' }} disabled={!identity?.displayHash}>
                        <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                            <CheckCheck size={14} /> Mark All Read
                        </span>
                    </button>
                </div>

                {loading ? (
                    <div style={{ padding: '32px 12px', color: 'var(--text-dim)', textAlign: 'center' }}>
                        Loading notifications...
                    </div>
                ) : items.length === 0 ? (
                    <div style={{ padding: '32px 12px', color: 'var(--text-dim)', textAlign: 'center' }}>
                        Nothing new right now.
                    </div>
                ) : (
                    <div className="space-y-3">
                        {items.map(item => (
                            <div key={item.id} className="flat-card" style={{ padding: '14px 16px', background: item.read ? 'rgba(255,255,255,0.02)' : 'rgba(251,191,36,0.06)' }}>
                                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', gap: '12px', flexWrap: 'wrap' }}>
                                    <div style={{ minWidth: 0, flex: 1 }}>
                                        <div style={{ display: 'flex', alignItems: 'center', gap: '10px', flexWrap: 'wrap' }}>
                                            <span className="identity-badge" style={{ padding: '5px 8px', color: item.read ? 'var(--text-dim)' : 'var(--gold)' }}>
                                                <Bell size={11} style={{ marginRight: '6px', verticalAlign: 'middle' }} />
                                                {String(item.type || 'notice').replace(/_/g, ' ')}
                                            </span>
                                            {!item.read && (
                                                <span className="text-xs font-mono" style={{ color: 'var(--gold)' }}>
                                                    unread
                                                </span>
                                            )}
                                        </div>
                                        <h3 style={{ color: 'var(--text)', marginTop: '10px', fontSize: '1rem' }}>{item.title}</h3>
                                        {item.body && (
                                            <p style={{ color: 'var(--text-dim)', marginTop: '6px', lineHeight: 1.55, fontSize: '0.88rem' }}>
                                                {item.body}
                                            </p>
                                        )}
                                        <p style={{ color: 'var(--text-dim)', marginTop: '8px', fontSize: '0.76rem', fontFamily: 'var(--font-mono)' }}>
                                            {formatTS(Number(item.timestamp) || Date.now())}
                                        </p>
                                    </div>
                                    {item.link && (
                                        <Link to={item.link} className="btn-v2 text-xs" style={{ padding: '8px 10px', textDecoration: 'none' }}>
                                            <span style={{ display: 'inline-flex', alignItems: 'center', gap: '6px' }}>
                                                Open <ArrowRight size={12} />
                                            </span>
                                        </Link>
                                    )}
                                </div>
                            </div>
                        ))}
                    </div>
                )}
            </div>
        </div>
    );
}
