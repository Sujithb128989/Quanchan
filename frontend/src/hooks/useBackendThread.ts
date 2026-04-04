/**
 * useBackendThread — Fetches thread from backend with Dilithium5 verification.
 * Now includes a `refetch` function so the UI can re-fetch after posting.
 */

import { useState, useEffect, useCallback } from 'react';
import { useDilithium } from './useDilithium';
import type { Thread, Post } from '../types';

const API_BASE = '';

interface BackendThreadResponse {
  thread_block: string;
  signature: string;
  pubkey: string;
}

function mapBackendPost(raw: Record<string, unknown>, boardId: string, threadId?: number): Post {
  return {
    id: Number(raw.id) || 0,
    no: Number(raw.id) || 0,
    threadId: threadId || Number(raw.threadId) || 0,
    boardId: String(raw.boardId || boardId),
    content: String(raw.content || ''),
    encryptedContent: raw.encryptedContent ? String(raw.encryptedContent) : undefined,
    isEncrypted: Boolean(raw.isEncrypted),
    imageUrl: raw.imageUrl ? String(raw.imageUrl) : undefined,
    timestamp: raw.createdAt ? new Date(String(raw.createdAt)).getTime() : Date.now(),
    name: String(raw.name || 'Anonymous'),
    tripcode: raw.tripcode ? String(raw.tripcode) : undefined,
    sage: Boolean(raw.sage),
    replies: [],
  };
}

function mapBackendThread(raw: Record<string, unknown>, boardId: string): Thread {
  const threadId = Number(raw.id) || 0;
  const op = raw.op ? mapBackendPost(raw.op as Record<string, unknown>, boardId, threadId) : {
    id: 0, no: 0, threadId, boardId, content: '', isEncrypted: false,
    timestamp: Date.now(), name: 'Anonymous', sage: false, replies: [],
  };

  const replies = Array.isArray(raw.replies)
    ? (raw.replies as Record<string, unknown>[]).map(r => mapBackendPost(r, boardId, threadId))
    : [];

  return {
    id: threadId,
    boardId: String(raw.boardId || boardId),
    subject: String(raw.subject || 'No Subject'),
    op,
    replyCount: Number(raw.replyCount) || replies.length,
    imageCount: Number(raw.imageCount) || 0,
    lastBump: raw.lastBump ? new Date(String(raw.lastBump)).getTime() : Date.now(),
    sticky: Boolean(raw.sticky),
    locked: Boolean(raw.locked),
    archived: Boolean(raw.archived),
    replies,
  };
}

export function useBackendThread(boardId: string, threadId: number) {
  const { ready: wasmReady, verifySignature, error: wasmError } = useDilithium();
  const [thread, setThread] = useState<Thread | null>(null);
  const [verified, setVerified] = useState<boolean | null>(null);
  const [cipherSuite, setCipherSuite] = useState<string>('Unknown');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [fetchCount, setFetchCount] = useState(0);

  const refetch = useCallback(() => {
    setFetchCount(c => c + 1);
  }, []);

  useEffect(() => {
    if (!threadId || threadId <= 0) {
      setLoading(false);
      return;
    }

    let cancelled = false;

    async function fetchAndVerify() {
      try {
        setLoading(true);
        const res = await fetch(`${API_BASE}/api/threads/${threadId}`);
        if (!res.ok) {
          if (!cancelled) {
            setLoading(false);
            setError(null);
            setCipherSuite('AES-256-GCM (Local Fallback)');
          }
          return;
        }

        const cipher = res.headers.get('X-PQC-Cipher') || 'Transport disclosure unavailable; see /crypto';
        if (!cancelled) setCipherSuite(cipher);

        const data: BackendThreadResponse = await res.json();
        const threadData = JSON.parse(data.thread_block);
        const mappedThread = mapBackendThread(threadData, boardId);

        if (!cancelled) {
          setThread(mappedThread);
        }

        if (wasmReady && data.signature && data.pubkey) {
          const result = verifySignature(data.thread_block, data.signature, data.pubkey);
          if (!cancelled) {
            if (result === 0) {
              console.log(`[PQC] ✅ Thread #${threadId} signature verified (Dilithium5)`);
              setVerified(true);
            } else if (result === null) {
              setVerified(null);
            } else {
              console.error(`[PQC] ❌ Thread #${threadId} SIGNATURE INVALID`);
              setThread(null);
              setVerified(false);
            }
          }
        } else if (wasmError) {
          console.warn('[PQC] WASM load failed, cannot verify:', wasmError);
        }
      } catch (err) {
        if (!cancelled) {
          const msg = err instanceof Error ? err.message : String(err);
          console.debug('[PQC] Backend fetch failed (falling back to store):', msg);
          setError(msg);
          setCipherSuite('AES-256-GCM (Local Fallback)');
        }
      } finally {
        if (!cancelled) setLoading(false);
      }
    }

    fetchAndVerify();
    return () => { cancelled = true; };
  }, [threadId, boardId, wasmReady, wasmError, verifySignature, fetchCount]);

  return { thread, verified, cipherSuite, loading, error, refetch };
}
