import { useEffect, useState } from 'react';
import { Shield, Lock, Cpu } from 'lucide-react';
import { useIdentity } from '../hooks/useIdentity';
import { buildSignedIdentityBinding, verifyIdentityBindingRecord } from '../utils/identityBinding';

export default function CryptoStatusPage() {
    const { identity } = useIdentity();
    const [status, setStatus] = useState<any>(null);
    const [tlsProof, setTlsProof] = useState<any>(null);
    const [localBinding, setLocalBinding] = useState<{ valid: boolean; reason: string } | null>(null);

    useEffect(() => {
        fetch('/api/crypto/status')
            .then(res => res.ok ? res.json() : null)
            .then(data => setStatus(data))
            .catch(() => {});

        fetch('/api/crypto/tls-proof')
            .then(res => res.ok ? res.json() : null)
            .then(data => setTlsProof(data))
            .catch(() => {});
    }, []);

    useEffect(() => {
        if (!identity) {
            setLocalBinding(null);
            return;
        }

        buildSignedIdentityBinding(identity)
            .then(binding => verifyIdentityBindingRecord({
                pub_key_hash: identity.displayHash,
                username: identity.username || '',
                identity_public_key: identity.publicKey,
                pqc_kem_public_key: identity.pqcKemPublicKey,
                pqc_identity_public_key: identity.pqcIdentityPublicKey,
                pqc_identity_scheme: identity.pqcIdentityScheme,
                identity_binding_payload: binding.payload,
                identity_binding_signature: binding.signature,
            }))
            .then(result => setLocalBinding(result))
            .catch(() => setLocalBinding({ valid: false, reason: 'Failed to build local identity proof' }));
    }, [identity]);

    return (
        <div style={{ padding: '20px', maxWidth: '960px', margin: '0 auto 80px' }}>
            <div className="mb-6" style={{ borderBottom: '1px solid var(--border)', paddingBottom: '16px' }}>
                <h1 className="text-xl font-black font-mono" style={{ color: 'var(--text)' }}>
                    /crypto/
                </h1>
                <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>
                    Honest runtime status for signatures, messaging, storage, and transport.
                </p>
            </div>

            <div className="space-y-4">
                <div className="flat-card p-4">
                    <div className="flex items-center gap-2 mb-3">
                        <Shield size={15} style={{ color: 'var(--cyan)' }} />
                        <h2 className="text-sm font-bold font-mono" style={{ color: 'var(--text)' }}>Local Identity</h2>
                    </div>
                    <div className="space-y-2 text-sm" style={{ color: 'var(--text-muted)' }}>
                        <div className="stat-pill">Display hash basis: ML-DSA-87</div>
                        <div className="stat-pill">PQC identity: {identity?.pqcIdentityScheme || 'ML-DSA-87'}</div>
                        <div className="stat-pill">PQC DM KEM: {identity?.pqcKemScheme || 'ML-KEM-1024'}</div>
                        <div className="stat-pill">Display hash: {identity?.displayHash || 'Generating...'}</div>
                        <div className="stat-pill">
                            Local binding proof: {localBinding ? (localBinding.valid ? 'verified' : 'invalid') : 'pending'}
                        </div>
                        {localBinding && (
                            <div className="text-xs" style={{ color: localBinding.valid ? 'var(--green)' : 'var(--red)' }}>
                                {localBinding.reason}
                            </div>
                        )}
                    </div>
                </div>

                <div className="flat-card p-4">
                    <div className="flex items-center gap-2 mb-3">
                        <Cpu size={15} style={{ color: 'var(--accent)' }} />
                        <h2 className="text-sm font-bold font-mono" style={{ color: 'var(--text)' }}>Runtime Crypto Status</h2>
                    </div>
                    <pre className="text-xs font-mono" style={{ color: 'var(--text-dim)', whiteSpace: 'pre-wrap', lineHeight: 1.6 }}>
                        {status ? JSON.stringify(status, null, 2) : 'Loading status...'}
                    </pre>
                </div>

                <div className="flat-card p-4">
                    <div className="flex items-center gap-2 mb-3">
                        <Lock size={15} style={{ color: 'var(--green)' }} />
                        <h2 className="text-sm font-bold font-mono" style={{ color: 'var(--text)' }}>TLS Proof</h2>
                    </div>
                    <pre className="text-xs font-mono" style={{ color: 'var(--text-dim)', whiteSpace: 'pre-wrap', lineHeight: 1.6 }}>
                        {tlsProof ? JSON.stringify(tlsProof, null, 2) : 'Loading TLS proof...'}
                    </pre>
                </div>
            </div>
        </div>
    );
}
