import { useParams, Link } from 'react-router-dom';
import { useStore } from '../store/useStore';
import { useIsPhone } from '../hooks/useIsPhone';
import PostForm from '../components/PostForm';
import { useEffect } from 'react';
import { MessageSquare, Image, Clock } from 'lucide-react';
import { parsePublicIdentityMarker } from '../utils/publicIdentity';

export default function BoardPage() {
    const { board } = useParams<{ board: string }>();
    const boardId = board || '';
    const threads = useStore(s => s.threads);
    const boards = useStore(s => s.boards);
    const threadsState = useStore(s => s.threadsState);
    const threadsError = useStore(s => s.threadsError);
    const fetchThreads = useStore(s => (s as any).fetchThreads);
    const isPhone = useIsPhone();

    const boardMeta = boards.find(b => b.id === boardId);

    useEffect(() => {
        if (boardId && fetchThreads) fetchThreads(boardId);
    }, [boardId, fetchThreads]);

    if (!boardId) return null;

    return (
        <div style={{ padding: '16px' }}>
            {!isPhone && (
                <div className="mb-6" style={{ borderBottom: '1px solid var(--border)', paddingBottom: '16px' }}>
                    <h1 className="text-xl font-black font-mono" style={{ color: 'var(--text)' }}>
                        /{boardId}/ - {boardMeta?.name || boardId}
                    </h1>
                    {boardMeta?.description && (
                        <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>{boardMeta.description}</p>
                    )}
                </div>
            )}

            <div className="mb-6">
                <PostForm boardId={boardId} />
            </div>

            <div className="space-y-3">
                {threadsState === 'error' ? (
                    <div className="flat-card p-4 text-center" style={{ color: 'var(--red)', lineHeight: 1.6 }}>
                        <p className="font-mono text-sm">
                            Failed to load /{boardId}/. {threadsError || 'The backend is unavailable right now.'}
                        </p>
                    </div>
                ) : threadsState === 'loading' ? (
                    <div className="flat-card p-4 text-center" style={{ color: 'var(--text-dim)' }}>
                        <p className="font-mono text-sm">Loading threads...</p>
                    </div>
                ) : threads.length === 0 ? (
                    <div className="flat-card p-4 text-center" style={{ color: 'var(--text-dim)' }}>
                        <p className="font-mono text-sm">No threads yet. Be the first to post.</p>
                    </div>
                ) : (
                    threads.map(t => (
                        (() => {
                            const opIdentity = parsePublicIdentityMarker(t.op.name, t.op.tripcode);
                            const opLabel = opIdentity.label || t.op.name || 'Anonymous';
                            return (
                                <Link
                                    key={t.id}
                                    to={`/${boardId}/thread/${t.id}`}
                                    state={{ threadSubject: t.subject || `Thread No.${t.id}` }}
                                    style={{ textDecoration: 'none', display: 'block' }}
                                >
                                    <div className="thread-card-v2">
                                        <div className="flex items-start gap-3">
                                            {t.op.imageUrl && (
                                                <img
                                                    src={t.op.imageUrl}
                                                    alt=""
                                                    style={{
                                                        width: '60px',
                                                        height: '60px',
                                                        objectFit: 'cover',
                                                        flexShrink: 0,
                                                        border: '1px solid var(--border)',
                                                        borderRadius: '2px',
                                                    }}
                                                    onError={e => (e.currentTarget.style.display = 'none')}
                                                />
                                            )}
                                            <div className="flex-1" style={{ minWidth: 0 }}>
                                                <div className="flex items-center gap-2 mb-1">
                                                    <span className="font-bold text-sm" style={{ color: 'var(--text)' }}>
                                                        {t.subject || 'No Subject'}
                                                    </span>
                                                    <span className="text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                                                        No.{t.id}
                                                    </span>
                                                </div>
                                                <p className="text-sm" style={{ color: 'var(--text-muted)', maxWidth: '500px', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                                                    {t.op.content || '(no content)'}
                                                </p>
                                                <div className="flex items-center gap-3 mt-2 text-xs" style={{ color: 'var(--text-dim)' }}>
                                                    <span className="flex items-center gap-1 font-mono">
                                                        <MessageSquare size={11} /> {t.replyCount} replies
                                                    </span>
                                                    <span className="flex items-center gap-1 font-mono">
                                                        <Image size={11} /> {t.imageCount} images
                                                    </span>
                                                    <span className="flex items-center gap-1 font-mono">
                                                        <Clock size={11} /> {new Date(t.lastBump).toLocaleString()}
                                                    </span>
                                                    <span className="font-mono" style={{ color: 'var(--text-dim)' }}>
                                                        by {opLabel}
                                                    </span>
                                                </div>
                                            </div>
                                        </div>
                                    </div>
                                </Link>
                            );
                        })()
                    ))
                )}
            </div>
        </div>
    );
}
