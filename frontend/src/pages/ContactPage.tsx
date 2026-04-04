import { Mail, Github, Linkedin, Shield, MessageCircle } from 'lucide-react';

const channels = [
    {
        icon: <Mail size={16} />,
        label: 'Email',
        value: 'tukimo810@gmail.com',
        href: 'mailto:tukimo810@gmail.com',
        note: 'Best for direct support, deployment issues, and security problems.',
    },
    {
        icon: <Linkedin size={16} />,
        label: 'LinkedIn',
        value: 'linkedin.com/in/sujith006',
        href: 'https://www.linkedin.com/in/sujith006/',
        note: 'Professional profile and direct outreach.',
    },
    {
        icon: <Github size={16} />,
        label: 'GitHub',
        value: 'github.com/sujithb128989',
        href: 'https://github.com/sujithb128989',
        note: 'Source, issues, and technical context.',
    },
];

export default function ContactPage() {
    return (
        <div style={{ padding: '20px', maxWidth: '860px', margin: '0 auto 120px' }}>
            <div className="flat-card" style={{ padding: '22px' }}>
                <div style={{ marginBottom: '18px' }}>
                    <h1 style={{ color: 'var(--text)', fontFamily: 'var(--font-heading)', fontSize: 'clamp(1.6rem, 4vw, 2.3rem)' }}>
                        Contact
                    </h1>
                    <p style={{ color: 'var(--text-dim)', marginTop: '8px', lineHeight: 1.6 }}>
                        Use these links when you need a direct response outside the app.
                    </p>
                </div>

                <div className="space-y-4">
                    {channels.map(channel => (
                        <a
                            key={channel.label}
                            href={channel.href}
                            target={channel.href.startsWith('mailto:') ? undefined : '_blank'}
                            rel={channel.href.startsWith('mailto:') ? undefined : 'noreferrer'}
                            className="flat-card"
                            style={{
                                display: 'block',
                                padding: '16px',
                                textDecoration: 'none',
                                background: 'rgba(255,255,255,0.025)',
                            }}
                        >
                            <div style={{ display: 'flex', alignItems: 'center', gap: '10px', color: 'var(--text)' }}>
                                {channel.icon}
                                <strong>{channel.label}</strong>
                            </div>
                            <div style={{ color: 'var(--gold)', marginTop: '8px', fontFamily: 'var(--font-mono)', fontSize: '0.92rem' }}>
                                {channel.value}
                            </div>
                            <p style={{ color: 'var(--text-dim)', marginTop: '8px', lineHeight: 1.5, fontSize: '0.85rem' }}>
                                {channel.note}
                            </p>
                        </a>
                    ))}
                </div>

                <div className="flat-card" style={{ marginTop: '18px', padding: '16px', background: 'rgba(255,255,255,0.025)' }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: '8px', color: 'var(--text)' }}>
                        <Shield size={16} />
                        <strong>Security And Abuse</strong>
                    </div>
                    <p style={{ color: 'var(--text-dim)', marginTop: '10px', lineHeight: 1.6 }}>
                        Use in-app reports for abuse, impersonation, and bad posts. Use email for anything urgent or if the site itself is broken.
                    </p>
                    <div style={{ marginTop: '12px', color: 'var(--text)', fontSize: '0.88rem', display: 'flex', alignItems: 'center', gap: '8px' }}>
                        <MessageCircle size={14} />
                        The support bot is not a human escalation path.
                    </div>
                </div>
            </div>
        </div>
    );
}
