import { useNavigate, Link } from 'react-router-dom';
import { useState, useRef } from 'react';
import { motion } from 'framer-motion';
import PhoenixBackground from '../components/three/PhoenixBackground';

const FEATURES = [
    {
        title: 'Hybrid Local Identity',
        description: 'No email or account signup is required. QuanChan creates a local browser identity rooted in an ML-DSA-87 public key, with extra compatibility metadata layered on top instead of defining the identity root.',
        image: '/images/feature-anonymity.png',
        accent: '#eab308',
    },
    {
        title: 'Metadata Hygiene',
        description: 'JPEG uploads are processed to remove EXIF metadata before storage. That reduces accidental leaks such as GPS coordinates, camera model details, and embedded device information.',
        image: '/images/feature-metadata.png',
        accent: '#ef4444',
    },
    {
        title: 'Signed Thread Integrity',
        description: 'Thread responses are signed with Dilithium5 and verified client-side in WASM. The transport layer now exposes honest runtime disclosure instead of blindly claiming a PQC cipher when the live TLS session cannot be proven.',
        image: '/images/feature-quantum.png',
        accent: '#22d3ee',
    },
];

function StickyCard({
    feature,
    index,
    total,
}: {
    feature: typeof FEATURES[0];
    index: number;
    total: number;
}) {
    const topOffset = 100;

    return (
        <motion.div
            style={{
                position: 'sticky',
                top: topOffset,
                zIndex: index + 1,
                marginBottom: '100px',
            }}
        >
            <div className="stack-card" style={{ background: 'var(--bg)', boxShadow: '0 20px 40px rgba(0,0,0,0.3)' }}>
                <div className="stack-card-inner">
                    <div className="stack-card-text">
                        <div
                            style={{
                                width: '32px',
                                height: '3px',
                                background: feature.accent,
                                marginBottom: '20px',
                            }}
                        />
                        <span
                            style={{
                                fontFamily: 'var(--font-mono)',
                                fontSize: '0.65rem',
                                color: 'var(--text-dim)',
                                letterSpacing: '0.2em',
                                textTransform: 'uppercase',
                            }}
                        >
                            0{index + 1} / 0{total}
                        </span>
                        <h3
                            style={{
                                fontFamily: 'var(--font-heading)',
                                fontSize: 'clamp(1.5rem, 3vw, 2.2rem)',
                                fontWeight: 700,
                                color: 'var(--text)',
                                lineHeight: 1.2,
                                marginTop: '8px',
                                marginBottom: '16px',
                            }}
                        >
                            {feature.title}
                        </h3>
                        <p
                            style={{
                                color: 'var(--text-muted)',
                                fontSize: '0.9rem',
                                lineHeight: 1.8,
                                maxWidth: '420px',
                            }}
                        >
                            {feature.description}
                        </p>
                    </div>

                    <div className="stack-card-image">
                        <img
                            src={feature.image}
                            alt={feature.title}
                            style={{
                                width: '100%',
                                height: '100%',
                                objectFit: 'cover',
                                display: 'block',
                            }}
                        />
                    </div>
                </div>
            </div>
        </motion.div>
    );
}

export default function HomePage() {
    const navigate = useNavigate();
    const [zooming, setZooming] = useState(false);
    const [heroFading, setHeroFading] = useState(false);
    const navigatedRef = useRef(false);
    const featuresRef = useRef<HTMLDivElement>(null);

    function handleEnter() {
        if (navigatedRef.current) return;
        navigatedRef.current = true;
        setHeroFading(true);
        setZooming(true);

        try {
            const ctx = new AudioContext();
            const osc = ctx.createOscillator();
            const gain = ctx.createGain();
            osc.type = 'sine';
            osc.frequency.setValueAtTime(220, ctx.currentTime);
            osc.frequency.exponentialRampToValueAtTime(880, ctx.currentTime + 1.2);
            gain.gain.setValueAtTime(0.06, ctx.currentTime);
            gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 1.2);
            osc.connect(gain).connect(ctx.destination);
            osc.start();
            osc.stop(ctx.currentTime + 1.2);
        } catch {
            // Audio unavailable
        }

        setTimeout(() => navigate('/directory'), 1200);
    }

    return (
        <div style={{ background: 'var(--bg)', minHeight: '100vh', position: 'relative', overflow: 'hidden' }}>
            <PhoenixBackground zooming={zooming} opacity={zooming ? 0.7 : 0.35} />

            <section
                className={`hero-content ${heroFading ? 'hero-fading' : ''}`}
                style={{
                    position: 'relative',
                    zIndex: 10,
                    display: 'flex',
                    flexDirection: 'column',
                    alignItems: 'center',
                    justifyContent: 'center',
                    minHeight: '100vh',
                    textAlign: 'center',
                    padding: '0 24px',
                }}
            >
                <h1
                    style={{
                        fontSize: 'clamp(3rem, 8vw, 5.5rem)',
                        fontFamily: "'Space Grotesk', var(--font-heading)",
                        fontWeight: 700,
                        color: 'var(--text)',
                        letterSpacing: '0.08em',
                        lineHeight: 1.1,
                        marginBottom: '16px',
                    }}
                >
                    QUANCHAN
                </h1>
                <p
                    style={{
                        fontFamily: "'Space Grotesk', sans-serif",
                        fontSize: '1rem',
                        letterSpacing: '0.15em',
                        color: 'var(--text-dim)',
                        textTransform: 'uppercase',
                        marginBottom: '48px',
                        maxWidth: '560px',
                        fontWeight: 500,
                    }}
                >
                    Anonymous imageboard with PQC-backed integrity checks
                </p>

                <button
                    onClick={handleEnter}
                    disabled={heroFading}
                    className="pulse-text"
                    style={{
                        background: 'transparent',
                        border: '1px solid var(--accent)',
                        borderRadius: '2px',
                        color: 'var(--accent)',
                        fontFamily: "'Space Grotesk', sans-serif",
                        fontSize: '0.9rem',
                        fontWeight: 600,
                        letterSpacing: '0.3em',
                        textTransform: 'uppercase',
                        padding: '16px 56px',
                        cursor: heroFading ? 'default' : 'pointer',
                        transition: 'background 0.2s, color 0.2s',
                    }}
                    onMouseEnter={e => {
                        if (!heroFading) {
                            e.currentTarget.style.background = 'var(--accent)';
                            e.currentTarget.style.color = 'var(--bg)';
                        }
                    }}
                    onMouseLeave={e => {
                        e.currentTarget.style.background = 'transparent';
                        e.currentTarget.style.color = 'var(--accent)';
                    }}
                >
                    Open Directory
                </button>

                <div
                    style={{
                        position: 'absolute',
                        bottom: '32px',
                        color: 'var(--text-dim)',
                        fontSize: '0.75rem',
                        fontFamily: 'var(--font-mono)',
                        letterSpacing: '0.1em',
                    }}
                >
                    Scroll for details
                </div>
            </section>

            <section
                ref={featuresRef}
                style={{
                    position: 'relative',
                    zIndex: 10,
                    minHeight: '300vh',
                    padding: '80px 24px 200px',
                    maxWidth: '1100px',
                    margin: '0 auto',
                }}
            >
                <h2
                    style={{
                        fontFamily: 'var(--font-heading)',
                        fontSize: '0.75rem',
                        fontWeight: 600,
                        letterSpacing: '0.3em',
                        textTransform: 'uppercase',
                        color: 'var(--text-dim)',
                        textAlign: 'center',
                        marginBottom: '80px',
                    }}
                >
                    Core Principles
                </h2>

                {FEATURES.map((feature, i) => (
                    <StickyCard key={i} feature={feature} index={i} total={FEATURES.length} />
                ))}
            </section>

            <footer style={{ position: 'relative', zIndex: 10, borderTop: '1px solid var(--border)', padding: '32px 24px' }}>
                <div style={{ maxWidth: '900px', margin: '0 auto', display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: '16px' }}>
                    <div style={{ display: 'flex', gap: '24px', flexWrap: 'wrap' }}>
                        {[
                            { label: 'FAQ', to: '/faq' },
                            { label: 'Rules', to: '/rules' },
                            { label: 'About', to: '/about' },
                            { label: 'Source Code', to: 'https://github.com/sujithb128989/quanchan' },
                        ].map(link =>
                            link.to.startsWith('http') ? (
                                <a
                                    key={link.label}
                                    href={link.to}
                                    target="_blank"
                                    rel="noopener noreferrer"
                                    style={{ fontFamily: 'var(--font-mono)', fontSize: '0.75rem', color: 'var(--text-dim)', textDecoration: 'none', transition: 'color 0.15s' }}
                                    onMouseEnter={e => (e.currentTarget.style.color = 'var(--text-muted)')}
                                    onMouseLeave={e => (e.currentTarget.style.color = 'var(--text-dim)')}
                                >
                                    {link.label}
                                </a>
                            ) : (
                                <Link
                                    key={link.label}
                                    to={link.to}
                                    style={{ fontFamily: 'var(--font-mono)', fontSize: '0.75rem', color: 'var(--text-dim)', textDecoration: 'none', transition: 'color 0.15s' }}
                                    onMouseEnter={e => (e.currentTarget.style.color = 'var(--text-muted)')}
                                    onMouseLeave={e => (e.currentTarget.style.color = 'var(--text-dim)')}
                                >
                                    {link.label}
                                </Link>
                            )
                        )}
                    </div>
                    <p style={{ fontFamily: 'var(--font-mono)', fontSize: '0.7rem', color: 'var(--text-dim)' }}>
                        2026 QuanChan
                    </p>
                </div>
            </footer>
        </div>
    );
}
