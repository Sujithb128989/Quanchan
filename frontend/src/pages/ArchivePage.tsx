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
import { useParams, Link } from 'react-router-dom';
import { useStore } from '../store/useStore';
import { Archive, ExternalLink } from 'lucide-react';

function formatDate(ts: number) {
    return new Date(ts).toLocaleDateString('en-US', { year: 'numeric', month: 'short', day: 'numeric' });
}

export default function ArchivePage() {
    const { board } = useParams<{ board: string }>();
    const boardId = board || '';
    const boards = useStore(s => s.boards);
    const threads = useStore(s => s.threads);
    const boardData = boards.find(b => b.id === boardId);

    const archivedThreads = threads
        .filter(t => t.boardId === boardId && t.archived)
        .sort((a, b) => b.lastBump - a.lastBump);

    const activeThreads = threads
        .filter(t => t.boardId === boardId && !t.archived)
        .sort((a, b) => a.lastBump - b.lastBump)
        .slice(0, 3);

    if (!boardData) {
        return (
            <div style={{ padding: '32px', textAlign: 'center' }}>
                <p className="text-sm font-mono" style={{ color: 'var(--text-dim)' }}>Board not found.</p>
            </div>
        );
    }

    const allArchive = [...archivedThreads, ...activeThreads].slice(0, 20);

    return (
        <div style={{ padding: '16px' }}>
            <div className="flex items-center justify-between mb-4" style={{ borderBottom: '1px solid var(--border)', paddingBottom: '12px' }}>
                <div className="flex items-center gap-2">
                    <Archive size={16} style={{ color: 'var(--text-dim)' }} />
                    <h1 className="text-lg font-black font-mono" style={{ color: 'var(--text)' }}>/{boardId}/ Archive</h1>
                </div>
                <div className="flex gap-2">
                    <Link to={`/${boardId}`} className="btn-v2 text-xs" style={{ padding: '4px 10px', textDecoration: 'none', fontFamily: 'var(--font-mono)' }}>Index</Link>
                    <Link to={`/${boardId}/catalog`} className="btn-v2 text-xs" style={{ padding: '4px 10px', textDecoration: 'none', fontFamily: 'var(--font-mono)' }}>Catalog</Link>
                </div>
            </div>

            <div className="flat-card" style={{ overflow: 'hidden' }}>
                <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: '0.85rem' }}>
                    <thead>
                        <tr style={{ borderBottom: '1px solid var(--border)', background: 'var(--surface-2)' }}>
                            {['No.', 'Date', 'Subject', 'Replies', ''].map(h => (
                                <th key={h} style={{
                                    padding: '10px 14px', textAlign: 'left', fontSize: '0.75rem',
                                    color: 'var(--text-dim)', fontFamily: 'var(--font-mono)', fontWeight: 600,
                                }}>{h}</th>
                            ))}
                        </tr>
                    </thead>
                    <tbody>
                        {allArchive.length === 0 ? (
                            <tr>
                                <td colSpan={5} style={{ padding: '32px', textAlign: 'center', color: 'var(--text-dim)', fontSize: '0.8rem' }}>
                                    Archive is empty.
                                </td>
                            </tr>
                        ) : allArchive.map((thread, i) => (
                            <tr key={thread.id} style={{
                                borderBottom: '1px solid var(--border)',
                                background: i % 2 === 0 ? 'transparent' : 'var(--surface)',
                            }}>
                                <td style={{ padding: '8px 14px', fontFamily: 'var(--font-mono)', color: 'var(--text-dim)', fontSize: '0.8rem' }}>{thread.op.no}</td>
                                <td style={{ padding: '8px 14px', fontFamily: 'var(--font-mono)', color: 'var(--text-dim)', fontSize: '0.8rem' }}>{formatDate(thread.lastBump)}</td>
                                <td style={{ padding: '8px 14px', color: 'var(--text)', fontSize: '0.8rem' }}>{thread.subject}</td>
                                <td style={{ padding: '8px 14px', fontFamily: 'var(--font-mono)', color: 'var(--text-dim)', fontSize: '0.8rem' }}>{thread.replyCount}</td>
                                <td style={{ padding: '8px 14px' }}>
                                    <Link to={`/${boardId}/thread/${thread.id}`} className="flex items-center gap-1 text-xs" style={{ color: 'var(--text-muted)', textDecoration: 'none' }}>
                                        <ExternalLink size={10} /> View
                                    </Link>
                                </td>
                            </tr>
                        ))}
                    </tbody>
                </table>
            </div>
        </div>
    );
}
