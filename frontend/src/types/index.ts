// TypeScript types for QuanChan

export interface Board {
    id: string;
    name: string;
    description: string;
    icon: string;
    threadCount: number;
    postCount: number;
    nsfw: boolean;
}

export interface Post {
    id: number;
    no: number; // post number on board
    threadId: number;
    boardId: string;
    content: string;
    encryptedContent?: string; // AES-GCM encrypted
    isEncrypted: boolean;
    imageUrl?: string;
    timestamp: number;
    name: string;
    tripcode?: string;
    sage: boolean;
    replies: number[];
    subscriptionTier?: string;
    customBadge?: string;
}

export interface Thread {
    id: number;
    boardId: string;
    subject: string;
    op: Post; // original post
    replyCount: number;
    imageCount: number;
    lastBump: number;
    sticky: boolean;
    locked: boolean;
    archived: boolean;
    replies: Post[];
}

export interface EncryptionState {
    enabled: boolean;
}

export type LoadState = 'idle' | 'loading' | 'ready' | 'error';

export interface AppState {
    boards: Board[];
    threads: Thread[];
    boardsState: LoadState;
    boardsError: string;
    threadsState: LoadState;
    threadsError: string;
    nextPostNo: number;
    nextThreadId: number;
    encryptionState: EncryptionState;
    quantumModalVisible: boolean;
    pendingPost: {
        boardId: string;
        threadId: number | null;
        content: string;
        subject: string;
        imageUrl: string;
        name: string;
        authorHash: string;
    } | null;

    isPhoenixFiring: boolean;

    // Actions
    setPhoenixFiring: (v: boolean) => void;
    setEncryptionState: (state: Partial<EncryptionState>) => void;
    setQuantumModalVisible: (v: boolean) => void;
    setPendingPost: (p: AppState['pendingPost']) => void;
    createThread: (boardId: string, subject: string, content: string, imageUrl: string, name: string, authorHash?: string, encryptedContent?: string) => Promise<Thread>;
    createReply: (boardId: string, threadId: number, content: string, imageUrl: string, name: string, authorHash?: string, encryptedContent?: string) => Promise<Post>;
    archiveThread: (threadId: number) => void;
    giftUser?: (actorHash: string, targetHash: string, giftType: 'tag' | 'subscription', giftValue: string, durationDays: number) => Promise<any>;
}
