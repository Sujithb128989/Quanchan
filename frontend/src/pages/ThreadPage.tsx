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
import { useBackendThread } from '../hooks/useBackendThread';
import { useIsPhone } from '../hooks/useIsPhone';
import { useIdentity } from '../hooks/useIdentity';
import Post from '../components/Post';
import PostForm from '../components/PostForm';
import { ArrowLeft, MessageSquareReply, Sparkles } from 'lucide-react';
import { useState, useRef } from 'react';

function extractBacklinkedPostNos(content: string): number[] {
    const backlinks: number[] = [];
    const backlinkRegex = />>(\d+)/g;
    let match: RegExpExecArray | null;

    while ((match = backlinkRegex.exec(content)) !== null) {
        backlinks.push(Number(match[1]));
    }

    return backlinks;
}

export default function ThreadPage() {
    const { board, id } = useParams<{ board: string; id: string }>();
    const boardId = board || '';
    const tid = Number(id) || 0;
    const isPhone = useIsPhone();
    const { identity } = useIdentity();
    const { createPayment } = useStore();

    const { thread: backendThread, verified, cipherSuite, loading, refetch } = useBackendThread(boardId, tid);
    const storeThread = useStore(s => s.threads.find(t => t.id === tid && t.boardId === boardId));
    const thread = backendThread || storeThread;

    const [replyPrefix, setReplyPrefix] = useState('');
    const formRef = useRef<HTMLDivElement>(null);

    // Paid Bump states
    const [isBumpModalOpen, setIsBumpModalOpen] = useState(false);
    const [payCurrency, setPayCurrency] = useState<'btc' | 'ltc' | 'xmr'>('btc');
    const [invoice, setInvoice] = useState<any>(null);
    const [paymentLoading, setPaymentLoading] = useState(false);
    const [paymentError, setPaymentError] = useState('');


    function handleReplyTo(postNo: number) {
        setReplyPrefix(`>>${postNo}\n`);
        formRef.current?.scrollIntoView({ behavior: 'smooth', block: 'center' });
    }

    function handlePostSuccess() {
        refetch();
    }

    const handleCreateBumpInvoice = async () => {
        if (!identity) return;
        try {
            setPaymentLoading(true);
            setPaymentError('');
            const data = await createPayment(identity.displayHash, 'sticky', payCurrency, undefined, String(tid));
            setInvoice(data);
        } catch (err) {
            console.error('Invoice creation failed:', err);
            setPaymentError(err instanceof Error ? err.message : 'Invoice creation failed.');
        } finally {
            setPaymentLoading(false);
        }
    };



    if (!boardId) return null;

    if (loading) {
        return (
            <div style={{ padding: '32px', textAlign: 'center' }}>
                <div className="text-sm font-mono animate-pulse" style={{ color: 'var(--text-dim)' }}>
                    Loading thread...
                </div>
            </div>
        );
    }

    if (!thread) {
        return (
            <div style={{ padding: '32px', textAlign: 'center' }}>
                <p className="text-sm font-mono" style={{ color: 'var(--text-dim)' }}>Thread not found.</p>
                <Link to={`/${boardId}`} className="text-sm font-mono mt-4 inline-block">
                    Back to /{boardId}/
                </Link>
            </div>
        );
    }

    const allPosts = [thread.op, ...thread.replies];
    const rootReplies = thread.replies.filter(reply => {
        const backlinks = extractBacklinkedPostNos(reply.content);
        return !backlinks.some(linkedNo => thread.replies.some(other => other.no === linkedNo));
    });

    return (
        <div style={{ padding: '16px' }}>
            {isPhone && (
                <div className="flex items-center justify-between mb-3 px-1">
                    <Link to={`/${boardId}`} className="text-xs font-mono flex items-center gap-1" style={{ color: 'var(--text-dim)', textDecoration: 'none' }}>
                        <ArrowLeft size={12} /> Back
                    </Link>
                    {identity && !thread.sticky && (
                        <button
                            onClick={() => {
                                setIsBumpModalOpen(true);
                                setInvoice(null);
                                setPaymentError('');
                            }}
                            className="btn-v2-accent flex items-center gap-1 text-xs"
                            style={{ padding: '4px 10px' }}
                        >
                            <Sparkles size={12} /> Bump ($5.00)
                        </button>
                    )}
                </div>
            )}

            {!isPhone && (
                <div className="flex items-center justify-between mb-4" style={{ borderBottom: '1px solid var(--border)', paddingBottom: '12px' }}>
                    <div className="flex items-center gap-3">
                        <Link to={`/${boardId}`} style={{ color: 'var(--text-dim)', textDecoration: 'none' }}>
                            <ArrowLeft size={16} />
                        </Link>
                        <div>
                            <div className="flex items-center gap-2">
                                <h1 className="text-lg font-bold" style={{ color: 'var(--text)' }}>{thread.subject}</h1>
                                {thread.sticky && (
                                    <span className="identity-badge px-2 py-0.5 text-xs font-bold text-amber-400 border border-amber-500 bg-amber-950/30">
                                        Pinned
                                    </span>
                                )}
                            </div>
                            <span className="text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                                /{boardId}/ - Thread No.{thread.id} - {thread.replyCount} replies
                            </span>
                        </div>
                    </div>
                    {identity && !thread.sticky && (
                        <button
                            onClick={() => {
                                setIsBumpModalOpen(true);
                                setInvoice(null);
                                setPaymentError('');
                            }}
                            className="btn-v2-accent flex items-center gap-1.5 text-xs"
                            style={{ padding: '8px 14px' }}
                        >
                            <Sparkles size={14} />
                            Bump Thread ($5.00)
                        </button>
                    )}
                </div>
            )}

            <div className="mb-4">
                <Post
                    post={thread.op}
                    isOP
                    verified={verified}
                    cipherSuite={cipherSuite}
                    allPosts={allPosts}
                    onReplyTo={handleReplyTo}
                />
            </div>

            <div className="space-y-3 mb-4">
                {rootReplies.map(reply => (
                    <Post
                        key={reply.id}
                        post={reply}
                        verified={verified}
                        cipherSuite={cipherSuite}
                        allPosts={allPosts}
                        onReplyTo={handleReplyTo}
                    />
                ))}
            </div>

            {!thread.locked && (
                <div
                    ref={formRef}
                    className="flat-card p-4"
                    style={{ marginTop: '16px' }}
                >
                    <div className="flex flex-wrap items-center gap-2 mb-3" style={{ justifyContent: 'space-between' }}>
                        <div className="flex items-center gap-2">
                            <MessageSquareReply size={16} style={{ color: 'var(--accent)' }} />
                            <span className="font-bold font-mono text-sm" style={{ color: 'var(--text)' }}>
                                Reply to Thread
                            </span>
                        </div>
                        {replyPrefix !== '' && (
                            <button onClick={() => setReplyPrefix('')} className="btn-v2 p-1 text-xs">
                                Clear Reply Target
                            </button>
                        )}
                    </div>
                    <PostForm
                        boardId={boardId}
                        threadId={tid}
                        initialContent={replyPrefix}
                        onSuccess={() => { handlePostSuccess(); setReplyPrefix(''); }}
                        startCollapsed={false}
                    />
                </div>
            )}

            {/* Thread Bump Payment Modal */}
            {isBumpModalOpen && (
                <div
                    style={{
                        position: 'fixed',
                        top: 0,
                        left: 0,
                        right: 0,
                        bottom: 0,
                        zIndex: 100,
                        background: 'rgba(0,0,0,0.6)',
                        backdropFilter: 'blur(4px)',
                        display: 'flex',
                        alignItems: 'center',
                        justifyContent: 'center',
                        padding: '16px',
                    }}
                    onClick={() => {
                        if (!paymentLoading) {
                            setIsBumpModalOpen(false);
                            setInvoice(null);
                        }
                    }}
                >
                    <div
                        style={{
                            background: 'var(--surface-1)',
                            border: '1px solid var(--border)',
                            borderRadius: '16px',
                            padding: '24px',
                            maxWidth: '480px',
                            width: '100%',
                            boxShadow: '0 20px 40px rgba(0,0,0,0.4)',
                        }}
                        onClick={e => e.stopPropagation()}
                    >
                        <h3 className="text-lg font-bold" style={{ color: 'var(--text)', marginBottom: '8px' }}>
                            Sticky/Bump Thread
                        </h3>
                        <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem', marginBottom: '20px' }}>
                            Amount Due: <span style={{ color: 'var(--cyan)', fontWeight: 'bold' }}>$5.00 USD</span>
                        </p>

                        {!invoice ? (
                            <div>
                                <label style={{ display: 'block', color: 'var(--text-dim)', fontSize: '0.8rem', marginBottom: '8px', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
                                    Choose Payment Cryptocurrency
                                </label>
                                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '8px', marginBottom: '20px' }}>
                                    {(['btc', 'ltc', 'xmr'] as const).map(curr => (
                                        <button
                                            key={curr}
                                            onClick={() => setPayCurrency(curr)}
                                            style={{
                                                padding: '12px 8px',
                                                borderRadius: '8px',
                                                background: payCurrency === curr ? 'var(--surface-3)' : 'var(--surface-2)',
                                                border: payCurrency === curr ? '1px solid var(--cyan)' : '1px solid var(--border)',
                                                color: payCurrency === curr ? 'var(--cyan)' : 'var(--text)',
                                                textTransform: 'uppercase',
                                                fontWeight: 'bold',
                                                cursor: 'pointer',
                                            }}
                                        >
                                            {curr}
                                        </button>
                                    ))}
                                </div>

                                {paymentError && (
                                    <p style={{ color: 'var(--red)', fontSize: '0.85rem', marginBottom: '12px' }}>{paymentError}</p>
                                )}

                                <div style={{ display: 'flex', gap: '8px', justifyContent: 'flex-end' }}>
                                    <button
                                        onClick={() => setIsBumpModalOpen(false)}
                                        className="btn-v2"
                                        style={{ padding: '10px 16px' }}
                                        disabled={paymentLoading}
                                    >
                                        Cancel
                                    </button>
                                    <button
                                        onClick={handleCreateBumpInvoice}
                                        className="btn-v2-accent"
                                        style={{ padding: '10px 16px' }}
                                        disabled={paymentLoading}
                                    >
                                        {paymentLoading ? 'Generating Invoice...' : 'Generate Invoice'}
                                    </button>
                                </div>
                            </div>
                        ) : (
                            <div>
                                <div
                                    style={{
                                        padding: '16px',
                                        borderRadius: '8px',
                                        background: 'var(--surface-2)',
                                        border: '1px dashed var(--border)',
                                        marginBottom: '20px',
                                    }}
                                >
                                    <p style={{ fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '4px' }}>
                                        Send exactly:
                                    </p>
                                    <p style={{ fontSize: '1.2rem', fontFamily: 'var(--font-mono)', fontWeight: 'bold', color: 'var(--text)', marginBottom: '12px' }}>
                                        {invoice.pay_amount} <span style={{ textTransform: 'uppercase' }}>{invoice.pay_currency}</span>
                                    </p>
                                    <p style={{ fontSize: '0.8rem', color: 'var(--text-dim)', marginBottom: '4px' }}>
                                        To Address:
                                    </p>
                                    <p
                                        style={{
                                            fontSize: '0.78rem',
                                            fontFamily: 'var(--font-mono)',
                                            background: 'var(--surface-3)',
                                            padding: '8px 10px',
                                            borderRadius: '4px',
                                            wordBreak: 'break-all',
                                            color: 'var(--cyan)',
                                        }}
                                    >
                                        {invoice.pay_address}
                                    </p>
                                </div>



                                <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
                                    <button
                                        onClick={() => {
                                            setIsBumpModalOpen(false);
                                            setInvoice(null);
                                        }}
                                        className="btn-v2"
                                        style={{ padding: '10px 16px' }}
                                    >
                                        Close
                                    </button>
                                </div>
                            </div>
                        )}
                    </div>
                </div>
            )}
        </div>
    );
}
