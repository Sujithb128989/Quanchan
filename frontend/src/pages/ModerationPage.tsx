import { useEffect, useMemo, useState } from 'react';
import { Link, Navigate } from 'react-router-dom';
import { Shield, Clock, ExternalLink, CheckCircle2, Ban, RotateCcw } from 'lucide-react';
import { useStore } from '../store/useStore';
import { useIdentity } from '../hooks/useIdentity';
import { getFounderToken, normalizeProfileRole } from '../utils/roleAuth';

type ReportStatus = 'open' | 'resolved' | 'dismissed';

export default function ModerationPage() {
    const identity = useIdentity();
    const getModerationReports = useStore(s => (s as any).getModerationReports as (actorHash: string, founderToken?: string, limit?: number) => Promise<{ reports: any[] }>);
    const getModerationAudit = useStore(s => (s as any).getModerationAudit as (actorHash: string, founderToken?: string, limit?: number) => Promise<{ events: any[] }>);
    const resolveModerationReport = useStore(s => (s as any).resolveModerationReport as (reportId: number, actorHash: string, status: ReportStatus, note?: string, founderToken?: string) => Promise<any>);
    const [reports, setReports] = useState<any[]>([]);
    const [auditEvents, setAuditEvents] = useState<any[]>([]);
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState('');
    const [busyId, setBusyId] = useState<number | null>(null);
    const [viewerRole, setViewerRole] = useState<'user' | 'moderator' | 'founder'>(() => normalizeProfileRole(localStorage.getItem('quanchan_self_role') || undefined));
    const [roleState, setRoleState] = useState<'loading' | 'ready'>('loading');
    const [founderTokenVersion, setFounderTokenVersion] = useState(0);
    const founderToken = getFounderToken();

    async function loadReports() {
        if (!identity.identity?.displayHash) {
            setReports([]);
            setLoading(false);
            return;
        }
        if (viewerRole === 'user') {
            setReports([]);
            setAuditEvents([]);
            setLoading(false);
            return;
        }
        try {
            setLoading(true);
            setError('');
            const [reportData, auditData] = await Promise.all([
                getModerationReports(identity.identity.displayHash, founderToken, 100),
                getModerationAudit(identity.identity.displayHash, founderToken, 40),
            ]);
            setReports(Array.isArray(reportData?.reports) ? reportData.reports : []);
            setAuditEvents(Array.isArray(auditData?.events) ? auditData.events : []);
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
    }, [identity.identity?.displayHash, founderTokenVersion]);

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
            await resolveModerationReport(reportId, identity.identity.displayHash, status, '', founderToken);
            await loadReports();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'Could not update report.');
        } finally {
            setBusyId(null);
        }
    }

    if (!identity.identity?.displayHash) {
        return <Navigate to="/directory" replace />;
    }

    if (roleState === 'loading') {
        return (
            <div style={{ padding: '20px', maxWidth: '980px', margin: '0 auto 120px' }}>
                <div className="flat-card" style={{ minHeight: '220px' }} />
            </div>
        );
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
                            Moderation Queue
                        </h1>
                        <p style={{ color: 'var(--text-dim)', marginTop: '8px', lineHeight: 1.6 }}>
                            Post and user reports land here. Moderators can review them, jump to the target, and mark them resolved.
                        </p>
                    </div>
                    <div className="stat-pill" style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
                        <Shield size={14} />
                        <span style={{ color: 'var(--text)' }}>{openCount} open</span>
                    </div>
                </div>
                {error && (
                    <p style={{ color: 'var(--red)', marginTop: '12px', lineHeight: 1.5 }}>
                        {error}
                    </p>
                )}
            </div>

            {loading ? (
                <div className="flat-card" style={{ minHeight: '220px' }} />
            ) : reports.length === 0 ? (
                <div className="space-y-4">
                    <div className="flat-card" style={{ padding: '24px', color: 'var(--text-dim)', textAlign: 'center' }}>
                        No reports yet.
                    </div>
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
                </div>
            ) : (
                <div className="space-y-4">
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

                    {reports.map(report => {
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
    );
}
