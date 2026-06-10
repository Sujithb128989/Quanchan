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
import type { Thread } from '../types';
import { MessageSquare, Lock } from 'lucide-react';

interface CatalogCardProps {
    thread: Thread;
    boardId: string;
}

export default function CatalogCard({ thread, boardId }: CatalogCardProps) {
    return (
        <Link
            to={`/${boardId}/thread/${thread.id}`}
            className="flat-card block"
            style={{ textDecoration: 'none', height: '100%' }}
        >
            {/* Image */}
            <div style={{
                height: 140, overflow: 'hidden',
                background: 'var(--surface)',
                borderBottom: '1px solid var(--border)',
                display: 'flex', alignItems: 'center', justifyContent: 'center',
            }}>
                {thread.op.imageUrl ? (
                    <img
                        src={thread.op.imageUrl} alt=""
                        style={{ width: '100%', height: '100%', objectFit: 'cover' }}
                        onError={e => (e.currentTarget.style.display = 'none')}
                    />
                ) : (
                    <span className="text-sm font-mono" style={{ color: 'var(--text-dim)' }}>No image</span>
                )}
            </div>

            <div style={{ padding: '10px 12px' }}>
                {/* Subject */}
                <div className="flex items-start gap-1 mb-1">
                    {thread.op.isEncrypted && <Lock size={10} style={{ color: 'var(--cyan)', marginTop: '2px', flexShrink: 0 }} />}
                    <p className="text-xs font-semibold" style={{ color: 'var(--text)', overflow: 'hidden', display: '-webkit-box', WebkitLineClamp: 2, WebkitBoxOrient: 'vertical' }}>
                        {thread.subject}
                    </p>
                </div>

                {/* Preview */}
                <p className="text-xs" style={{
                    color: 'var(--text-dim)', lineHeight: 1.4, marginBottom: '8px',
                    overflow: 'hidden', display: '-webkit-box', WebkitLineClamp: 3, WebkitBoxOrient: 'vertical',
                }}>
                    {thread.op.isEncrypted ? 'Encrypted content' : thread.op.content.slice(0, 100)}
                </p>

                {/* Replies */}
                <div className="flex items-center gap-1 text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                    <MessageSquare size={10} /> {thread.replyCount}
                </div>
            </div>
        </Link>
    );
}
