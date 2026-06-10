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
import { useEffect, useRef, useState, useCallback } from 'react';
import QuantumEncryptionScene from './three/QuantumEncryptionScene';

const PHASES = [
    { text: 'PREPARING ENCRYPTION...', color: '#ffd700', delay: 0 },
    { text: 'DERIVING KEY MATERIAL...', color: '#fff000', delay: 300 },
    { text: 'GENERATING NONCE...', color: '#ffee00', delay: 600 },
    { text: 'ENCRYPTING PAYLOAD...', color: '#ffaa00', delay: 900 },
    { text: 'FINALIZING OUTPUT...', color: '#00d0ff', delay: 1200 },
    { text: 'ENCRYPTION COMPLETE', color: '#00f0ff', delay: 1500 },
];

const TOTAL_DURATION = 1800;

interface QuantumModalProps {
    visible: boolean;
    onComplete: (passphrase: string) => void;
    onCancel: () => void;
}

export default function QuantumModal({ visible, onComplete, onCancel }: QuantumModalProps) {
    const [phase, setPhase] = useState(0);
    const [visibleTexts, setVisibleTexts] = useState<number[]>([]);
    const [explode, setExplode] = useState(false);
    const [whiteFlash, setWhiteFlash] = useState(false);
    const [passphrase, setPassphrase] = useState('');
    const [confirmPassphrase, setConfirmPassphrase] = useState('');
    const [error, setError] = useState('');
    const [started, setStarted] = useState(false);
    const audioCtxRef = useRef<AudioContext | null>(null);
    const startTimeRef = useRef<number>(0);
    const rafRef = useRef<number>(0);
    const completedRef = useRef(false);

    const startAudio = useCallback(() => {
        try {
            const ctx = new (window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext)();
            audioCtxRef.current = ctx;

            const oscillator = ctx.createOscillator();
            const gainNode = ctx.createGain();
            const filter = ctx.createBiquadFilter();

            filter.type = 'bandpass';
            filter.frequency.setValueAtTime(60, ctx.currentTime);
            filter.frequency.linearRampToValueAtTime(880, ctx.currentTime + 1.6);
            filter.Q.value = 2;

            oscillator.type = 'sawtooth';
            oscillator.frequency.setValueAtTime(40, ctx.currentTime);
            oscillator.frequency.linearRampToValueAtTime(440, ctx.currentTime + 1.6);
            oscillator.connect(filter);

            gainNode.gain.setValueAtTime(0, ctx.currentTime);
            gainNode.gain.linearRampToValueAtTime(0.08, ctx.currentTime + 0.2);
            gainNode.gain.linearRampToValueAtTime(0.15, ctx.currentTime + 1.4);
            gainNode.gain.linearRampToValueAtTime(0, ctx.currentTime + 1.8);
            filter.connect(gainNode);
            gainNode.connect(ctx.destination);

            oscillator.start(ctx.currentTime);
            oscillator.stop(ctx.currentTime + 1.9);
        } catch {
            // Audio not available
        }
    }, []);

    const stopAudio = useCallback(() => {
        if (audioCtxRef.current) {
            audioCtxRef.current.close();
            audioCtxRef.current = null;
        }
    }, []);

    useEffect(() => {
        if (!visible) {
            setPhase(0);
            setVisibleTexts([]);
            setExplode(false);
            setWhiteFlash(false);
            setPassphrase('');
            setConfirmPassphrase('');
            setError('');
            setStarted(false);
            completedRef.current = false;
            cancelAnimationFrame(rafRef.current);
            stopAudio();
        }
    }, [visible, stopAudio]);

    const beginAnimation = useCallback(() => {
        if (passphrase.length < 12) {
            setError('Use a passphrase with at least 12 characters.');
            return;
        }
        if (passphrase !== confirmPassphrase) {
            setError('Passphrases do not match.');
            return;
        }

        setError('');
        setStarted(true);
        startTimeRef.current = performance.now();
        completedRef.current = false;
        setPhase(0);
        setVisibleTexts([]);
        setExplode(false);
        setWhiteFlash(false);
        startAudio();

        PHASES.forEach((phaseItem, index) => {
            setTimeout(() => {
                setVisibleTexts(prev => [...prev, index]);
            }, phaseItem.delay);
        });

        setTimeout(() => setExplode(true), TOTAL_DURATION * 0.75);
        setTimeout(() => setWhiteFlash(true), TOTAL_DURATION * 0.9);

        const animate = () => {
            const elapsed = performance.now() - startTimeRef.current;
            const progress = Math.min(elapsed / TOTAL_DURATION, 1);
            setPhase(progress);

            if (progress < 1 && !completedRef.current) {
                rafRef.current = requestAnimationFrame(animate);
            } else if (!completedRef.current) {
                completedRef.current = true;
                setTimeout(() => {
                    setWhiteFlash(false);
                    onComplete(passphrase);
                }, 200);
            }
        };

        rafRef.current = requestAnimationFrame(animate);
    }, [confirmPassphrase, onComplete, passphrase, startAudio]);

    useEffect(() => {
        return () => {
            cancelAnimationFrame(rafRef.current);
            stopAudio();
        };
    }, [stopAudio]);

    if (!visible) return null;

    const currentColor =
        phase < 0.5
            ? `hsl(${16 + phase * 2 * 164}, 100%, ${50 + phase * 20}%)`
            : `hsl(${180}, ${100 - (phase - 0.5) * 200}%, ${70 + (phase - 0.5) * 60}%)`;

    return (
        <div
            className="fixed inset-0 z-50 flex items-center justify-center"
            style={{
                background: 'rgba(0,0,0,0.88)',
                backdropFilter: 'blur(8px)',
            }}
        >
            <div
                className="absolute inset-0 pointer-events-none"
                style={{
                    border: `2px solid ${currentColor}`,
                    boxShadow: `inset 0 0 60px rgba(255,215,0,0.05), 0 0 40px ${currentColor}44, 0 0 80px ${currentColor}22`,
                    borderRadius: 0,
                }}
            />

            {!started ? (
                <div className="flat-card" style={{ width: 'min(480px, calc(100vw - 32px))', padding: '24px', zIndex: 2 }}>
                    <h3 style={{ color: 'var(--text)', fontFamily: 'var(--font-heading)', fontSize: '1.2rem', marginBottom: '10px' }}>
                        Encrypt Post
                    </h3>
                    <p style={{ color: 'var(--text-dim)', fontSize: '0.9rem', lineHeight: 1.6, marginBottom: '16px' }}>
                        This post will be encrypted locally with AES-256-GCM using a passphrase-derived key. Anyone who needs to read it must know the passphrase.
                    </p>
                    <div style={{ display: 'grid', gap: '12px' }}>
                        <input
                            type="password"
                            className="v2-input"
                            style={{ padding: '10px 12px' }}
                            placeholder="Passphrase"
                            value={passphrase}
                            onChange={e => setPassphrase(e.target.value)}
                            autoFocus
                        />
                        <input
                            type="password"
                            className="v2-input"
                            style={{ padding: '10px 12px' }}
                            placeholder="Confirm passphrase"
                            value={confirmPassphrase}
                            onChange={e => setConfirmPassphrase(e.target.value)}
                        />
                    </div>
                    <p style={{ color: 'var(--text-dim)', fontSize: '0.75rem', lineHeight: 1.5, marginTop: '12px' }}>
                        Minimum 12 characters. The passphrase is never sent to the server.
                    </p>
                    {error && (
                        <p style={{ color: 'var(--red)', fontSize: '0.8rem', marginTop: '10px' }}>{error}</p>
                    )}
                    <div style={{ display: 'flex', gap: '10px', justifyContent: 'flex-end', marginTop: '18px' }}>
                        <button className="btn-v2" type="button" onClick={onCancel} style={{ padding: '10px 14px' }}>
                            Cancel
                        </button>
                        <button className="btn-v2-accent" type="button" onClick={beginAnimation} style={{ padding: '10px 14px' }}>
                            Encrypt
                        </button>
                    </div>
                </div>
            ) : (
                <>
                    {whiteFlash && (
                        <div
                            className="absolute inset-0 pointer-events-none z-10"
                            style={{
                                background: 'radial-gradient(circle at center, rgba(255,255,255,0.25), transparent 70%)',
                                animation: 'fadeIn 0.15s ease-in-out forwards',
                            }}
                        />
                    )}

                    <div className="relative w-full h-full flex flex-col items-center justify-center gap-6">
                        <div className="w-full" style={{ height: '60vh' }}>
                            <QuantumEncryptionScene phase={phase} explode={explode} />
                        </div>

                        <div className="w-64 h-0.5 bg-gray-800 rounded-full overflow-hidden">
                            <div
                                className="h-full rounded-full transition-all duration-100"
                                style={{
                                    width: `${phase * 100}%`,
                                    background: `linear-gradient(90deg, #ffd700, ${currentColor})`,
                                    boxShadow: `0 0 8px ${currentColor}`,
                                }}
                            />
                        </div>

                        <div className="flex flex-col items-center gap-2 min-h-[12rem] px-4">
                            {PHASES.map((phaseItem, index) =>
                                visibleTexts.includes(index) ? (
                                    <div
                                        key={index}
                                        className="quantum-typing quantum-text-phase text-center"
                                        style={{
                                            color: index === visibleTexts[visibleTexts.length - 1] ? phaseItem.color : `${phaseItem.color}88`,
                                            fontSize: index === visibleTexts[visibleTexts.length - 1] ? '1.3rem' : '0.95rem',
                                            fontWeight: index === visibleTexts[visibleTexts.length - 1] ? '700' : '400',
                                            textShadow:
                                                index === visibleTexts[visibleTexts.length - 1]
                                                    ? `0 0 20px ${phaseItem.color}, 0 0 40px ${phaseItem.color}88`
                                                    : 'none',
                                            transition: 'all 0.3s',
                                            animationDelay: '0s',
                                        }}
                                    >
                                        {'> '}{phaseItem.text}
                                    </div>
                                ) : null
                            )}
                        </div>

                        <div className="text-xs font-mono" style={{ color: 'rgba(0,240,255,0.5)' }}>
                            AES-256-GCM Â· PBKDF2-SHA256 Â· 600,000 iterations
                        </div>
                    </div>
                </>
            )}
        </div>
    );
}
