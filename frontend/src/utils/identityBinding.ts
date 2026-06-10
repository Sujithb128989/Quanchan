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
import { ml_dsa87 } from '@noble/post-quantum/ml-dsa.js';

const encoder = new TextEncoder();

export interface IdentityBindingPayload {
    version: 1;
    binding: 'ml-dsa87-root';
    displayHash: string;
    identityPublicKey: string;
    pqcKemPublicKey: string;
    pqcIdentityPublicKey: string;
    username: string;
    issuedAt: string;
}

export interface IdentityBindingRecordLike {
    pub_key_hash?: string;
    displayHash?: string;
    username?: string;
    identity_public_key?: string;
    identityPublicKey?: string;
    pqc_kem_public_key?: string;
    pqcKemPublicKey?: string;
    pqc_identity_public_key?: string;
    pqcIdentityPublicKey?: string;
    pqc_identity_scheme?: string;
    identity_binding_payload?: string;
    identityBindingPayload?: string;
    identity_binding_signature?: string;
    identityBindingSignature?: string;
}

function bytesToBase64(bytes: Uint8Array): string {
    let binary = '';
    for (let i = 0; i < bytes.length; i += 1) {
        binary += String.fromCharCode(bytes[i]);
    }
    return btoa(binary);
}

function base64ToBytes(value: string): Uint8Array {
    const binary = atob(value);
    const out = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i += 1) {
        out[i] = binary.charCodeAt(i);
    }
    return out;
}

function bytesToHex(bytes: Uint8Array): string {
    return Array.from(bytes).map(byte => byte.toString(16).padStart(2, '0')).join('');
}

function normalizeUsername(username?: string): string {
    return username?.trim() || '';
}

export async function computeDisplayHash(pqcIdentityPublicKey: string): Promise<string> {
    const digest = await crypto.subtle.digest('SHA-256', encoder.encode(pqcIdentityPublicKey));
    return bytesToHex(new Uint8Array(digest)).slice(0, 16);
}

export function generatePqcIdentityKeypair() {
    const keys = ml_dsa87.keygen();
    return {
        publicKey: bytesToBase64(keys.publicKey),
        secretKey: bytesToBase64(keys.secretKey),
        scheme: 'ML-DSA-87' as const,
    };
}

export function createIdentityBindingPayload(input: {
    displayHash: string;
    identityPublicKey: string;
    pqcKemPublicKey: string;
    pqcIdentityPublicKey: string;
    username?: string;
    issuedAt?: string;
}): string {
    const payload: IdentityBindingPayload = {
        version: 1,
        binding: 'ml-dsa87-root',
        displayHash: input.displayHash,
        identityPublicKey: input.identityPublicKey,
        pqcKemPublicKey: input.pqcKemPublicKey,
        pqcIdentityPublicKey: input.pqcIdentityPublicKey,
        username: normalizeUsername(input.username),
        issuedAt: input.issuedAt || new Date().toISOString(),
    };
    return JSON.stringify(payload);
}

export function signIdentityBindingPayload(payload: string, pqcIdentitySecretKey: string): string {
    const signature = ml_dsa87.sign(encoder.encode(payload), base64ToBytes(pqcIdentitySecretKey));
    return bytesToBase64(signature);
}

export async function buildSignedIdentityBinding(identity: {
    publicKey: string;
    pqcKemPublicKey: string;
    pqcIdentityPublicKey: string;
    pqcIdentitySecretKey: string;
    username?: string;
}) {
    const displayHash = await computeDisplayHash(identity.pqcIdentityPublicKey);
    const payloadObject: IdentityBindingPayload = {
        version: 1,
        binding: 'ml-dsa87-root',
        displayHash,
        identityPublicKey: identity.publicKey,
        pqcKemPublicKey: identity.pqcKemPublicKey,
        pqcIdentityPublicKey: identity.pqcIdentityPublicKey,
        username: normalizeUsername(identity.username),
        issuedAt: new Date().toISOString(),
    };
    const payload = JSON.stringify(payloadObject);

    return {
        displayHash,
        payload,
        signature: signIdentityBindingPayload(payload, identity.pqcIdentitySecretKey),
        scheme: 'ML-DSA-87' as const,
    };
}

export async function verifyIdentityBindingRecord(record: IdentityBindingRecordLike): Promise<{ valid: boolean; reason: string }> {
    const payloadString = record.identity_binding_payload || record.identityBindingPayload || '';
    const signature = record.identity_binding_signature || record.identityBindingSignature || '';
    const pqcIdentityPublicKey = record.pqc_identity_public_key || record.pqcIdentityPublicKey || '';

    if (!payloadString || !signature || !pqcIdentityPublicKey) {
        return { valid: false, reason: 'Missing PQC identity proof fields' };
    }

    let payload: IdentityBindingPayload;
    try {
        payload = JSON.parse(payloadString) as IdentityBindingPayload;
    } catch {
        return { valid: false, reason: 'Identity binding payload is not valid JSON' };
    }

    if (payload.version !== 1 || payload.binding !== 'ml-dsa87-root') {
        return { valid: false, reason: 'Unsupported identity binding format' };
    }

    const expectedDisplayHash = record.pub_key_hash || record.displayHash || '';
    const expectedIdentityPublicKey = record.identity_public_key || record.identityPublicKey || '';
    const expectedPqcKemPublicKey = record.pqc_kem_public_key || record.pqcKemPublicKey || '';
    const expectedUsername = normalizeUsername(record.username);

    if (expectedDisplayHash && payload.displayHash !== expectedDisplayHash) {
        return { valid: false, reason: 'Binding payload display hash does not match profile hash' };
    }
    if (expectedIdentityPublicKey && payload.identityPublicKey !== expectedIdentityPublicKey) {
        return { valid: false, reason: 'Binding payload identity key does not match profile key' };
    }
    if (expectedPqcKemPublicKey && payload.pqcKemPublicKey !== expectedPqcKemPublicKey) {
        return { valid: false, reason: 'Binding payload KEM key does not match profile key' };
    }
    if (expectedUsername && payload.username !== expectedUsername) {
        return { valid: false, reason: 'Binding payload username does not match profile username' };
    }
    if (payload.pqcIdentityPublicKey !== pqcIdentityPublicKey) {
        return { valid: false, reason: 'Binding payload PQC identity key does not match profile key' };
    }

    const derivedDisplayHash = await computeDisplayHash(payload.pqcIdentityPublicKey);
    if (derivedDisplayHash !== payload.displayHash) {
        return { valid: false, reason: 'Profile hash is not derived from the ML-DSA-87 public key' };
    }

    const signatureOk = ml_dsa87.verify(
        base64ToBytes(signature),
        encoder.encode(payloadString),
        base64ToBytes(pqcIdentityPublicKey)
    );

    if (!signatureOk) {
        return { valid: false, reason: 'ML-DSA-87 signature verification failed' };
    }

    return { valid: true, reason: 'Verified ML-DSA-87 rooted identity binding' };
}
