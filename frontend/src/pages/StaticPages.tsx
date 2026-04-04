import { Link } from 'react-router-dom';
import { ChevronRight } from 'lucide-react';

function Section({ title, children, id }: { title: string; children: React.ReactNode; id?: string }) {
    return (
        <div id={id} className="flat-card" style={{ marginBottom: '12px' }}>
            <div style={{ padding: '14px 20px', borderBottom: '1px solid var(--border)' }}>
                <h2 className="text-sm font-bold" style={{ color: '#fff', fontFamily: 'var(--font-mono)' }}>{title}</h2>
            </div>
            <div style={{ padding: '16px 20px', color: 'var(--text-muted)', fontSize: '0.85rem', lineHeight: 1.75 }}>
                {children}
            </div>
        </div>
    );
}

export function FAQPage() {
    const faqs = [
        {
            q: 'What is QuanChan?',
            a: 'QuanChan is an anonymous imageboard that experiments with post-quantum cryptography. It combines Dilithium5 thread snapshots, ML-KEM-1024 direct-message encryption, ML-DSA-87 identity binding, and client-side WASM verification.',
        },
        {
            q: 'How does the encryption work?',
            a: 'When you enable encrypted posting, your content is encrypted locally with AES-256-GCM using a passphrase-derived key via PBKDF2-SHA256 at 600,000 iterations. The passphrase is never sent to the server.',
        },
        {
            q: 'What is the Identity Vault?',
            a: 'On first visit, QuanChan generates a seed-rooted local identity wallet. The same 12-word recovery phrase deterministically recreates the same ML-DSA-87 rooted identity hash, ML-KEM-1024 messaging keys, and founder access on a new browser. Older accounts still keep an encrypted recovery bundle for migration.',
        },
        {
            q: 'Is my data private?',
            a: 'All uploads undergo automatic EXIF metadata stripping. Direct-message bodies are encrypted in the browser and stored server-side as opaque ciphertext, so inbox previews stay generic for privacy. Transport details are disclosed honestly at /crypto instead of blindly asserting a PQC TLS cipher for every live session.',
        },
        {
            q: 'What does "post-quantum" mean?',
            a: 'Post-quantum cryptography uses algorithms designed to resist quantum attacks. QuanChan uses Dilithium or ML-DSA style signatures for signed data and ML-KEM for direct-message key encapsulation.',
        },
        {
            q: 'How are signatures verified?',
            a: 'Thread and direct-message snapshots are signed server-side with Dilithium5. Your browser verifies those signatures client-side using a compiled WASM module, so integrity does not depend only on transport.',
        },
    ];

    return (
        <div style={{ padding: '24px 16px', maxWidth: '720px', margin: '0 auto' }}>
            <div style={{ marginBottom: '24px', borderBottom: '1px solid var(--border)', paddingBottom: '16px' }}>
                <h1 className="text-xl font-black font-mono" style={{ color: '#fff' }}>Frequently Asked Questions</h1>
                <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>Everything you need to know about QuanChan.</p>
            </div>

            <div className="flat-card" style={{ marginBottom: '20px' }}>
                <div style={{ padding: '12px 20px', borderBottom: '1px solid var(--border)' }}>
                    <span className="text-xs uppercase tracking-wider font-mono" style={{ color: 'var(--text-dim)' }}>Contents</span>
                </div>
                <div style={{ padding: '8px 20px' }}>
                    {faqs.map((faq, index) => (
                        <a
                            key={faq.q}
                            href={`#faq-${index}`}
                            className="flex items-center gap-2 text-sm"
                            style={{
                                padding: '6px 0',
                                color: 'var(--text-muted)',
                                textDecoration: 'none',
                                borderBottom: index < faqs.length - 1 ? '1px solid var(--border)' : 'none',
                            }}
                        >
                            <ChevronRight size={12} style={{ color: 'var(--text-dim)' }} />
                            {faq.q}
                        </a>
                    ))}
                </div>
            </div>

            {faqs.map((faq, index) => (
                <Section key={faq.q} title={faq.q} id={`faq-${index}`}>
                    <p>{faq.a}</p>
                </Section>
            ))}

            <div className="text-center mt-6">
                <Link to="/" className="text-sm font-mono" style={{ color: 'var(--text-muted)' }}>Return Home</Link>
            </div>
        </div>
    );
}

export function RulesPage() {
    const rules = [
        { title: 'No Illegal Content', desc: 'Do not post, share, or link to any content that violates applicable law. This includes but is not limited to CSAM, controlled substance distribution, and incitement to violence.' },
        { title: 'No Doxxing', desc: 'Do not share personal information such as real names, addresses, phone numbers, or workplace details of any individual without their explicit consent.' },
        { title: 'No Spam or Flooding', desc: 'Do not flood boards or threads with repetitive, low-effort, or automated content. This includes bot accounts and coordinated spam campaigns.' },
        { title: 'NSFW Content Policy', desc: 'NSFW content must be posted only on boards designated for such content. All NSFW threads must be appropriately marked. Failure to do so will result in thread deletion.' },
        { title: 'No Coordinated Harassment', desc: 'Do not organize or participate in targeted harassment campaigns against individuals or groups, whether on-platform or off-platform.' },
        { title: 'Respect Cryptographic Integrity', desc: 'Do not attempt to spoof, forge, or manipulate post signatures, identity hashes, identity proofs, or encryption metadata. Abuse of the Identity Vault system will result in hash bans.' },
    ];

    return (
        <div style={{ padding: '24px 16px', maxWidth: '720px', margin: '0 auto' }}>
            <div style={{ marginBottom: '24px', borderBottom: '1px solid var(--border)', paddingBottom: '16px' }}>
                <h1 className="text-xl font-black font-mono" style={{ color: '#fff' }}>Community Rules</h1>
                <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>By using QuanChan, you agree to these rules.</p>
            </div>

            <div className="flat-card">
                {rules.map((rule, index) => (
                    <div
                        key={rule.title}
                        style={{
                            display: 'flex',
                            gap: '16px',
                            padding: '16px 20px',
                            borderBottom: index < rules.length - 1 ? '1px solid var(--border)' : 'none',
                            alignItems: 'flex-start',
                        }}
                    >
                        <span
                            className="font-mono text-xs font-bold"
                            style={{
                                background: 'var(--surface-2)',
                                border: '1px solid var(--border)',
                                borderRadius: '2px',
                                padding: '4px 8px',
                                color: 'var(--text-dim)',
                                flexShrink: 0,
                                marginTop: '2px',
                            }}
                        >
                            {index + 1}
                        </span>
                        <div>
                            <h3 className="text-sm font-semibold mb-1" style={{ color: '#fff' }}>{rule.title}</h3>
                            <p className="text-sm" style={{ color: 'var(--text-muted)', lineHeight: 1.6 }}>{rule.desc}</p>
                        </div>
                    </div>
                ))}
            </div>

            <div style={{ marginTop: '20px', background: 'rgba(239,68,68,0.06)', border: '1px solid rgba(239,68,68,0.15)', borderRadius: '2px', padding: '14px 20px' }}>
                <p className="text-xs" style={{ color: 'var(--text-muted)', lineHeight: 1.6 }}>
                    <strong style={{ color: 'var(--red)' }}>Enforcement:</strong> Violations are handled through hash-based bans. Severe or repeated violations will result in permanent exclusion from the network. Moderation logs still need stronger authenticity guarantees and should not be treated as fully PQC.
                </p>
            </div>

            <div className="text-center mt-6">
                <Link to="/" className="text-sm font-mono" style={{ color: 'var(--text-muted)' }}>Return Home</Link>
            </div>
        </div>
    );
}
