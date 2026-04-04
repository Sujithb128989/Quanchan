import { ed25519 } from '@noble/curves/ed25519.js';
import { ml_dsa87 } from '@noble/post-quantum/ml-dsa.js';
import { ml_kem1024 } from '@noble/post-quantum/ml-kem.js';
import { computeDisplayHash } from './identityBinding';

const encoder = new TextEncoder();

export interface DeterministicIdentityMaterial {
    publicKey: string;
    privateKey: string;
    displayHash: string;
    pqcKemPublicKey: string;
    pqcKemSecretKey: string;
    pqcKemScheme: 'ML-KEM-1024';
    pqcIdentityPublicKey: string;
    pqcIdentitySecretKey: string;
    pqcIdentityScheme: 'ML-DSA-87';
    founderToken: string;
}

function bytesToBase64(bytes: Uint8Array): string {
    let binary = '';
    for (let i = 0; i < bytes.length; i += 1) {
        binary += String.fromCharCode(bytes[i]);
    }
    return btoa(binary);
}

function bytesToHex(bytes: Uint8Array): string {
    return Array.from(bytes).map(byte => byte.toString(16).padStart(2, '0')).join('');
}

export function normalizeSeedPhrase(value: string): string {
    return value
        .trim()
        .toLowerCase()
        .split(/\s+/)
        .filter(Boolean)
        .join(' ');
}

async function deriveBytes(label: string, phrase: string, length: number): Promise<Uint8Array> {
    const normalized = normalizeSeedPhrase(phrase);
    const out = new Uint8Array(new ArrayBuffer(length));
    let offset = 0;
    let counter = 0;

    while (offset < length) {
        const digest = await crypto.subtle.digest(
            'SHA-256',
            encoder.encode(`${label}:${counter}:${normalized}`)
        );
        const chunk = new Uint8Array(digest);
        out.set(chunk.subarray(0, Math.min(chunk.length, length - offset)), offset);
        offset += Math.min(chunk.length, length - offset);
        counter += 1;
    }

    return out;
}

export async function deriveFounderTokenFromSeedPhrase(seedPhrase: string): Promise<string> {
    const normalized = normalizeSeedPhrase(seedPhrase);
    const digest = await crypto.subtle.digest(
        'SHA-256',
        encoder.encode(`quanchan-founder-token:${normalized}`)
    );
    return bytesToHex(new Uint8Array(digest));
}

export async function createDeterministicIdentityFromSeedPhrase(seedPhrase: string): Promise<DeterministicIdentityMaterial> {
    const normalized = normalizeSeedPhrase(seedPhrase);
    const edSeed = await deriveBytes('quanchan-ed25519', normalized, 32);
    const kemSeed = await deriveBytes('quanchan-ml-kem-1024', normalized, 64);
    const identitySeed = await deriveBytes('quanchan-ml-dsa-87', normalized, 32);

    const edKeys = ed25519.keygen(edSeed);
    const kemKeys = ml_kem1024.keygen(kemSeed);
    const identityKeys = ml_dsa87.keygen(identitySeed);
    const displayHash = await computeDisplayHash(bytesToBase64(identityKeys.publicKey));

    return {
        publicKey: bytesToHex(edKeys.publicKey),
        privateKey: bytesToBase64(edKeys.secretKey),
        displayHash,
        pqcKemPublicKey: bytesToBase64(kemKeys.publicKey),
        pqcKemSecretKey: bytesToBase64(kemKeys.secretKey),
        pqcKemScheme: 'ML-KEM-1024',
        pqcIdentityPublicKey: bytesToBase64(identityKeys.publicKey),
        pqcIdentitySecretKey: bytesToBase64(identityKeys.secretKey),
        pqcIdentityScheme: 'ML-DSA-87',
        founderToken: await deriveFounderTokenFromSeedPhrase(normalized),
    };
}
