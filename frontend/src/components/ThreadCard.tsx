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
import { MessageSquare, Image, Clock } from 'lucide-react';
import { parsePublicIdentityMarker } from '../utils/publicIdentity';

interface ThreadCardProps {
    thread: Thread;
    boardId: string;
}

function formatRelTime(ts: number): string {
    const diff = Date.now() - ts;
    const minutes = Math.floor(diff / 60000);
    const hours = Math.floor(diff / 3600000);
    const days = Math.floor(diff / 86400000);
    if (minutes < 1) return 'just now';
    if (hours < 1) return `${minutes}m ago`;
    if (days < 1) return `${hours}h ago`;
    return `${days}d ago`;
}

export default function ThreadCard({ thread, boardId }: ThreadCardProps) {
    const preview = thread.op.content.slice(0, 180);
    const opIdentity = parsePublicIdentityMarker(thread.op.name, thread.op.tripcode);
    const opLabel = opIdentity.label || thread.op.name || 'Anonymous';

    return (
        <div
            className="thread-card glass-card p-4 mb-3"
            style={{ borderLeft: thread.op.isEncrypted ? '2px solid rgba(0,240,255,0.4)' : '2px solid rgba(255,215,0,0.3)' }}
        >
            <div className="flex gap-3">
                {thread.op.imageUrl && (
                    <Link to={`/${boardId}/thread/${thread.id}`}>
                        <img
                            src={thread.op.imageUrl}
                            alt=""
                            className="rounded object-cover flex-shrink-0"
                            style={{ width: 100, height: 100, border: '1px solid rgba(255,215,0,0.2)' }}
                            onError={e => (e.currentTarget.style.display = 'none')}
                        />
                    </Link>
                )}

                <div className="flex-1 min-w-0">
                    <div className="flex flex-wrap items-center gap-2 mb-1">
                        <Link
                            to={`/${boardId}/thread/${thread.id}`}
                            className="font-bold text-base hover:underline"
                            style={{ color: 'var(--gold)', textDecoration: 'none' }}
                        >
                            {thread.subject}
                        </Link>
                        {thread.op.isEncrypted && <span className="quantum-badge">Encrypted</span>}
                        {thread.sticky && (
                            <span
                                className="text-xs px-1.5 py-0.5 rounded"
                                style={{ background: 'rgba(0,240,255,0.1)', color: '#00f0ff', border: '1px solid rgba(0,240,255,0.3)' }}
                            >
                                Sticky
                            </span>
                        )}
                    </div>

                    <div className="flex items-center gap-3 mb-2 text-xs" style={{ color: 'var(--text-dim)', fontFamily: 'monospace' }}>
                        <span style={{ color: '#ffd700' }}>{opLabel}</span>
                        <span className="flex items-center gap-1">
                            <Clock size={10} />{formatRelTime(thread.lastBump)}
                        </span>
                        <span>No.{thread.op.no}</span>
                    </div>

                    <p className="text-sm leading-relaxed mb-2" style={{ color: 'var(--text-muted)', wordBreak: 'break-word' }}>
                        {thread.op.isEncrypted
                            ? '[Encrypted post - open thread to decrypt with passphrase]'
                            : preview + (thread.op.content.length > 180 ? '...' : '')}
                    </p>

                    <div className="flex items-center gap-4 text-xs" style={{ color: 'var(--text-dim)', fontFamily: 'monospace' }}>
                        <span className="flex items-center gap-1">
                            <MessageSquare size={11} style={{ color: '#ffd700' }} />
                            {thread.replyCount} replies
                        </span>
                        {thread.imageCount > 0 && (
                            <span className="flex items-center gap-1">
                                <Image size={11} style={{ color: '#ffd700' }} />
                                {thread.imageCount} images
                            </span>
                        )}
                        <Link
                            to={`/${boardId}/thread/${thread.id}`}
                            className="ml-auto text-xs px-2 py-0.5 rounded hover:opacity-80 transition-opacity"
                            style={{ background: 'rgba(255,215,0,0.1)', color: '#ffd700', border: '1px solid rgba(255,215,0,0.25)', textDecoration: 'none' }}
                        >
                            Open Thread
                        </Link>
                    </div>
                </div>
            </div>
        </div>
    );
}
