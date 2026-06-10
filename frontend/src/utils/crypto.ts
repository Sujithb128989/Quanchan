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
const PBKDF2_ITERATIONS = 600_000;
const SALT_LENGTH = 16;
const IV_LENGTH = 12;

interface EncryptedPostEnvelope {
    version: 2;
    scheme: 'AES-256-GCM';
    kdf: 'PBKDF2-SHA256';
    iterations: number;
    salt: string;
    iv: string;
    ciphertext: string;
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
    const out = new Uint8Array(new ArrayBuffer(binary.length));
    for (let i = 0; i < binary.length; i += 1) {
        out[i] = binary.charCodeAt(i);
    }
    return out;
}

async function deriveKey(passphrase: string, salt: Uint8Array, iterations: number): Promise<CryptoKey> {
    const keyMaterial = await crypto.subtle.importKey(
        'raw',
        new TextEncoder().encode(passphrase),
        'PBKDF2',
        false,
        ['deriveKey']
    );

    return crypto.subtle.deriveKey(
        {
            name: 'PBKDF2',
            salt: salt as BufferSource,
            iterations,
            hash: 'SHA-256',
        },
        keyMaterial,
        { name: 'AES-GCM', length: 256 },
        false,
        ['encrypt', 'decrypt']
    );
}

export function isEncryptedPostPayload(value?: string): boolean {
    if (!value || value[0] !== '{') return false;
    try {
        const parsed = JSON.parse(value) as Partial<EncryptedPostEnvelope>;
        return parsed.version === 2
            && parsed.scheme === 'AES-256-GCM'
            && parsed.kdf === 'PBKDF2-SHA256'
            && typeof parsed.iterations === 'number'
            && !!parsed.salt
            && !!parsed.iv
            && !!parsed.ciphertext;
    } catch {
        return false;
    }
}

export async function encryptPost(plaintext: string, passphrase: string): Promise<string> {
    if (!passphrase.trim()) {
        throw new Error('Passphrase required for encrypted posts');
    }

    const salt = crypto.getRandomValues(new Uint8Array(SALT_LENGTH));
    const iv = crypto.getRandomValues(new Uint8Array(IV_LENGTH));
    const key = await deriveKey(passphrase, salt, PBKDF2_ITERATIONS);

    const encrypted = await crypto.subtle.encrypt(
        { name: 'AES-GCM', iv },
        key,
        new TextEncoder().encode(plaintext)
    );

    const payload: EncryptedPostEnvelope = {
        version: 2,
        scheme: 'AES-256-GCM',
        kdf: 'PBKDF2-SHA256',
        iterations: PBKDF2_ITERATIONS,
        salt: bytesToBase64(salt),
        iv: bytesToBase64(iv),
        ciphertext: bytesToBase64(new Uint8Array(encrypted)),
    };

    return JSON.stringify(payload);
}

export async function decryptPost(ciphertext: string, passphrase: string): Promise<string> {
    if (!passphrase.trim()) {
        throw new Error('Passphrase required for decryption');
    }

    const payload = JSON.parse(ciphertext) as EncryptedPostEnvelope;
    const iv = base64ToBytes(payload.iv) as BufferSource;
    const cipherBytes = base64ToBytes(payload.ciphertext) as BufferSource;
    const key = await deriveKey(
        passphrase,
        base64ToBytes(payload.salt),
        payload.iterations
    );

    const decrypted = await crypto.subtle.decrypt(
        { name: 'AES-GCM', iv },
        key,
        cipherBytes
    );

    return new TextDecoder().decode(decrypted);
}

export function generateQuantumId(): number {
    const arr = new Uint32Array(1);
    crypto.getRandomValues(arr);
    return arr[0];
}
