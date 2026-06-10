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
import React, { useState, useRef, useEffect } from 'react';
import { Send, Paperclip, X, Image as ImageIcon } from 'lucide-react';
import { useStore } from '../store/useStore';
import { useIdentity } from '../hooks/useIdentity';
import { buildPublicIdentityMarker } from '../utils/publicIdentity';

const POST_CREATED_EVENT = 'quanchan:post-created';

interface PostFormProps {
    boardId: string;
    threadId?: number;
    initialContent?: string;
    onSuccess?: () => void;
    /** For thread page: start collapsed, show only when triggered */
    startCollapsed?: boolean;
}

export default function PostForm({ boardId, threadId, initialContent, onSuccess, startCollapsed = false }: PostFormProps) {
    const {
        encryptionState,
        setEncryptionState,
        setQuantumModalVisible,
        setPendingPost,
        createThread,
        createReply,
    } = useStore();

    const [collapsed, setCollapsed] = useState(startCollapsed);
    const [content, setContent] = useState(initialContent || '');
    useEffect(() => {
        if (initialContent) {
            setContent(initialContent);
            setCollapsed(false); // auto-open when user clicks Reply
        }
    }, [initialContent]);
    const [subject, setSubject] = useState('');
    const [imageUrl, setImageUrl] = useState('');
    const [usePublicName, setUsePublicName] = useState(false);
    const [isUploading, setIsUploading] = useState(false);
    const [isDragging, setIsDragging] = useState(false);
    const [error, setError] = useState('');
    const fileInputRef = useRef<HTMLInputElement>(null);
    const { identity } = useIdentity();

    const isThread = !threadId;

    async function handleFileUpload(file: File) {
        const form = new FormData();
        form.append('file', file);
        setIsUploading(true);
        try {
            const res = await fetch('/api/upload', { method: 'POST', body: form });
            if (res.ok) {
                const data = await res.json();
                setImageUrl(data.url);
            } else {
                setError('Upload failed');
            }
        } catch {
            setError('Upload failed');
        }
        setIsUploading(false);
    }

    function handleFileChange(e: React.ChangeEvent<HTMLInputElement>) {
        const file = e.target.files?.[0];
        if (file) handleFileUpload(file);
    }

    function getPostName(): string {
        if (usePublicName && identity) {
            const displayName = identity.username || identity.displayHash;
            return buildPublicIdentityMarker(displayName, identity.displayHash);
        }
        return 'Anonymous';
    }

    useEffect(() => {
        function handlePostCreated(event: Event) {
            const customEvent = event as CustomEvent<{ kind: 'thread' | 'reply'; boardId: string; threadId: number | null }>;
            const detail = customEvent.detail;
            if (!detail) return;

            const matchesThreadForm = isThread && detail.kind === 'thread' && detail.boardId === boardId;
            const matchesReplyForm = !isThread && detail.kind === 'reply' && detail.threadId === threadId;

            if (!matchesThreadForm && !matchesReplyForm) return;

            setContent('');
            setSubject('');
            setImageUrl('');
            setError('');
            if (startCollapsed) setCollapsed(true);
            onSuccess?.();
        }

        window.addEventListener(POST_CREATED_EVENT, handlePostCreated as EventListener);
        return () => window.removeEventListener(POST_CREATED_EVENT, handlePostCreated as EventListener);
    }, [boardId, isThread, onSuccess, startCollapsed, threadId]);

    async function handleSubmit(e: React.FormEvent) {
        e.preventDefault();
        if (!content.trim()) return;
        setError('');
        const finalName = getPostName();

        if (encryptionState.enabled) {
            setPendingPost({
                boardId,
                threadId: threadId ?? null,
                content,
                subject,
                imageUrl,
                name: finalName,
                authorHash: usePublicName ? (identity?.displayHash || '') : '',
            });
            setQuantumModalVisible(true);
        } else {
            try {
                if (threadId) {
                    await createReply(boardId, threadId, content, imageUrl, finalName, usePublicName ? (identity?.displayHash || '') : '');
                } else {
                    await createThread(boardId, subject, content, imageUrl, finalName, usePublicName ? (identity?.displayHash || '') : '');
                }
            } catch (e: any) {
                let msg = 'Failed to submit post.';
                try { const parsed = JSON.parse(e?.message || ''); msg = parsed.error || msg; } catch { /* use default */ }
                setError(msg);
            }
        }
    }

    const handleDragOver = (e: React.DragEvent) => { e.preventDefault(); setIsDragging(true); };
    const handleDragLeave = (e: React.DragEvent) => { e.preventDefault(); setIsDragging(false); };
    const handleDrop = (e: React.DragEvent) => { e.preventDefault(); setIsDragging(false); const file = e.dataTransfer.files?.[0]; if (file) handleFileUpload(file); };

    // Collapsed state â€” just show a bar
    if (collapsed) {
        return (
            <button
                onClick={() => setCollapsed(false)}
                className="w-full text-sm font-mono py-3 px-4 flat-card text-center cursor-pointer transition-colors"
                style={{ color: 'var(--text-dim)', border: '1px dashed var(--border)' }}
            >
                + Write a reply
            </button>
        );
    }

    return (
        <form
            onSubmit={handleSubmit}
            onDragOver={handleDragOver}
            onDragLeave={handleDragLeave}
            onDrop={handleDrop}
            className="flat-card p-4 space-y-3"
            style={{ borderColor: isDragging ? 'var(--border-hover)' : 'var(--border)' }}
        >
            {/* Header */}
            <div className="flex flex-wrap items-center w-full gap-2 mb-2" style={{ justifyContent: 'space-between' }}>
                <h3 className="text-sm font-bold" style={{ color: 'var(--text)', fontFamily: 'var(--font-mono)' }}>
                    {isThread ? '[ New Thread ]' : '[ Reply ]'}
                </h3>
                <div className="flex items-center gap-2 ml-auto">
                    <label className="flex items-center gap-1 cursor-pointer text-xs" style={{ color: encryptionState.enabled ? 'var(--cyan)' : 'var(--text-dim)' }}>
                        <input
                            type="checkbox"
                            checked={encryptionState.enabled}
                            onChange={e => setEncryptionState({ enabled: e.target.checked })}
                            style={{ accentColor: 'var(--accent)', width: '14px', height: '14px' }}
                        />
                        Encrypt post
                    </label>
                    {/* Simple checkbox: Use public name? */}
                    {identity && (
                        <label className="flex items-center gap-1 cursor-pointer text-xs" style={{ color: usePublicName ? 'var(--accent)' : 'var(--text-dim)' }}>
                            <input
                                type="checkbox"
                                checked={usePublicName}
                                onChange={e => setUsePublicName(e.target.checked)}
                                style={{ accentColor: 'var(--accent)', width: '14px', height: '14px' }}
                            />
                            Use public name
                        </label>
                    )}
                    {/* Close button for replies */}
                    {startCollapsed && (
                        <button
                            type="button"
                            onClick={() => { setCollapsed(true); setContent(''); }}
                            className="btn-v2 p-1"
                            title="Close"
                        >
                            <X size={14} />
                        </button>
                    )}
                </div>
            </div>

            {/* Subject (only for new threads) */}
            {isThread && (
                <input
                    className="v2-input w-full text-sm px-3 py-2"
                    placeholder="Subject *"
                    value={subject}
                    onChange={e => setSubject(e.target.value)}
                    required
                />
            )}

            {/* File upload */}
            <div className="flex items-center gap-2">
                <button
                    type="button"
                    onClick={() => fileInputRef.current?.click()}
                    className="btn-v2 p-1"
                    title="Attach file"
                >
                    <Paperclip size={13} />
                </button>
                <input
                    type="file"
                    ref={fileInputRef}
                    onChange={handleFileChange}
                    accept="image/*,video/*"
                    className="hidden"
                />

                {isUploading ? (
                    <div className="flex-1 px-2 py-1 text-xs animate-pulse font-mono" style={{ color: 'var(--cyan)' }}>
                        Uploading...
                    </div>
                ) : imageUrl ? (
                    <div className="flex-1 flex items-center justify-between px-2 py-1" style={{ background: 'var(--surface-2)', border: '1px solid var(--border)', borderRadius: '4px' }}>
                        <span className="text-xs truncate font-mono" style={{ color: 'var(--cyan)' }}>
                            {imageUrl.split('/').pop()}
                        </span>
                        <button type="button" onClick={() => setImageUrl('')} style={{ background: 'none', border: 'none', color: 'var(--text-dim)', cursor: 'pointer' }}>
                            <X size={12} />
                        </button>
                    </div>
                ) : (
                    <>
                        <ImageIcon size={13} style={{ color: 'var(--text-dim)' }} />
                        <input
                            className="v2-input flex-1 text-sm px-2 py-1"
                            placeholder="Image URL or attach file"
                            value={imageUrl}
                            onChange={e => setImageUrl(e.target.value)}
                        />
                    </>
                )}
            </div>

            {/* Content */}
            <textarea
                className="v2-input w-full text-sm px-3 py-2 resize-none"
                placeholder="Write your post..."
                rows={4}
                value={content}
                onChange={e => setContent(e.target.value)}
                required
            />

            {error && (
                <p className="text-xs font-mono" style={{ color: 'var(--red)' }}>âš  {error}</p>
            )}

            <div className="flex flex-wrap items-center mt-2 w-full gap-3" style={{ justifyContent: 'space-between' }}>
                <span className="text-xs font-mono" style={{ color: 'var(--text-dim)', flex: '1 1 180px' }}>
                    {usePublicName && identity ? `Posting as: ${identity.username || identity.displayHash}` : 'Posting as: Anonymous'}
                    {encryptionState.enabled ? ' â€¢ Encrypted locally before upload' : ''}
                </span>
                <button
                    type="submit"
                    disabled={isUploading}
                    className="btn-v2-accent flex items-center justify-center gap-2 px-4 py-2 text-sm"
                    style={{ opacity: isUploading ? 0.5 : 1, minWidth: '132px', flexShrink: 0 }}
                >
                    <Send size={13} /> {isThread ? 'Create Thread' : 'Post Reply'}
                </button>
            </div>
        </form>
    );
}
