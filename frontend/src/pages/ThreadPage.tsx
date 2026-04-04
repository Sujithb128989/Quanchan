import { useParams, Link } from 'react-router-dom';
import { useStore } from '../store/useStore';
import { useBackendThread } from '../hooks/useBackendThread';
import { useIsPhone } from '../hooks/useIsPhone';
import Post from '../components/Post';
import PostForm from '../components/PostForm';
import { ArrowLeft, MessageSquareReply } from 'lucide-react';
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

    const { thread: backendThread, verified, cipherSuite, loading, refetch } = useBackendThread(boardId, tid);
    const storeThread = useStore(s => s.threads.find(t => t.id === tid && t.boardId === boardId));
    const thread = backendThread || storeThread;

    const [replyPrefix, setReplyPrefix] = useState('');
    const formRef = useRef<HTMLDivElement>(null);

    function handleReplyTo(postNo: number) {
        setReplyPrefix(`>>${postNo}\n`);
        formRef.current?.scrollIntoView({ behavior: 'smooth', block: 'center' });
    }

    function handlePostSuccess() {
        refetch();
    }

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
            {!isPhone && (
                <div className="flex items-center gap-3 mb-4" style={{ borderBottom: '1px solid var(--border)', paddingBottom: '12px' }}>
                    <Link to={`/${boardId}`} style={{ color: 'var(--text-dim)', textDecoration: 'none' }}>
                        <ArrowLeft size={16} />
                    </Link>
                    <div>
                        <h1 className="text-lg font-bold" style={{ color: 'var(--text)' }}>{thread.subject}</h1>
                        <span className="text-xs font-mono" style={{ color: 'var(--text-dim)' }}>
                            /{boardId}/ - Thread No.{thread.id} - {thread.replyCount} replies
                        </span>
                    </div>
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
        </div>
    );
}
