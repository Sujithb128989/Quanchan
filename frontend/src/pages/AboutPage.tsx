import { Link } from 'react-router-dom';
import { Shield, Lock, Eye, Cpu, Github, Linkedin, Mail, UserRound } from 'lucide-react';

const TECH_ROWS = [
    { icon: <Shield size={16} style={{ color: 'var(--cyan)' }} />, label: 'Integrity', value: 'Dilithium5 snapshots with ML-DSA-87 rooted identity binding' },
    { icon: <Lock size={16} style={{ color: 'var(--green)' }} />, label: 'Messaging', value: 'ML-KEM-1024 + AES-256-GCM direct messages encrypted in the browser and stored as opaque ciphertext' },
    { icon: <Eye size={16} style={{ color: 'var(--red)' }} />, label: 'Privacy', value: 'EXIF stripping, local identity vaults, recovery-driven restore flow, and generic server-side DM previews' },
    { icon: <Cpu size={16} style={{ color: 'var(--gold)' }} />, label: 'Transport', value: 'Runtime crypto disclosure instead of fake static "quantum-safe" claims' },
];

export default function AboutPage() {
    return (
        <div style={{ padding: '24px 16px', maxWidth: '820px', margin: '0 auto' }}>
            <div style={{ marginBottom: '24px', borderBottom: '1px solid var(--border)', paddingBottom: '16px' }}>
                <h1 className="text-xl font-black font-mono" style={{ color: '#fff' }}>About QuanChan</h1>
                <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>
                    Anonymous posting with honest cryptography and fewer gimmicks.
                </p>
            </div>

            <div className="flat-card" style={{ marginBottom: '12px' }}>
                <div style={{ padding: '14px 20px', borderBottom: '1px solid var(--border)' }}>
                    <h2 className="text-sm font-bold font-mono" style={{ color: '#fff' }}>What QuanChan Is</h2>
                </div>
                <div style={{ padding: '16px 20px', color: 'var(--text-muted)', fontSize: '0.9rem', lineHeight: 1.8 }}>
                    <p>
                        QuanChan is a PQC-focused anonymous imageboard. The goal is simple: local identity, signed content,
                        encrypted direct messages that stay opaque to the server, and transport disclosures that reflect what the
                        server actually observed.
                    </p>
                    <p style={{ marginTop: '12px' }}>
                        Identity recovery, moderation, and naming rules should behave like real product features, not demo shortcuts.
                    </p>
                </div>
            </div>

            <div className="flat-card" style={{ marginBottom: '12px' }}>
                <div style={{ padding: '14px 20px', borderBottom: '1px solid var(--border)' }}>
                    <h2 className="text-sm font-bold font-mono" style={{ color: '#fff' }}>Technology</h2>
                </div>
                <div style={{ padding: '16px 20px' }}>
                    {TECH_ROWS.map(row => (
                        <div
                            key={row.label}
                            className="flex items-center gap-3"
                            style={{ padding: '10px 0', borderBottom: '1px solid var(--border)' }}
                        >
                            {row.icon}
                            <span className="text-xs font-mono" style={{ color: 'var(--text-dim)', width: '140px', flexShrink: 0 }}>{row.label}</span>
                            <span className="text-sm" style={{ color: 'var(--text)' }}>{row.value}</span>
                        </div>
                    ))}
                </div>
            </div>

            <div className="flat-card" style={{ marginBottom: '12px' }}>
                <div style={{ padding: '14px 20px', borderBottom: '1px solid var(--border)' }}>
                    <h2 className="text-sm font-bold font-mono" style={{ color: '#fff' }}>Builder</h2>
                </div>
                <div style={{ padding: '18px 20px', display: 'grid', gap: '18px', gridTemplateColumns: 'minmax(0, 1fr)' }}>
                    <div style={{ display: 'flex', gap: '16px', alignItems: 'flex-start', flexWrap: 'wrap' }}>
                        <div
                            style={{
                                width: '72px',
                                height: '72px',
                                borderRadius: '18px',
                                display: 'flex',
                                alignItems: 'center',
                                justifyContent: 'center',
                                background: 'var(--surface-2)',
                                border: '1px solid var(--border)',
                                flexShrink: 0,
                            }}
                        >
                            <UserRound size={30} style={{ color: 'var(--gold)' }} />
                        </div>
                        <div style={{ minWidth: 0, flex: 1 }}>
                            <h3 style={{ color: '#fff', fontSize: '1.15rem', fontFamily: 'var(--font-heading)', marginBottom: '8px' }}>Sujith B</h3>
                            <p style={{ color: 'var(--text-muted)', fontSize: '0.9rem', lineHeight: 1.75 }}>
                                Founder and builder of QuanChan. The contact links are here so users can reach a real person
                                instead of guessing whether a thread or bot message will be seen.
                            </p>
                        </div>
                    </div>

                    <div style={{ display: 'flex', gap: '10px', flexWrap: 'wrap' }}>
                        <a
                            href="https://github.com/sujithb128989"
                            target="_blank"
                            rel="noopener noreferrer"
                            className="btn-v2 flex items-center gap-2 text-sm"
                            style={{ padding: '8px 14px', textDecoration: 'none', fontFamily: 'var(--font-mono)' }}
                        >
                            <Github size={14} /> GitHub
                        </a>
                        <a
                            href="https://www.linkedin.com/in/sujith006/"
                            target="_blank"
                            rel="noopener noreferrer"
                            className="btn-v2 flex items-center gap-2 text-sm"
                            style={{ padding: '8px 14px', textDecoration: 'none', fontFamily: 'var(--font-mono)' }}
                        >
                            <Linkedin size={14} /> LinkedIn
                        </a>
                        <a
                            href="mailto:tukimo810@gmail.com"
                            className="btn-v2 flex items-center gap-2 text-sm"
                            style={{ padding: '8px 14px', textDecoration: 'none', fontFamily: 'var(--font-mono)' }}
                        >
                            <Mail size={14} /> Contact
                        </a>
                    </div>
                </div>
            </div>

            <div className="flat-card" style={{ marginBottom: '16px' }}>
                <div style={{ padding: '14px 20px', borderBottom: '1px solid var(--border)' }}>
                    <h2 className="text-sm font-bold font-mono" style={{ color: '#fff' }}>Stack</h2>
                </div>
                <div style={{ padding: '16px 20px', display: 'flex', gap: '8px', flexWrap: 'wrap' }}>
                    {['C++17', 'liboqs', 'PostgreSQL', 'React', 'TypeScript', 'Three.js', 'WASM', 'Docker', 'gRPC'].map(tag => (
                        <span
                            key={tag}
                            style={{
                                fontFamily: 'var(--font-mono)',
                                fontSize: '0.7rem',
                                padding: '4px 10px',
                                border: '1px solid var(--border)',
                                borderRadius: '2px',
                                color: 'var(--text-dim)',
                            }}
                        >
                            {tag}
                        </span>
                    ))}
                </div>
            </div>

            <div className="flex gap-4 mt-6 flex-wrap">
                <a
                    href="https://github.com/sujithb128989/quanchan"
                    target="_blank"
                    rel="noopener noreferrer"
                    className="btn-v2 flex items-center gap-2 text-sm"
                    style={{ padding: '8px 16px', textDecoration: 'none', fontFamily: 'var(--font-mono)' }}
                >
                    <Github size={14} /> Source Code
                </a>
                <Link to="/" className="btn-v2 text-sm" style={{ padding: '8px 16px', textDecoration: 'none', fontFamily: 'var(--font-mono)' }}>
                    Return Home
                </Link>
            </div>
        </div>
    );
}
