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
import { Link } from 'react-router-dom';
import { useStore } from '../store/useStore';
import { useIsPhone } from '../hooks/useIsPhone';
import { Hash } from 'lucide-react';

export default function DirectoryPage() {
    const boards = useStore(s => s.boards);
    const boardsState = useStore(s => s.boardsState);
    const boardsError = useStore(s => s.boardsError);
    const isPhone = useIsPhone();

    return (
        <div style={{ padding: '16px' }}>
            {!isPhone && (
            <div className="mb-6" style={{ borderBottom: '1px solid var(--border)', paddingBottom: '16px' }}>
                <h1 className="text-xl font-black font-mono" style={{ color: 'var(--text)' }}>
                    /directory/
                </h1>
                <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>All boards on the network.</p>
            </div>
            )}

            {boardsState === 'error' ? (
                <div className="flat-card p-4" style={{ color: 'var(--red)', lineHeight: 1.6 }}>
                    Failed to load boards. {boardsError || 'The backend is unavailable right now.'}
                </div>
            ) : boardsState === 'loading' ? (
                <div className="flat-card p-4 text-center" style={{ color: 'var(--text-dim)' }}>
                    <p className="font-mono text-sm">Loading boards...</p>
                </div>
            ) : (
                <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 gap-3">
                    {boards.map(b => (
                        <Link key={b.id} to={`/${b.id}`} style={{ textDecoration: 'none' }}>
                            <div className="flat-card p-4 transition-colors" style={{ cursor: 'pointer' }}>
                                <div className="flex items-center gap-2 mb-2">
                                    <Hash size={14} style={{ color: 'var(--text-dim)' }} />
                                    <span className="font-bold font-mono" style={{ color: 'var(--text)' }}>/{b.id}/</span>
                                </div>
                                <p className="text-sm font-semibold mb-1" style={{ color: 'var(--text)' }}>{b.name}</p>
                                <p className="text-xs" style={{ color: 'var(--text-dim)' }}>{b.description}</p>
                                <div className="flex gap-3 mt-3 text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                                    <span>{b.threadCount} threads</span>
                                    <span>{b.postCount} posts</span>
                                    {b.nsfw && <span style={{ color: 'var(--red)' }}>NSFW</span>}
                                </div>
                            </div>
                        </Link>
                    ))}
                </div>
            )}
        </div>
    );
}
