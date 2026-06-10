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
import { useEffect, useMemo, useState } from 'react';
import { Link, Navigate } from 'react-router-dom';
import { Shield, Clock, ExternalLink, CheckCircle2, Ban, RotateCcw, Trash2, Plus } from 'lucide-react';
import { useStore } from '../store/useStore';
import { useIdentity } from '../hooks/useIdentity';
import { getFounderToken, normalizeProfileRole } from '../utils/roleAuth';

type ReportStatus = 'open' | 'resolved' | 'dismissed';

export default function ModerationPage() {
    const identity = useIdentity();
    const getModerationReports = useStore(s => (s as any).getModerationReports as (actorHash: string, founderToken?: string, limit?: number) => Promise<{ reports: any[] }>);
    const getModerationAudit = useStore(s => (s as any).getModerationAudit as (actorHash: string, founderToken?: string, limit?: number) => Promise<{ events: any[] }>);
    const resolveModerationReport = useStore(s => (s as any).resolveModerationReport as (reportId: number, actorHash: string, status: ReportStatus, note?: string, founderToken?: string) => Promise<any>);
    
    const getBans = useStore(s => (s as any).getBans as (actorHash: string) => Promise<any[]>);
    const banUser = useStore(s => (s as any).banUser as (actorHash: string, target: string, banType: 'identity' | 'ip', reason: string, durationSeconds: number) => Promise<any>);
    const unbanUser = useStore(s => (s as any).unbanUser as (actorHash: string, banId: string) => Promise<any>);
    const extendBan = useStore(s => (s as any).extendBan as (actorHash: string, banId: string, durationSeconds: number) => Promise<any>);

    const [reports, setReports] = useState<any[]>([]);
    const [showAllReports, setShowAllReports] = useState(false);
    const [auditEvents, setAuditEvents] = useState<any[]>([]);
    const [bans, setBans] = useState<any[]>([]);
    const [activeTab, setActiveTab] = useState<'reports' | 'bans' | 'audit'>('reports');

    const filteredReports = useMemo(() => {
        if (showAllReports) return reports;
        return reports.filter(r => r.status === 'open');
    }, [reports, showAllReports]);

    // New ban form state
    const [banTarget, setBanTarget] = useState('');
    const [banType, setBanType] = useState<'identity' | 'ip'>('identity');
    const [banReason, setBanReason] = useState('');
    const [banDuration, setBanDuration] = useState('86400');
    const [banSubmitting, setBanSubmitting] = useState(false);

    const [loading, setLoading] = useState(true);
    const [error, setError] = useState('');
    const [busyId, setBusyId] = useState<number | null>(null);
    const [viewerRole, setViewerRole] = useState<'user' | 'moderator' | 'founder'>(() => normalizeProfileRole(localStorage.getItem('quanchan_self_role') || undefined));
    const [roleState, setRoleState] = useState<'loading' | 'ready'>('loading');
    const [founderTokenVersion, setFounderTokenVersion] = useState(0);
    const founderToken = getFounderToken();

    async function loadReports() {
        const actorHash = identity.identity?.displayHash;
        if (!actorHash) {
            setReports([]);
            setBans([]);
            setLoading(false);
            return;
        }
        if (viewerRole === 'user') {
            setReports([]);
            setBans([]);
            setAuditEvents([]);
            setLoading(false);
            return;
        }
        try {
            setLoading(true);
            setError('');
            let reportData: any, auditData: any, banData: any;
            try {
                [reportData, auditData, banData] = await Promise.all([
                    getModerationReports(actorHash, founderToken, 100),
                    getModerationAudit(actorHash, founderToken, 40),
                    getBans(actorHash)
                ]);
            } catch (err) {
                if (founderToken) {
                    await fetch('/api/admin/login', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({
                            actor_hash: actorHash,
                            founder_token: founderToken,
                        }),
                    });
                    [reportData, auditData, banData] = await Promise.all([
                        getModerationReports(actorHash, founderToken, 100),
                        getModerationAudit(actorHash, founderToken, 40),
                        getBans(actorHash)
                    ]);
                } else {
                    throw err;
                }
            }
            setReports(Array.isArray(reportData?.reports) ? reportData.reports : []);
            setAuditEvents(Array.isArray(auditData?.events) ? auditData.events : []);
            setBans(Array.isArray(banData) ? banData : (banData?.bans && Array.isArray(banData.bans) ? banData.bans : []));
        } catch (err) {
            setError(err instanceof Error ? err.message : 'Could not load moderation queue.');
        } finally {
            setLoading(false);
        }
    }

    useEffect(() => {
        loadReports().catch(err => console.error('Moderation queue failed:', err));
    }, [identity.identity?.displayHash, viewerRole, founderTokenVersion]);

    useEffect(() => {
        if (identity.loading) {
            return;
        }

        if (!identity.identity?.displayHash) {
            setViewerRole('user');
            setRoleState('ready');
            return;
        }

        let cancelled = false;
        setRoleState('loading');

        fetch(`/api/profile/${encodeURIComponent(identity.identity.displayHash)}`)
            .then(res => res.ok ? res.json() : null)
            .then(profile => {
                if (cancelled) return;
                const normalizedRole = normalizeProfileRole(profile?.role);
                setViewerRole(normalizedRole);
                localStorage.setItem('quanchan_self_role', normalizedRole);
                window.dispatchEvent(new CustomEvent('quanchan:self-role', { detail: { role: normalizedRole } }));
                setRoleState('ready');
            })
            .catch(() => {
                if (cancelled) return;
                setViewerRole('user');
                localStorage.setItem('quanchan_self_role', 'user');
                window.dispatchEvent(new CustomEvent('quanchan:self-role', { detail: { role: 'user' } }));
                setRoleState('ready');
            });

        return () => {
            cancelled = true;
        };
    }, [identity.identity?.displayHash, identity.loading, founderTokenVersion]);

    useEffect(() => {
        const onRoleUpdate = (event: Event) => {
            const role = (event as CustomEvent<{ role?: string }>).detail?.role;
            setViewerRole(normalizeProfileRole(role));
            setRoleState('ready');
        };
        const onFounderTokenUpdate = () => {
            setFounderTokenVersion(version => version + 1);
        };

        window.addEventListener('quanchan:self-role', onRoleUpdate as EventListener);
        window.addEventListener('quanchan:founder-token', onFounderTokenUpdate as EventListener);
        return () => {
            window.removeEventListener('quanchan:self-role', onRoleUpdate as EventListener);
            window.removeEventListener('quanchan:founder-token', onFounderTokenUpdate as EventListener);
        };
    }, []);

    const openCount = useMemo(
        () => reports.filter(report => report.status === 'open').length,
        [reports]
    );

    async function updateStatus(reportId: number, status: ReportStatus) {
        if (!identity.identity?.displayHash) return;
        try {
            setBusyId(reportId);
            setError('');
            try {
                await resolveModerationReport(reportId, identity.identity.displayHash, status, '', founderToken);
            } catch (err) {
                if (founderToken) {
                    await fetch('/api/admin/login', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({
                            actor_hash: identity.identity.displayHash,
                            founder_token: founderToken,
                        }),
                    });
                    await resolveModerationReport(reportId, identity.identity.displayHash, status, '', founderToken);
                } else {
                    throw err;
                }
            }
            await loadReports();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'Could not update report.');
        } finally {
            setBusyId(null);
        }
    }

    const handleCreateBan = async (e: React.FormEvent) => {
        e.preventDefault();
        const actorHash = identity.identity?.displayHash;
        if (!actorHash || !banTarget) return;
        try {
            setBanSubmitting(true);
            setError('');
            await banUser(
                actorHash,
                banTarget,
                banType,
                banReason,
                Number(banDuration)
            );
            setBanTarget('');
            setBanReason('');
            setBanDuration('86400');
            await loadReports();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'Failed to create ban');
        } finally {
            setBanSubmitting(false);
        }
    };

    const handleUnban = async (banId: string) => {
        const actorHash = identity.identity?.displayHash;
        if (!actorHash) return;
        try {
            setError('');
            await unbanUser(actorHash, banId);
            await loadReports();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'Failed to lift ban');
        }
    };

    const handleExtendBan = async (banId: string, durationSeconds: number) => {
        const actorHash = identity.identity?.displayHash;
        if (!actorHash) return;
        try {
            setError('');
            await extendBan(actorHash, banId, durationSeconds);
            await loadReports();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'Failed to extend ban');
        }
    };

    if (identity.loading || roleState === 'loading') {
        return (
            <div style={{ padding: '20px', maxWidth: '980px', margin: '0 auto 120px' }}>
                <div className="flat-card" style={{ minHeight: '220px' }} />
            </div>
        );
    }

    if (!identity.identity?.displayHash) {
        return <Navigate to="/directory" replace />;
    }

    if (viewerRole === 'user') {
        return <Navigate to="/directory" replace />;
    }

    return (
        <div style={{ padding: '20px', maxWidth: '980px', margin: '0 auto 120px' }}>
            <div className="flat-card" style={{ padding: '20px', marginBottom: '16px' }}>
                <div style={{ display: 'flex', alignItems: 'flex-start', justifyContent: 'space-between', gap: '16px', flexWrap: 'wrap' }}>
                    <div>
                        <h1 style={{ color: 'var(--text)', fontFamily: 'var(--font-heading)', fontSize: 'clamp(1.6rem, 4vw, 2.3rem)' }}>
                            Moderation Control Panel
                        </h1>
                        <p style={{ color: 'var(--text-dim)', marginTop: '8px', lineHeight: 1.6 }}>
                            Manage reports, apply network-wide blocks, and audit recent moderator activity.
                        </p>
                    </div>
                    <div className="stat-pill" style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                        <Shield size={14} />
                        <span style={{ color: 'var(--text)' }}>{openCount} open reports</span>
                    </div>
                </div>
                {error && (
                    <p style={{ color: 'var(--red)', marginTop: '12px', lineHeight: 1.5 }}>
                        {error}
                    </p>
                )}
            </div>

            <div style={{ display: 'flex', gap: '8px', marginBottom: '20px', flexWrap: 'wrap' }}>
                <button
                    onClick={() => setActiveTab('reports')}
                    className={activeTab === 'reports' ? 'btn-v2-accent' : 'btn-v2'}
                    style={{ padding: '8px 16px', textDecoration: 'none' }}
                >
                    Reports ({reports.filter(r => r.status === 'open').length} open)
                </button>
                <button
                    onClick={() => setActiveTab('bans')}
                    className={activeTab === 'bans' ? 'btn-v2-accent' : 'btn-v2'}
                    style={{ padding: '8px 16px', textDecoration: 'none' }}
                >
                    Active Bans ({bans.length})
                </button>
                <button
                    onClick={() => setActiveTab('audit')}
                    className={activeTab === 'audit' ? 'btn-v2-accent' : 'btn-v2'}
                    style={{ padding: '8px 16px', textDecoration: 'none' }}
                >
                    Audit Trail ({auditEvents.length})
                </button>
            </div>

            {activeTab === 'reports' && (
                <div>
                    <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '14px' }}>
                        <input
                            type="checkbox"
                            id="showAllReports"
                            checked={showAllReports}
                            onChange={(e) => setShowAllReports(e.target.checked)}
                            style={{ width: '16px', height: '16px', cursor: 'pointer' }}
                        />
                        <label htmlFor="showAllReports" style={{ color: 'var(--text-dim)', fontSize: '0.85rem', cursor: 'pointer', userSelect: 'none' }}>
                            Show resolved/dismissed reports
                        </label>
                    </div>

                    {loading ? (
                        <div className="flat-card" style={{ minHeight: '220px' }} />
                    ) : filteredReports.length === 0 ? (
                        <div className="flat-card" style={{ padding: '24px', color: 'var(--text-dim)', textAlign: 'center' }}>
                            No reports in the queue.
                        </div>
                    ) : (
                        <div className="space-y-4">
                            {filteredReports.map(report => {
                                const isOpen = report.status === 'open';
                            const targetHref = report.contextLink || (report.targetHash ? `/u/${report.targetHash}` : '');
                            return (
                                <div key={report.id} className="flat-card" style={{ padding: '18px' }}>
                                    <div style={{ display: 'flex', justifyContent: 'space-between', gap: '12px', flexWrap: 'wrap', alignItems: 'flex-start' }}>
                                        <div style={{ minWidth: 0, flex: 1 }}>
                                            <div style={{ display: 'flex', alignItems: 'center', gap: '10px', flexWrap: 'wrap' }}>
                                                <span className="identity-badge" style={{ padding: '6px 10px', color: isOpen ? 'var(--gold)' : 'var(--text-dim)' }}>
                                                    {report.targetKind === 'post' ? 'POST REPORT' : 'USER REPORT'}
                                                </span>
                                                <span className="identity-badge" style={{ padding: '6px 10px', color: report.status === 'resolved' ? 'var(--green)' : report.status === 'dismissed' ? 'var(--red)' : 'var(--gold)' }}>
                                                    {String(report.status || 'open').toUpperCase()}
                                                </span>
                                            </div>
                                            <p style={{ color: 'var(--text)', marginTop: '12px', lineHeight: 1.6 }}>
                                                {report.reason}
                                            </p>
                                            <div style={{ display: 'flex', gap: '10px', flexWrap: 'wrap', marginTop: '12px' }}>
                                                <span className="stat-pill text-xs">
                                                    Reporter: {report.reporterLabel || report.reporterHash}
                                                </span>
                                                <span className="stat-pill text-xs">
                                                    Target: {report.targetLabel || report.targetDisplayName || report.targetHash || `Post #${report.targetPostId}`}
                                                </span>
                                                <span className="stat-pill text-xs" style={{ display: 'inline-flex', alignItems: 'center', gap: '6px' }}>
                                                    <Clock size={12} /> {new Date(Number(report.createdAt || Date.now())).toLocaleString()}
                                                </span>
                                            </div>
                                            {!isOpen && report.resolvedByHash && (
                                                <p style={{ color: 'var(--text-dim)', marginTop: '10px', lineHeight: 1.5, fontSize: '0.8rem' }}>
                                                    Handled by {report.resolvedByBadge ? `${report.resolvedByBadge} ` : ''}{report.resolvedByLabel || report.resolvedByHash}
                                                    {report.resolvedAt ? ` on ${new Date(Number(report.resolvedAt)).toLocaleString()}` : ''}
                                                </p>
                                            )}
                                        </div>
                                        {targetHref ? (
                                            <Link to={targetHref} className="btn-v2 text-sm" style={{ padding: '9px 12px', textDecoration: 'none' }}>
                                                <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                                                    <ExternalLink size={14} /> Open Target
                                                </span>
                                            </Link>
                                        ) : null}
                                    </div>

                                    <div style={{ display: 'flex', gap: '10px', flexWrap: 'wrap', marginTop: '16px' }}>
                                        <button
                                            className="btn-v2-accent text-sm"
                                            style={{ padding: '9px 12px' }}
                                            onClick={() => updateStatus(report.id, 'resolved')}
                                            disabled={busyId === report.id}
                                        >
                                            <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                                                <CheckCircle2 size={14} /> {busyId === report.id ? 'Updating...' : 'Resolve'}
                                            </span>
                                        </button>
                                        <button
                                            className="btn-v2 text-sm"
                                            style={{ padding: '9px 12px' }}
                                            onClick={() => updateStatus(report.id, 'dismissed')}
                                            disabled={busyId === report.id}
                                        >
                                            <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                                                <Ban size={14} /> Dismiss
                                            </span>
                                        </button>
                                        {!isOpen && (
                                            <button
                                                className="btn-v2 text-sm"
                                                style={{ padding: '9px 12px' }}
                                                onClick={() => updateStatus(report.id, 'open')}
                                                disabled={busyId === report.id}
                                            >
                                                <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                                                    <RotateCcw size={14} /> Reopen
                                                </span>
                                            </button>
                                        )}
                                    </div>
                                </div>
                            );
                        })}
                    </div>
                )}
                </div>
            )}

            {activeTab === 'bans' && (
                loading ? (
                    <div className="flat-card" style={{ minHeight: '220px' }} />
                ) : (
                    <div className="space-y-4">
                        <div className="flat-card" style={{ padding: '20px' }}>
                            <h2 style={{ color: 'var(--text)', fontSize: '1.1rem', marginBottom: '16px', display: 'flex', alignItems: 'center', gap: '8px' }}>
                                <Plus size={18} /> Apply Network Restriction
                            </h2>
                            <form onSubmit={handleCreateBan} style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
                                <div style={{ display: 'flex', gap: '12px', flexWrap: 'wrap' }}>
                                    <div style={{ flex: '1', minWidth: '200px' }}>
                                        <label style={{ display: 'block', color: 'var(--text-dim)', fontSize: '0.85rem', marginBottom: '4px' }}>
                                            Ban Target (Identity Hash or IP Address)
                                        </label>
                                        <input
                                            type="text"
                                            className="form-input"
                                            placeholder="e.g. Identity Hash or IP"
                                            value={banTarget}
                                            onChange={e => setBanTarget(e.target.value)}
                                            required
                                            style={{ width: '100%' }}
                                        />
                                    </div>
                                    <div style={{ width: '150px' }}>
                                        <label style={{ display: 'block', color: 'var(--text-dim)', fontSize: '0.85rem', marginBottom: '4px' }}>
                                            Ban Type
                                        </label>
                                        <select
                                            className="form-input"
                                            value={banType}
                                            onChange={e => setBanType(e.target.value as 'identity' | 'ip')}
                                            style={{ width: '100%', padding: '8px' }}
                                        >
                                            <option value="identity">Identity Hash</option>
                                            <option value="ip">IP Address</option>
                                        </select>
                                    </div>
                                    <div style={{ width: '180px' }}>
                                        <label style={{ display: 'block', color: 'var(--text-dim)', fontSize: '0.85rem', marginBottom: '4px' }}>
                                            Duration
                                        </label>
                                        <select
                                            className="form-input"
                                            value={banDuration}
                                            onChange={e => setBanDuration(e.target.value)}
                                            style={{ width: '100%', padding: '8px' }}
                                        >
                                            <option value="3600">1 Hour</option>
                                            <option value="86400">1 Day</option>
                                            <option value="604800">1 Week</option>
                                            <option value="2592000">30 Days</option>
                                            <option value="31536000">365 Days</option>
                                            <option value="0">Permanent</option>
                                        </select>
                                    </div>
                                </div>
                                <div>
                                    <label style={{ display: 'block', color: 'var(--text-dim)', fontSize: '0.85rem', marginBottom: '4px' }}>
                                        Reason for Ban
                                    </label>
                                    <input
                                        type="text"
                                        className="form-input"
                                        placeholder="Enter the justification for this ban..."
                                        value={banReason}
                                        onChange={e => setBanReason(e.target.value)}
                                        required
                                        style={{ width: '100%' }}
                                    />
                                </div>
                                <div>
                                    <button
                                        type="submit"
                                        className="btn-v2-accent"
                                        disabled={banSubmitting}
                                        style={{ padding: '8px 16px' }}
                                    >
                                        {banSubmitting ? 'Creating Ban...' : 'Enforce Restriction'}
                                    </button>
                                </div>
                            </form>
                        </div>

                        <div className="flat-card" style={{ padding: '20px' }}>
                            <h2 style={{ color: 'var(--text)', fontSize: '1.1rem', marginBottom: '16px', display: 'flex', alignItems: 'center', gap: '8px' }}>
                                <Ban size={18} /> Active Ban Database
                            </h2>
                            {bans.length === 0 ? (
                                <p style={{ color: 'var(--text-dim)', textAlign: 'center', padding: '24px 0' }}>
                                    No network restrictions are currently active.
                                </p>
                            ) : (
                                <div style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
                                    {bans.map(ban => (
                                        <div key={ban.id} className="stat-pill" style={{ padding: '14px', display: 'flex', flexDirection: 'column', gap: '8px' }}>
                                            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', flexWrap: 'wrap', gap: '12px' }}>
                                                <div style={{ minWidth: 0, flex: 1 }}>
                                                    <div style={{ display: 'flex', alignItems: 'center', gap: '8px', flexWrap: 'wrap' }}>
                                                        <span className="identity-badge" style={{ color: ban.ban_type === 'ip' ? 'var(--cyan)' : 'var(--gold)' }}>
                                                            {String(ban.ban_type).toUpperCase()} BAN
                                                        </span>
                                                        <span style={{ color: 'var(--text)', fontFamily: 'monospace', fontSize: '0.85rem', wordBreak: 'break-all' }}>
                                                            {ban.target_identifier}
                                                        </span>
                                                    </div>
                                                    <p style={{ color: 'var(--text)', fontSize: '0.9rem', marginTop: '8px' }}>
                                                        <strong>Reason:</strong> {ban.reason || 'No justification provided'}
                                                    </p>
                                                </div>
                                                <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                                                    <button
                                                        className="btn-v2 text-xs"
                                                        style={{ padding: '6px 10px', display: 'flex', alignItems: 'center', gap: '4px' }}
                                                        onClick={() => handleUnban(ban.id)}
                                                    >
                                                        <Trash2 size={12} /> Lift
                                                    </button>
                                                    <select
                                                        className="btn-v2 text-xs"
                                                        style={{ padding: '6px', cursor: 'pointer' }}
                                                        onChange={(e) => {
                                                            if (e.target.value) {
                                                                handleExtendBan(ban.id, Number(e.target.value));
                                                                e.target.value = '';
                                                            }
                                                        }}
                                                        defaultValue=""
                                                    >
                                                        <option value="" disabled>Extend...</option>
                                                        <option value="86400">+1 Day</option>
                                                        <option value="604800">+1 Week</option>
                                                        <option value="2592000">+30 Days</option>
                                                    </select>
                                                </div>
                                            </div>
                                            <div style={{ display: 'flex', justifyContent: 'space-between', flexWrap: 'wrap', gap: '8px', fontSize: '0.75rem', color: 'var(--text-dim)', borderTop: '1px solid rgba(255,255,255,0.05)', paddingTop: '6px', marginTop: '4px' }}>
                                                <span>
                                                    Enforced: {new Date(ban.created_at).toLocaleString()} by {ban.created_by}
                                                </span>
                                                <span>
                                                    Expires: {ban.expires_at ? new Date(ban.expires_at).toLocaleString() : 'Permanent'}
                                                </span>
                                            </div>
                                        </div>
                                    ))}
                                </div>
                            )}
                        </div>
                    </div>
                )
            )}

            {activeTab === 'audit' && (
                <div className="flat-card" style={{ padding: '20px' }}>
                    <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '12px', marginBottom: '12px', flexWrap: 'wrap' }}>
                        <h2 style={{ color: 'var(--text)', fontSize: '1rem' }}>Moderator Audit Trail</h2>
                        <span className="identity-badge" style={{ color: 'var(--text-dim)' }}>{auditEvents.length} recent actions</span>
                    </div>
                    {auditEvents.length === 0 ? (
                        <p style={{ color: 'var(--text-dim)', lineHeight: 1.5 }}>No moderator actions have been recorded yet.</p>
                    ) : (
                        <div className="space-y-3">
                            {auditEvents.map(event => (
                                <div key={event.id} className="stat-pill" style={{ padding: '12px 14px' }}>
                                    <div style={{ display: 'flex', alignItems: 'center', gap: '8px', flexWrap: 'wrap' }}>
                                        {event.actorBadge && (
                                            <span className="identity-badge" style={{ color: event.actorBadge === 'FOUNDER' ? 'var(--gold)' : 'var(--cyan)' }}>
                                                {event.actorBadge}
                                            </span>
                                        )}
                                        <span style={{ color: 'var(--text)' }}>{event.actorLabel || event.actorHash}</span>
                                        <span style={{ color: 'var(--text-dim)' }}>{event.summary}</span>
                                    </div>
                                    <div style={{ display: 'flex', alignItems: 'center', gap: '10px', flexWrap: 'wrap', marginTop: '8px' }}>
                                        <span className="text-xs" style={{ color: 'var(--text-dim)' }}>
                                            <Clock size={12} style={{ marginRight: '6px', verticalAlign: 'middle' }} />
                                            {new Date(Number(event.createdAt || Date.now())).toLocaleString()}
                                        </span>
                                        {event.targetLink ? (
                                            <Link to={event.targetLink} className="btn-v2 text-xs" style={{ padding: '6px 10px', textDecoration: 'none' }}>
                                                Open Target
                                            </Link>
                                        ) : null}
                                    </div>
                                </div>
                            ))}
                        </div>
                    )}
                </div>
            )}
        </div>
    );
}
