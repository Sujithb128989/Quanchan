import { useState, useRef, useEffect, useCallback } from 'react';
import { useSearchParams, useNavigate, useParams, useLocation } from 'react-router-dom';
import { useIdentity } from '../hooks/useIdentity';
import { useIsPhone } from '../hooks/useIsPhone';
import { useStore } from '../store/useStore';
import { Send, Plus, Hash, ImagePlus, MessageCircle, ShieldCheck, ShieldAlert, Lock } from 'lucide-react';
import PhoenixBackground from '../components/three/PhoenixBackground';
import { useDilithium } from '../hooks/useDilithium';
import {
    decryptDirectMessageEnvelope,
    encryptDirectMessageForParticipants,
    isPqcEncryptedMessage,
} from '../utils/pqcMessaging';
import { subscribeToLiveEvent } from '../utils/liveEvents';

const API_BASE = '/api';
const E2EE_IMAGE_PREFIX = '__QC_E2EE_IMAGE__:';

interface Contact {
    hash: string;
    label: string;
    username?: string;
    lastMessage?: string;
    lastTimestamp?: number;
    unreadCount?: number;
}

interface Message {
    id: string;
    senderHash: string;
    text: string;
    rawText?: string;
    isPqcEncrypted?: boolean;
    imageUrl?: string;
    timestamp: number;
}

function shortHash(value: string) {
    if (!value) return '';
    if (value.length <= 12) return value;
    return `${value.slice(0, 8)}...`;
}

function labelFor(hash: string, username?: string) {
    if (hash === 'admin') return 'Support Bot';
    return username?.trim() || shortHash(hash);
}

function formatTS(ts: number) {
    return new Date(ts).toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', hour12: false });
}

function mapContact(raw: Record<string, unknown>): Contact {
    const hash = String(raw.hash || '');
    const username = raw.username ? String(raw.username) : undefined;
    const lastTimestamp = Number(raw.lastTimestamp);
    const unreadCount = Number(raw.unreadCount);
    return {
        hash,
        username,
        label: labelFor(hash, username),
        lastMessage: raw.lastMessage ? String(raw.lastMessage) : undefined,
        lastTimestamp: Number.isFinite(lastTimestamp) ? lastTimestamp : undefined,
        unreadCount: Number.isFinite(unreadCount) ? unreadCount : 0,
    };
}

function mapMessage(raw: Record<string, unknown>): Message {
    const timestamp = Number(raw.timestamp);
    const text = raw.text ? String(raw.text) : '';
    return {
        id: String(raw.id || Date.now()),
        senderHash: String(raw.senderHash || raw.sender_hash || ''),
        text,
        rawText: text,
        isPqcEncrypted: Boolean(raw.isPqcEncrypted) || isPqcEncryptedMessage(text),
        imageUrl: raw.imageUrl ? String(raw.imageUrl) : undefined,
        timestamp: Number.isFinite(timestamp) ? timestamp : Date.now(),
    };
}

function mergeContacts(existing: Contact[], incoming: Contact[], forcedHash = '') {
    const map = new Map<string, Contact>();

    for (const contact of existing) {
        map.set(contact.hash, contact);
    }

    for (const contact of incoming) {
        const previous = map.get(contact.hash);
        map.set(contact.hash, {
            ...previous,
            ...contact,
            label: labelFor(contact.hash, contact.username || previous?.username),
        });
    }

    if (forcedHash && !map.has(forcedHash)) {
        map.set(forcedHash, {
            hash: forcedHash,
            label: labelFor(forcedHash),
        });
    }

    return Array.from(map.values()).sort((a, b) => (b.lastTimestamp || 0) - (a.lastTimestamp || 0));
}

export default function DirectMessagesPage() {
    const { identity } = useIdentity();
    const { ready: dilithiumReady, verifySignature } = useDilithium();
    const respondToMessageRequest = useStore(s => (s as any).respondToMessageRequest as (actorHash: string, requesterHash: string, action: 'accept' | 'decline' | 'block') => Promise<any>);
    const navigate = useNavigate();
    const location = useLocation();
    const { hash: routeHashParam } = useParams<{ hash?: string }>();
    const [searchParams] = useSearchParams();
    const isPhone = useIsPhone();
    const initHash = searchParams.get('to') || '';
    const routeHash = routeHashParam || '';
    const myHash = identity?.displayHash || '';

    const [contacts, setContacts] = useState<Contact[]>([]);
    const [receivedRequests, setReceivedRequests] = useState<Contact[]>([]);
    const [sentRequests, setSentRequests] = useState<Contact[]>([]);
    const [activeHash, setActiveHash] = useState<string>(routeHash || initHash);
    const [messages, setMessages] = useState<Message[]>([]);
    const [inputText, setInputText] = useState('');
    const [addHash, setAddHash] = useState('');
    const [showAddInput, setShowAddInput] = useState(false);
    const [loadingConversation, setLoadingConversation] = useState(false);
    const [snapshotVerified, setSnapshotVerified] = useState<boolean | null>(null);
    const [sendError, setSendError] = useState('');
    const [peerMessagingState, setPeerMessagingState] = useState<'unknown' | 'ready' | 'missing_profile' | 'missing_pqc'>('unknown');
    const [channelStatus, setChannelStatus] = useState<'unknown' | 'accepted' | 'request_pending_incoming' | 'request_pending_outgoing' | 'request_declined' | 'blocked' | 'no_channel'>('unknown');
    const [requestActionLoading, setRequestActionLoading] = useState(false);
    const chatEndRef = useRef<HTMLDivElement>(null);
    const profileCacheRef = useRef(new Map<string, { data: any; fetchedAt: number }>());

    const selectedHash = routeHash || activeHash;
    const activeContact = contacts.find(c => c.hash === selectedHash);
    const requestContact = receivedRequests.find(c => c.hash === selectedHash) || sentRequests.find(c => c.hash === selectedHash);
    const activeLabel = activeContact?.label || requestContact?.label || labelFor(selectedHash);
    const peerMessagingReady = selectedHash === 'admin' || peerMessagingState === 'ready';
    const canSendToPeer = Boolean(myHash)
        && peerMessagingReady
        && channelStatus !== 'request_pending_incoming'
        && channelStatus !== 'request_declined'
        && channelStatus !== 'blocked';

    const fetchProfile = useCallback(async (hash: string, options?: { force?: boolean }) => {
        if (!hash || hash === 'admin') return null;
        const cached = profileCacheRef.current.get(hash);
        if (!options?.force && cached && (Date.now() - cached.fetchedAt) < 15000) {
            return cached.data;
        }

        const res = await fetch(`${API_BASE}/profile/${encodeURIComponent(hash)}`);
        if (!res.ok) return null;
        const data = await res.json();
        if (data?.pub_key_hash) {
            profileCacheRef.current.set(hash, { data, fetchedAt: Date.now() });
        } else {
            profileCacheRef.current.delete(hash);
        }
        return data;
    }, []);

    const decodeMessages = useCallback(async (incoming: Message[]) => {
        if (!identity?.pqcKemSecretKey) return incoming;

        return Promise.all(incoming.map(async message => {
            if (!message.rawText || !message.isPqcEncrypted) {
                return message;
            }

            try {
                const role = message.senderHash === myHash ? 'sender' : 'receiver';
                const decrypted = await decryptDirectMessageEnvelope(message.rawText, role, identity.pqcKemSecretKey);
                return { ...message, text: decrypted };
            } catch (err) {
                console.error('Failed to decrypt PQC DM:', err);
                return { ...message, text: '[PQC decryption failed on this browser. Restore the same recovery phrase to read this message.]' };
            }
        }));
    }, [identity?.pqcKemSecretKey, myHash]);

    useEffect(() => {
        if (isPhone && initHash && !routeHash && location.pathname === '/dm') {
            navigate(`/dm/${encodeURIComponent(initHash)}`, {
                replace: true,
                state: { dmLabel: labelFor(initHash) },
            });
            return;
        }

        const nextHash = routeHash || initHash;
        if (!nextHash) {
            if (!routeHash) {
                setActiveHash('');
                setMessages([]);
                setSnapshotVerified(null);
                setChannelStatus('unknown');
            }
            return;
        }

        setActiveHash(nextHash);
        setContacts(prev => mergeContacts(prev, [], nextHash));
    }, [initHash, isPhone, location.pathname, navigate, routeHash]);

    useEffect(() => {
        chatEndRef.current?.scrollIntoView({ behavior: 'smooth' });
    }, [messages]);

    const applyInboxPayload = useCallback((data: Record<string, unknown>, forcedHash?: string) => {
        const incoming = Array.isArray(data.conversations)
            ? data.conversations.map((entry: Record<string, unknown>) => mapContact(entry))
            : [];
        const incomingRequests = Array.isArray(data.received_requests)
            ? data.received_requests.map((entry: Record<string, unknown>) => mapContact(entry))
            : [];
        const outgoingRequests = Array.isArray(data.sent_requests)
            ? data.sent_requests.map((entry: Record<string, unknown>) => mapContact(entry))
            : [];
        const preservedHash = [forcedHash || selectedHash || initHash]
            .filter((hashValue): hashValue is string => Boolean(hashValue))
            .find((hashValue: string) => !incomingRequests.some((entry: Contact) => entry.hash === hashValue) && !outgoingRequests.some((entry: Contact) => entry.hash === hashValue)) || '';

        setContacts(prev => mergeContacts(prev, incoming, preservedHash));
        setReceivedRequests(incomingRequests);
        setSentRequests(outgoingRequests);
    }, [initHash, selectedHash]);

    const fetchInbox = useCallback(async (forcedHash?: string) => {
        if (!myHash) return;

        try {
            const res = await fetch(`${API_BASE}/messages/inbox/${encodeURIComponent(myHash)}`);
            if (!res.ok) return;

            const data = await res.json();
            applyInboxPayload(data, forcedHash);
        } catch (err) {
            console.error('Failed to fetch inbox:', err);
        }
    }, [applyInboxPayload, myHash]);

    const fetchConversation = useCallback(async (peerValue: string) => {
        if (!myHash || !peerValue) {
            setMessages([]);
            setSnapshotVerified(null);
            return;
        }

        setLoadingConversation(true);
        try {
            const query = new URLSearchParams({
                user_hash: myHash,
                peer_hash: peerValue,
            });
            const res = await fetch(`${API_BASE}/messages/snapshot?${query.toString()}`);
            if (!res.ok) return;

            const data = await res.json();
            let resolvedPeer = peerValue;
            let parsedPayload: Record<string, unknown> = data;

            if (typeof data.conversation_block === 'string') {
                if (dilithiumReady && data.signature && data.pubkey) {
                    try {
                        const verified = verifySignature(data.conversation_block, String(data.signature), String(data.pubkey));
                        setSnapshotVerified(verified === 0 ? true : verified === null ? null : false);
                    } catch (err) {
                        console.error('Failed to verify DM snapshot:', err);
                        setSnapshotVerified(false);
                    }
                } else {
                    setSnapshotVerified(null);
                }
                parsedPayload = JSON.parse(data.conversation_block);
            } else {
                setSnapshotVerified(null);
            }

            resolvedPeer = parsedPayload.peerHash ? String(parsedPayload.peerHash) : peerValue;
            setChannelStatus(String(parsedPayload.channelStatus || 'unknown') as typeof channelStatus);
            const incoming = Array.isArray(parsedPayload.messages)
                ? parsedPayload.messages.map((entry: Record<string, unknown>) => mapMessage(entry))
                : [];
            const decryptedMessages = await decodeMessages(incoming);

            if (resolvedPeer !== peerValue) {
                setActiveHash(resolvedPeer);
                if (isPhone) {
                    navigate(`/dm/${encodeURIComponent(resolvedPeer)}`, { replace: true });
                }
            }

            setMessages(decryptedMessages);
            setContacts(prev => mergeContacts(prev, [], resolvedPeer));
        } catch (err) {
            console.error('Failed to fetch conversation:', err);
        } finally {
            setLoadingConversation(false);
        }
    }, [decodeMessages, dilithiumReady, isPhone, myHash, navigate, verifySignature]);

    const applyLiveConversationPayload = useCallback(async (payload: Record<string, unknown>, peerValue: string) => {
        const resolvedPeer = payload.peerHash ? String(payload.peerHash) : peerValue;
        setChannelStatus(String(payload.channelStatus || 'unknown') as typeof channelStatus);

        const incoming = Array.isArray(payload.messages)
            ? payload.messages.map((entry: Record<string, unknown>) => mapMessage(entry))
            : [];
        const decryptedMessages = await decodeMessages(incoming);

        if (resolvedPeer !== peerValue) {
            setActiveHash(resolvedPeer);
            if (isPhone) {
                navigate(`/dm/${encodeURIComponent(resolvedPeer)}`, { replace: true });
            }
        }

        setSnapshotVerified(null);
        setMessages(decryptedMessages);
        setContacts(prev => mergeContacts(prev, [], resolvedPeer));
    }, [decodeMessages, isPhone, navigate]);

    const postDirectMessage = useCallback(async (
        senderHash: string,
        receiverHash: string,
        content: string,
        imageUrl = ''
    ) => {
        const res = await fetch(`${API_BASE}/messages`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                sender_hash: senderHash,
                receiver_hash: receiverHash,
                content,
                image_url: imageUrl,
            }),
        });

        const data = await res.json().catch(() => ({}));
        if (!res.ok) {
            throw new Error(data?.error || `Server error ${res.status}`);
        }
        return data;
    }, []);

    useEffect(() => {
        if (!myHash) return;
        fetchInbox(selectedHash || initHash);
    }, [fetchInbox, initHash, myHash, selectedHash]);

    useEffect(() => {
        if (!selectedHash || !myHash) {
            setMessages([]);
            setChannelStatus('unknown');
            return;
        }
        fetchConversation(selectedHash);
    }, [fetchConversation, myHash, selectedHash]);

    useEffect(() => {
        if (!selectedHash || selectedHash === 'admin') {
            setPeerMessagingState(selectedHash === 'admin' ? 'ready' : 'unknown');
            return;
        }

        let cancelled = false;
        fetchProfile(selectedHash, { force: true })
            .then(profile => {
                if (cancelled) return;
                if (!profile?.pub_key_hash) {
                    setPeerMessagingState('missing_profile');
                    return;
                }
                if (!profile?.pqc_kem_public_key) {
                    setPeerMessagingState('missing_pqc');
                    return;
                }
                setPeerMessagingState('ready');
            })
            .catch(() => {
                if (!cancelled) {
                    setPeerMessagingState('unknown');
                }
            });

        return () => {
            cancelled = true;
        };
    }, [fetchProfile, selectedHash]);

    useEffect(() => {
        if (!myHash) return;

        const unsubscribe = subscribeToLiveEvent<Record<string, unknown>>(
            `/api/live/messages/inbox/${encodeURIComponent(myHash)}`,
            {
                onUpdate: payload => {
                    applyInboxPayload(payload, selectedHash || initHash);
                },
                onError: () => {
                    fetchInbox(selectedHash || initHash).catch(err => console.error('Failed to refresh inbox fallback:', err));
                },
            }
        );

        if (unsubscribe) {
            return () => {
                unsubscribe();
            };
        }

        const interval = window.setInterval(() => {
            fetchInbox(selectedHash || initHash);
            if (selectedHash) {
                fetchConversation(selectedHash);
            }
        }, 4000);

        return () => window.clearInterval(interval);
    }, [applyInboxPayload, fetchConversation, fetchInbox, initHash, myHash, selectedHash]);

    useEffect(() => {
        if (!myHash || !selectedHash) return;

        const unsubscribe = subscribeToLiveEvent<{ channelStatus?: string }>(
            `/api/live/messages/conversation?user_hash=${encodeURIComponent(myHash)}&peer_hash=${encodeURIComponent(selectedHash)}`,
            {
                onUpdate: payload => {
                    applyLiveConversationPayload(payload as Record<string, unknown>, selectedHash)
                        .catch(err => {
                            console.error('Failed to apply live conversation payload:', err);
                            fetchConversation(selectedHash).catch(fetchErr => console.error('Failed to refresh live conversation:', fetchErr));
                        });
                },
                onError: () => {
                    fetchConversation(selectedHash).catch(err => console.error('Failed to refresh conversation fallback:', err));
                },
            }
        );

        return () => {
            unsubscribe?.();
        };
    }, [applyLiveConversationPayload, fetchConversation, myHash, selectedHash]);

    function openConversation(hash: string, label?: string) {
        setActiveHash(hash);
        if (isPhone) {
            navigate(`/dm/${encodeURIComponent(hash)}`, { state: { dmLabel: label || labelFor(hash) } });
        }
    }

    function addContact() {
        const value = addHash.trim();
        if (!value) {
            setAddHash('');
            setShowAddInput(false);
            return;
        }

        setContacts(prev => mergeContacts(prev, [], value));
        openConversation(value, labelFor(value));
        setAddHash('');
        setShowAddInput(false);
    }

    function renderContactButton(contact: Contact, kind: 'conversation' | 'incoming' | 'outgoing') {
        const isSelected = selectedHash === contact.hash;
        const accent = kind === 'incoming' ? 'var(--gold)' : kind === 'outgoing' ? 'var(--cyan)' : 'var(--accent)';
        return (
            <button
                key={`${kind}:${contact.hash}`}
                onClick={() => openConversation(contact.hash, contact.label)}
                style={{
                    width: '100%',
                    padding: '12px 16px',
                    cursor: 'pointer',
                    display: 'flex',
                    alignItems: 'center',
                    gap: '10px',
                    background: isSelected ? 'var(--surface-2)' : 'transparent',
                    borderLeft: isSelected ? `3px solid ${accent}` : '3px solid transparent',
                    borderBottom: '1px solid var(--border)',
                    borderTop: 'none',
                    borderRight: 'none',
                    borderBottomStyle: 'solid',
                    textAlign: 'left',
                }}
            >
                <Hash size={12} style={{ color: 'var(--text-dim)', flexShrink: 0 }} />
                <div style={{ minWidth: 0, flex: 1 }}>
                    <div className="text-xs font-bold flex items-center justify-between gap-2" style={{ color: isSelected ? 'var(--text)' : 'var(--text-muted)' }}>
                        <span className="truncate">{contact.label}</span>
                        {(contact.unreadCount || 0) > 0 && (
                            <span className="identity-badge" style={{ padding: '2px 6px', color: accent }}>
                                {contact.unreadCount}
                            </span>
                        )}
                    </div>
                    <div className="text-xs truncate" style={{ color: 'var(--text-dim)' }}>
                        {contact.lastMessage || contact.hash}
                    </div>
                </div>
            </button>
        );
    }

    async function handleRequestAction(action: 'accept' | 'decline' | 'block') {
        if (!myHash || !selectedHash || selectedHash === 'admin') return;
        try {
            setRequestActionLoading(true);
            setSendError('');
            await respondToMessageRequest(myHash, selectedHash, action);
            await Promise.all([
                fetchInbox(selectedHash),
                fetchConversation(selectedHash),
            ]);
            if (action !== 'accept') {
                setMessages([]);
            }
        } catch (err) {
            console.error('Failed to update message request:', err);
            setSendError(err instanceof Error ? err.message : 'Failed to update message request.');
        } finally {
            setRequestActionLoading(false);
        }
    }

    async function sendMessage(text?: string, imageUrl?: string) {
        const outgoingText = text ?? inputText.trim();
        if (!outgoingText && !imageUrl) return;
        if (!selectedHash || !myHash) return;

        try {
            setSendError('');
            let payloadText = outgoingText;

            if (outgoingText && selectedHash !== 'admin') {
                const peerProfile = await fetchProfile(selectedHash, { force: true });
                if (!peerProfile?.pub_key_hash) {
                    throw new Error('That profile is not published yet. Ask them to open QuanChan in a normal browser and finish identity setup before messaging.');
                }
                if (!peerProfile?.pqc_kem_public_key || !identity?.pqcKemPublicKey) {
                    throw new Error('Recipient has not published a PQC messaging key yet. Ask them to open QuanChan in a normal browser and restore the same recovery phrase before messaging.');
                }
                const envelope = await encryptDirectMessageForParticipants(
                    outgoingText,
                    identity.pqcKemPublicKey,
                    String(peerProfile.pqc_kem_public_key)
                );
                payloadText = JSON.stringify(envelope);
            }

            const created = await postDirectMessage(myHash, selectedHash, payloadText, imageUrl || '');
            const resolvedPeer = String(created.receiverHash || selectedHash);
            if (created.channelStatus) {
                setChannelStatus(String(created.channelStatus) as typeof channelStatus);
            }

            setInputText('');
            setActiveHash(resolvedPeer);
            if (isPhone) {
                navigate(`/dm/${encodeURIComponent(resolvedPeer)}`, {
                    replace: true,
                    state: { dmLabel: activeLabel || labelFor(resolvedPeer) },
                });
            }

            await Promise.all([
                fetchInbox(resolvedPeer),
                fetchConversation(resolvedPeer),
            ]);

            if (resolvedPeer === 'admin') {
                const groqKey = import.meta.env.VITE_GROQ_API_KEY;
                const appendAdminReply = async (replyText: string) => {
                    await postDirectMessage('admin', myHash, replyText, '');
                    await Promise.all([
                        fetchInbox('admin'),
                        fetchConversation('admin'),
                    ]);
                };

                if (!groqKey) {
                    window.setTimeout(() => {
                        appendAdminReply('The support bot is disabled because VITE_GROQ_API_KEY is not set in frontend/.env.')
                            .catch(err => console.error('Failed to append admin reply:', err));
                    }, 600);
                    return;
                }

                try {
                    const conversation = [
                        {
                            role: 'system',
                            content: 'You are the QuanChan support bot. Keep replies short and practical. Do not pretend to be the founder. If the user needs a human response, billing help, security help, or deployment help, direct them to tukimo810@gmail.com.',
                        },
                        ...[...messages, {
                            id: String(created.id || Date.now()),
                            senderHash: myHash,
                            text: outgoingText,
                            imageUrl,
                            timestamp: Number(created.timestamp) || Date.now(),
                        }].slice(-10).map(msg => ({
                            role: msg.senderHash === 'admin' ? 'assistant' : 'user',
                            content: msg.text || '[Image Attachment]',
                        })),
                    ];

                    const res = await fetch('https://api.groq.com/openai/v1/chat/completions', {
                        method: 'POST',
                        headers: {
                            Authorization: `Bearer ${groqKey}`,
                            'Content-Type': 'application/json',
                        },
                        body: JSON.stringify({
                            model: 'llama-3.3-70b-versatile',
                            messages: conversation,
                            max_tokens: 300,
                        }),
                    });

                    if (res.ok) {
                        const data = await res.json();
                        await appendAdminReply(data.choices?.[0]?.message?.content || '...');
                    } else {
                        await appendAdminReply('The support bot is unavailable right now.');
                    }
                } catch (err) {
                    console.error('Admin bot error:', err);
                    await appendAdminReply('The support bot could not be reached.');
                }
            }
        } catch (err) {
            console.error('Failed to send message:', err);
            setSendError(err instanceof Error ? err.message : 'Failed to send message.');
        }
    }

    function handleImageUpload() {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = 'image/*';
        input.onchange = async event => {
            const file = (event.target as HTMLInputElement).files?.[0];
            if (!file) return;

            const formData = new FormData();
            formData.append('file', file);

            try {
                if (selectedHash && selectedHash !== 'admin') {
                    if (file.size > 512 * 1024) {
                        throw new Error('E2EE image attachments are limited to 512 KB right now.');
                    }
                    const reader = new FileReader();
                    reader.onload = () => {
                        sendMessage(`${E2EE_IMAGE_PREFIX}${String(reader.result || '')}`, '')
                            .catch(err => {
                                console.error('Failed to send encrypted image:', err);
                                setSendError(err instanceof Error ? err.message : 'Failed to send encrypted image.');
                            });
                    };
                    reader.readAsDataURL(file);
                    return;
                }

                const res = await fetch('/api/upload', { method: 'POST', body: formData });
                if (res.ok) {
                    const data = await res.json();
                    const url = data.url || data.imageUrl || '';
                    if (url) {
                        await sendMessage('', url);
                    }
                }
            } catch {
                if (selectedHash && selectedHash !== 'admin') {
                    setSendError('Encrypted image preparation failed. Private attachments were not uploaded.');
                    return;
                }
                const reader = new FileReader();
                reader.onload = () => {
                    sendMessage('', reader.result as string).catch(err => console.error('Failed to send local image:', err));
                };
                reader.readAsDataURL(file);
            }
        };
        input.click();
    }

    const showSidebar = !isPhone || !routeHash;
    const showChat = !isPhone || Boolean(routeHash);

    return (
        <div className="flex h-full overflow-hidden relative" style={{ background: 'var(--bg)', flex: 1 }}>
            <div className="absolute inset-0 pointer-events-none" style={{ zIndex: 0 }}>
                <PhoenixBackground zooming={false} opacity={0.4} />
            </div>

            <div
                className="border-r border-[var(--border)] relative z-10 shadow-2xl"
                style={{
                    background: 'var(--surface)',
                    width: isPhone ? '100%' : '320px',
                    minWidth: isPhone ? '100%' : '320px',
                    display: showSidebar ? 'flex' : 'none',
                    flexDirection: 'column',
                }}
            >
                <div style={{ padding: '16px', borderBottom: '1px solid var(--border)', display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
                    {!isPhone ? (
                        <h2 className="text-lg font-bold flex items-center gap-2" style={{ fontFamily: 'var(--font-heading)', color: 'var(--text)' }}>
                            <MessageCircle size={18} className="text-amber-500" /> Messages
                        </h2>
                    ) : <div />}
                    <button onClick={() => setShowAddInput(v => !v)} className="btn-v2 p-1" title="Add contact">
                        <Plus size={14} />
                    </button>
                </div>

                {showAddInput && (
                    <div style={{ padding: '8px 12px', borderBottom: '1px solid var(--border)', display: 'flex', gap: '6px' }}>
                        <input
                            className="v2-input flex-1 text-xs"
                            style={{ padding: '8px 10px' }}
                            placeholder="Enter hash or username..."
                            value={addHash}
                            onChange={e => setAddHash(e.target.value)}
                            onKeyDown={e => e.key === 'Enter' && addContact()}
                            autoFocus
                        />
                        <button onClick={addContact} className="btn-v2-accent p-1" style={{ borderRadius: '6px' }}>
                            <Send size={12} />
                        </button>
                    </div>
                )}

                <div style={{ flex: 1, overflowY: 'auto' }}>
                    {receivedRequests.length > 0 && (
                        <>
                            <div style={{ padding: '10px 16px 6px', color: 'var(--gold)', fontSize: '0.68rem', fontFamily: 'var(--font-mono)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>
                                Incoming Requests
                            </div>
                            {receivedRequests.map(contact => renderContactButton(contact, 'incoming'))}
                        </>
                    )}

                    {sentRequests.length > 0 && (
                        <>
                            <div style={{ padding: '10px 16px 6px', color: 'var(--cyan)', fontSize: '0.68rem', fontFamily: 'var(--font-mono)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>
                                Sent Requests
                            </div>
                            {sentRequests.map(contact => renderContactButton(contact, 'outgoing'))}
                        </>
                    )}

                    {contacts.length === 0 ? (
                        <div style={{ padding: '32px 16px', textAlign: 'center', color: 'var(--text-dim)', fontSize: '0.8rem' }}>
                            No accepted conversations yet.
                        </div>
                    ) : (
                        <>
                            <div style={{ padding: '10px 16px 6px', color: 'var(--text-dim)', fontSize: '0.68rem', fontFamily: 'var(--font-mono)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>
                                Conversations
                            </div>
                            {contacts.map(contact => renderContactButton(contact, 'conversation'))}
                        </>
                    )}
                </div>

                <div style={{ padding: '10px 16px', borderTop: '1px solid var(--border)', fontSize: '0.7rem', color: 'var(--text-dim)' }}>
                    You: <span style={{ color: 'var(--accent)', fontWeight: 'bold' }}>{identity?.username || shortHash(myHash) || 'Generating identity...'}</span>
                </div>
            </div>

            <div
                style={{
                    flex: 1,
                    minWidth: 0,
                    display: showChat ? 'flex' : 'none',
                    flexDirection: 'column',
                    background: 'var(--bg)',
                    position: 'relative',
                }}
            >
                <div style={{
                    position: 'absolute',
                    inset: 0,
                    zIndex: 0,
                    backgroundImage: 'url(/phoenix.png)',
                    backgroundPosition: isPhone ? 'center 38%' : 'center',
                    backgroundRepeat: 'no-repeat',
                    backgroundSize: isPhone ? 'min(72vw, 300px)' : '400px',
                    opacity: isPhone ? 0.13 : 0.1,
                    pointerEvents: 'none',
                }} />

                {!selectedHash ? (
                    <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', position: 'relative', zIndex: 1, padding: '16px' }}>
                        <div className="stack-card p-12 rounded-xl" style={{ textAlign: 'center', color: 'var(--text-dim)', maxWidth: '400px' }}>
                            <div className="mx-auto w-16 h-16 rounded-full flex items-center justify-center mb-6" style={{ background: 'var(--surface-2)' }}>
                                <MessageCircle size={28} className="text-amber-500" />
                            </div>
                            <p className="text-xl font-bold text-white mb-2" style={{ fontFamily: 'var(--font-heading)' }}>Select a conversation</p>
                            <p className="text-sm" style={{ lineHeight: 1.6 }}>Messages now sync through the backend, so both people can actually see the same thread.</p>
                        </div>
                    </div>
                ) : (
                    <>
                        <div className="dm-header" style={{ padding: isPhone ? '10px 12px' : '12px 16px', borderBottom: '1px solid var(--border)', background: 'var(--surface)', position: 'relative', zIndex: 1, display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: '12px' }}>
                            <div className="dm-header__meta flex items-center gap-2" style={{ minWidth: 0 }}>
                                <div style={{ minWidth: 0 }}>
                                    {!isPhone && (
                                        <>
                                            <div className="text-sm font-bold truncate" style={{ fontFamily: 'var(--font-heading)', color: 'var(--text)' }}>
                                                {activeLabel}
                                            </div>
                                            {selectedHash !== 'admin' && (
                                                <div className="text-xs font-mono truncate" style={{ color: 'var(--text-dim)' }}>
                                                    {selectedHash}
                                                </div>
                                            )}
                                        </>
                                    )}
                                    <div className="dm-header__status text-xs font-mono flex items-center gap-1" style={{ color: snapshotVerified === true ? 'var(--green)' : snapshotVerified === false ? 'var(--red)' : 'var(--text-dim)', marginTop: !isPhone && selectedHash !== 'admin' ? '4px' : '0' }}>
                                        {snapshotVerified === true ? <ShieldCheck size={11} /> : snapshotVerified === false ? <ShieldAlert size={11} /> : <Lock size={11} />}
                                        {snapshotVerified === true ? 'Dilithium5 snapshot verified' : snapshotVerified === false ? 'Snapshot signature invalid' : 'Snapshot signature pending'}
                                    </div>
                                    {selectedHash !== 'admin' && (
                                        <div className="dm-header__status text-xs font-mono flex items-center gap-1" style={{ color: peerMessagingState === 'ready' ? 'var(--green)' : peerMessagingState === 'unknown' ? 'var(--text-dim)' : 'var(--gold)', marginTop: '4px' }}>
                                            <Lock size={11} />
                                            {peerMessagingState === 'ready' && 'Recipient PQC messaging key is published'}
                                            {peerMessagingState === 'missing_profile' && 'Recipient has not published a profile on this browser yet'}
                                            {peerMessagingState === 'missing_pqc' && 'Recipient profile exists, but PQC messaging key is still missing'}
                                            {peerMessagingState === 'unknown' && 'Checking recipient PQC messaging status...'}
                                        </div>
                                    )}
                                    {selectedHash !== 'admin' && (
                                        <div className="dm-header__status text-xs font-mono" style={{ color: channelStatus === 'accepted' ? 'var(--green)' : channelStatus === 'blocked' ? 'var(--red)' : channelStatus === 'request_pending_incoming' ? 'var(--gold)' : channelStatus === 'request_pending_outgoing' ? 'var(--cyan)' : 'var(--text-dim)', marginTop: '4px' }}>
                                            {channelStatus === 'accepted' && 'Accepted conversation'}
                                            {channelStatus === 'request_pending_incoming' && 'Incoming message request waiting for your decision'}
                                            {channelStatus === 'request_pending_outgoing' && 'Waiting for the other person to accept your request'}
                                            {channelStatus === 'request_declined' && 'Previous message request was declined'}
                                            {channelStatus === 'blocked' && 'Conversation is blocked'}
                                            {(channelStatus === 'no_channel' || channelStatus === 'unknown') && 'No accepted channel yet. Your next message will open a request.'}
                                        </div>
                                    )}
                                </div>
                            </div>

                            <div className="dm-header__actions flex items-center gap-2" style={{ flexShrink: 0 }}>
                                {selectedHash !== 'admin' && channelStatus === 'request_pending_incoming' && (
                                    <>
                                        <button onClick={() => handleRequestAction('accept')} className="dm-header__button btn-v2-accent text-xs" style={{ padding: '4px 10px' }} disabled={requestActionLoading}>
                                            {requestActionLoading ? 'Working...' : 'Accept'}
                                        </button>
                                        <button onClick={() => handleRequestAction('decline')} className="dm-header__button btn-v2 text-xs" style={{ padding: '4px 10px' }} disabled={requestActionLoading}>
                                            Decline
                                        </button>
                                    </>
                                )}
                                {selectedHash !== 'admin' && (
                                    <a href={`/u/${selectedHash}`} target="_blank" className="dm-header__button btn-v2 text-xs" style={{ padding: '4px 10px', textDecoration: 'none' }}>
                                        View Profile
                                    </a>
                                )}
                            </div>
                        </div>

                        <div style={{ flex: 1, overflowY: 'auto', padding: isPhone ? '12px' : '16px', position: 'relative', zIndex: 1 }}>
                            {loadingConversation && messages.length === 0 ? (
                                <div style={{ textAlign: 'center', padding: '48px', color: 'var(--text-dim)', fontSize: '0.85rem' }}>
                                    Loading conversation...
                                </div>
                            ) : null}

                            {!loadingConversation && messages.length === 0 && (
                                <div style={{ textAlign: 'center', padding: '48px', color: 'var(--text-dim)', fontSize: '0.85rem' }}>
                                    Start a secure conversation.
                                </div>
                            )}

                            {messages.map(msg => {
                                const isMe = msg.senderHash === myHash;
                                const inlineImage = msg.text.startsWith(E2EE_IMAGE_PREFIX) ? msg.text.slice(E2EE_IMAGE_PREFIX.length) : '';
                                return (
                                    <div key={msg.id} style={{ display: 'flex', marginBottom: '12px', justifyContent: isMe ? 'flex-end' : 'flex-start' }}>
                                        <div className={`dm-bubble ${isMe ? 'dm-bubble-me' : 'dm-bubble-them'}`}>
                                            {msg.imageUrl && (
                                                <img
                                                    src={msg.imageUrl}
                                                    alt=""
                                                    style={{ maxWidth: isPhone ? '180px' : '240px', borderRadius: '12px', marginBottom: msg.text ? '6px' : 0, display: 'block' }}
                                                />
                                            )}
                                            {inlineImage && (
                                                <img
                                                    src={inlineImage}
                                                    alt="Encrypted attachment"
                                                    style={{ maxWidth: isPhone ? '180px' : '240px', borderRadius: '12px', display: 'block' }}
                                                />
                                            )}
                                            {msg.text && !inlineImage && <p className="dm-message-text" style={{ fontSize: '0.875rem', lineHeight: 1.5 }}>{msg.text}</p>}
                                            <span className="dm-message-meta" style={{ fontSize: '0.65rem', fontFamily: 'var(--font-mono)', opacity: 0.6, marginTop: '4px', display: 'block' }}>
                                                {formatTS(msg.timestamp)}{msg.isPqcEncrypted ? ' · PQC E2EE' : ''}
                                            </span>
                                        </div>
                                    </div>
                                );
                            })}
                            <div ref={chatEndRef} />
                        </div>

                        <div className="dm-composer" style={{
                            padding: isPhone ? '10px 12px calc(10px + env(safe-area-inset-bottom))' : '12px 16px',
                            borderTop: '1px solid var(--border)',
                            background: 'var(--surface)',
                            display: 'flex',
                            gap: '8px',
                            position: 'relative',
                            zIndex: 1,
                            alignItems: 'center',
                            flexWrap: 'wrap',
                        }}>
                            {sendError && (
                                <div className="dm-composer__error w-full text-xs" style={{ color: 'var(--red)', marginBottom: '4px' }}>
                                    {sendError}
                                </div>
                            )}
                            <button onClick={handleImageUpload} className="dm-composer__icon btn-v2" style={{ padding: '8px', borderRadius: '50%', flexShrink: 0 }} title="Send image" disabled={!canSendToPeer}>
                                <ImagePlus size={16} />
                            </button>
                            <input
                                className="dm-composer__input v2-input flex-1 text-sm disabled:opacity-50 disabled:cursor-not-allowed"
                                style={{ padding: '10px 14px', minWidth: 0 }}
                                placeholder={
                                    !myHash
                                        ? 'Generating your identity...'
                                        : !peerMessagingReady
                                            ? 'Recipient must publish their PQC identity first...'
                                            : channelStatus === 'request_pending_incoming'
                                                ? 'Accept or decline this request first...'
                                                : channelStatus === 'request_declined'
                                                    ? 'This request was declined. Start from the profile page if needed...'
                                                    : channelStatus === 'blocked'
                                                        ? 'This conversation is blocked...'
                                                        : channelStatus === 'request_pending_outgoing'
                                                            ? 'Request pending. You can still add more context...'
                                                            : 'Type a message...'
                                }
                                value={inputText}
                                onChange={e => setInputText(e.target.value)}
                                onKeyDown={e => e.key === 'Enter' && sendMessage()}
                                disabled={!canSendToPeer}
                            />
                            <button onClick={() => sendMessage()} className="dm-composer__send btn-v2-accent" style={{ padding: '8px 14px', borderRadius: '20px', flexShrink: 0 }} disabled={!canSendToPeer}>
                                <Send size={16} />
                            </button>
                        </div>
                    </>
                )}
            </div>
        </div>
    );
}
