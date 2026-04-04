import { useParams, Link } from 'react-router-dom';
import { useStore } from '../store/useStore';
import CatalogCard from '../components/CatalogCard';
import { List } from 'lucide-react';
import { useEffect } from 'react';

export default function CatalogPage() {
    const { board } = useParams<{ board: string }>();
    const boardId = board || '';
    const boards = useStore(s => s.boards);
    const threads = useStore(s => s.threads);
    const fetchThreads = useStore(s => (s as any).fetchThreads);
    const boardData = boards.find(b => b.id === boardId);

    useEffect(() => {
        if (boardId && fetchThreads) fetchThreads(boardId);
    }, [boardId, fetchThreads]);

    const boardThreads = threads
        .filter(t => t.boardId === boardId && !t.archived)
        .sort((a, b) => b.lastBump - a.lastBump);

    if (!boardData) {
        return (
            <div style={{ padding: '32px', textAlign: 'center' }}>
                <p className="text-sm font-mono" style={{ color: 'var(--text-dim)' }}>Board not found.</p>
                <Link to="/directory" className="text-sm font-mono mt-4 inline-block" style={{ color: 'var(--text-muted)' }}>← Back to directory</Link>
            </div>
        );
    }

    return (
        <div style={{ padding: '16px' }}>
            <div className="flex items-center justify-between mb-4" style={{ borderBottom: '1px solid var(--border)', paddingBottom: '12px' }}>
                <h1 className="text-lg font-black font-mono" style={{ color: '#fff' }}>/{boardId}/ Catalog</h1>
                <div className="flex gap-2">
                    <Link to={`/${boardId}`} className="btn-v2 flex items-center gap-1 text-xs" style={{ padding: '4px 10px', textDecoration: 'none', fontFamily: 'var(--font-mono)' }}>
                        <List size={12} /> Index
                    </Link>
                    <Link to={`/${boardId}/archive`} className="btn-v2 text-xs" style={{ padding: '4px 10px', textDecoration: 'none', fontFamily: 'var(--font-mono)' }}>
                        Archive
                    </Link>
                </div>
            </div>

            {boardThreads.length === 0 ? (
                <div className="flat-card p-4 text-center" style={{ color: 'var(--text-dim)' }}>
                    <p className="font-mono text-sm">No threads to display.</p>
                </div>
            ) : (
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(180px, 1fr))', gap: '12px' }}>
                    {boardThreads.map(thread => (
                        <CatalogCard key={thread.id} thread={thread} boardId={boardId} />
                    ))}
                </div>
            )}
        </div>
    );
}
