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
import { ml_kem1024 } from '@noble/post-quantum/ml-kem.js';

export const PQC_DM_SCHEME = 'ML-KEM-1024+AES-256-GCM';

export interface PqcEnvelopeSide {
    kemCiphertext: string;
    ciphertext: string;
    iv: string;
}

export interface PqcEncryptedMessage {
    version: 1;
    scheme: typeof PQC_DM_SCHEME;
    sender: PqcEnvelopeSide;
    receiver: PqcEnvelopeSide;
}

function bytesToBase64(bytes: Uint8Array): string {
    let binary = '';
    for (let i = 0; i < bytes.length; i += 1) {
        binary += String.fromCharCode(bytes[i]);
    }
    return btoa(binary);
}

function base64ToBytes(value: string): Uint8Array<ArrayBuffer> {
    const binary = atob(value);
    const out = new Uint8Array(new ArrayBuffer(binary.length));
    for (let i = 0; i < binary.length; i += 1) {
        out[i] = binary.charCodeAt(i);
    }
    return out;
}

function toStrictBytes(value: Uint8Array): Uint8Array<ArrayBuffer> {
    const out = new Uint8Array(new ArrayBuffer(value.length));
    out.set(value);
    return out;
}

async function deriveAesKey(sharedSecret: Uint8Array): Promise<CryptoKey> {
    const label = new TextEncoder().encode('quanchan-pqc-dm-aes-key');
    const material = new Uint8Array(new ArrayBuffer(label.length + sharedSecret.length));
    material.set(label, 0);
    material.set(sharedSecret, label.length);
    const digest = await crypto.subtle.digest('SHA-256', material);
    return crypto.subtle.importKey('raw', digest, { name: 'AES-GCM' }, false, ['encrypt', 'decrypt']);
}

async function encryptForSecret(plaintext: string, sharedSecret: Uint8Array): Promise<Omit<PqcEnvelopeSide, 'kemCiphertext'>> {
    const key = await deriveAesKey(sharedSecret);
    const iv = crypto.getRandomValues(new Uint8Array(12));
    const ciphertext = await crypto.subtle.encrypt(
        { name: 'AES-GCM', iv },
        key,
        new TextEncoder().encode(plaintext).buffer
    );

    return {
        ciphertext: bytesToBase64(new Uint8Array(ciphertext)),
        iv: bytesToBase64(iv),
    };
}

async function decryptForSecret(ciphertextB64: string, ivB64: string, sharedSecret: Uint8Array): Promise<string> {
    const key = await deriveAesKey(sharedSecret);
    const decrypted = await crypto.subtle.decrypt(
        { name: 'AES-GCM', iv: base64ToBytes(ivB64) },
        key,
        base64ToBytes(ciphertextB64)
    );
    return new TextDecoder().decode(decrypted);
}

export function generatePqcKemKeypair() {
    const keys = ml_kem1024.keygen();
    return {
        publicKey: bytesToBase64(keys.publicKey),
        secretKey: bytesToBase64(keys.secretKey),
        scheme: 'ML-KEM-1024' as const,
    };
}

export async function encryptDirectMessageForParticipants(
    plaintext: string,
    senderPublicKeyB64: string,
    receiverPublicKeyB64: string
): Promise<PqcEncryptedMessage> {
    const senderKem = ml_kem1024.encapsulate(toStrictBytes(base64ToBytes(senderPublicKeyB64)));
    const receiverKem = ml_kem1024.encapsulate(toStrictBytes(base64ToBytes(receiverPublicKeyB64)));
    const senderEncrypted = await encryptForSecret(plaintext, senderKem.sharedSecret);
    const receiverEncrypted = await encryptForSecret(plaintext, receiverKem.sharedSecret);

    return {
        version: 1,
        scheme: PQC_DM_SCHEME,
        sender: {
            kemCiphertext: bytesToBase64(senderKem.cipherText),
            ciphertext: senderEncrypted.ciphertext,
            iv: senderEncrypted.iv,
        },
        receiver: {
            kemCiphertext: bytesToBase64(receiverKem.cipherText),
            ciphertext: receiverEncrypted.ciphertext,
            iv: receiverEncrypted.iv,
        },
    };
}

export function isPqcEncryptedMessage(value: string): value is string {
    if (!value || value[0] !== '{') return false;
    try {
        const parsed = JSON.parse(value) as Partial<PqcEncryptedMessage>;
        return parsed.version === 1 && parsed.scheme === PQC_DM_SCHEME
            && !!parsed.sender?.ciphertext && !!parsed.receiver?.ciphertext;
    } catch {
        return false;
    }
}

export async function decryptDirectMessageEnvelope(
    serialized: string,
    role: 'sender' | 'receiver',
    secretKeyB64: string
): Promise<string> {
    const parsed = JSON.parse(serialized) as PqcEncryptedMessage;
    const secretKey = toStrictBytes(base64ToBytes(secretKeyB64));
    const selected = role === 'sender' ? parsed.sender : parsed.receiver;
    const sharedSecret = ml_kem1024.decapsulate(toStrictBytes(base64ToBytes(selected.kemCiphertext)), secretKey);
    return decryptForSecret(selected.ciphertext, selected.iv, sharedSecret);
}
