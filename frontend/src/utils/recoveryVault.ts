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
import { normalizeSeedPhrase } from './seedWallet';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export interface RecoverableIdentity {
    publicKey: string;
    privateKey: string;
    seedPhrase: string;
    displayHash: string;
    walletVersion?: 1 | 2;
    username?: string;
    pqcKemPublicKey: string;
    pqcKemSecretKey: string;
    pqcKemScheme: 'ML-KEM-1024';
    pqcIdentityPublicKey: string;
    pqcIdentitySecretKey: string;
    pqcIdentityScheme: 'ML-DSA-87';
}

interface RecoveryVaultPayload {
    version: 1 | 2;
    savedAt: string;
    founderToken: string;
    identity: RecoverableIdentity;
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

function toStrictBytes(value: Uint8Array): Uint8Array<ArrayBuffer> {
    const out = new Uint8Array(new ArrayBuffer(value.length));
    out.set(value);
    return out;
}

export function normalizeRecoveryPhrase(value: string): string {
    return normalizeSeedPhrase(value);
}

async function deriveDigest(label: string, phrase: string): Promise<Uint8Array> {
    const normalized = normalizeRecoveryPhrase(phrase);
    const digest = await crypto.subtle.digest('SHA-256', encoder.encode(`${label}:${normalized}`));
    return new Uint8Array(digest);
}

async function deriveVaultKey(phrase: string): Promise<CryptoKey> {
    const digest = await deriveDigest('quanchan-recovery-vault', phrase);
    return crypto.subtle.importKey('raw', toStrictBytes(digest), { name: 'AES-GCM' }, false, ['encrypt', 'decrypt']);
}

export async function computeRecoveryLookupHash(phrase: string): Promise<string> {
    const digest = await deriveDigest('quanchan-recovery-lookup', phrase);
    return bytesToHex(digest);
}

export async function encryptRecoveryVault(
    identity: RecoverableIdentity,
    founderToken: string,
    phrase: string
) {
    const key = await deriveVaultKey(phrase);
    const iv = crypto.getRandomValues(new Uint8Array(12));
    const payload: RecoveryVaultPayload = {
        version: identity.walletVersion === 2 ? 2 : 1,
        savedAt: new Date().toISOString(),
        founderToken,
        identity,
    };
    const ciphertext = await crypto.subtle.encrypt(
        { name: 'AES-GCM', iv },
        key,
        encoder.encode(JSON.stringify(payload))
    );

    return {
        recovery_lookup_hash: await computeRecoveryLookupHash(phrase),
        recovery_bundle_ciphertext: bytesToBase64(new Uint8Array(ciphertext)),
        recovery_bundle_iv: bytesToBase64(iv),
    };
}

export async function decryptRecoveryVault(
    recoveryBundleCiphertext: string,
    recoveryBundleIv: string,
    phrase: string
): Promise<RecoveryVaultPayload> {
    const key = await deriveVaultKey(phrase);
    const decrypted = await crypto.subtle.decrypt(
        { name: 'AES-GCM', iv: toStrictBytes(base64ToBytes(recoveryBundleIv)) },
        key,
        toStrictBytes(base64ToBytes(recoveryBundleCiphertext))
    );
    const parsed = JSON.parse(decoder.decode(decrypted)) as RecoveryVaultPayload;
    if ((parsed.version !== 1 && parsed.version !== 2) || !parsed.identity?.displayHash) {
        throw new Error('Recovery vault is invalid');
    }
    return parsed;
}
