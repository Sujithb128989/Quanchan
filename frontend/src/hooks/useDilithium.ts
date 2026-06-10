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
/**
 * useDilithium â€” WASM Bridge Hook for Dilithium5 Signature Verification
 *
 * Asynchronously loads dilithium_wasm.js from /wasm/ and exposes a
 * verifySignature function with strict WASM heap memory management.
 *
 * CRITICAL: All WASM heap allocations are freed in a try/finally block
 * to prevent browser memory leaks even if verification throws.
 */

import { useState, useEffect, useRef, useCallback } from 'react';

// Type for the Emscripten module created by MODULARIZE=1
interface DilithiumWasmModule {
  _dilithium5_verify: (
    msgPtr: number, msgLen: number,
    sigPtr: number, sigLen: number,
    pubPtr: number, pubLen: number
  ) => number;
  _dilithium5_public_key_len: () => number;
  _dilithium5_signature_len: () => number;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAPU8: Uint8Array;
  ccall: (...args: unknown[]) => unknown;
  cwrap: (...args: unknown[]) => (...args: unknown[]) => unknown;
}

export interface DilithiumState {
  ready: boolean;
  loading: boolean;
  error: string | null;
}

/**
 * Decode a Base64 string to a Uint8Array.
 * Works in both browser and Node environments.
 */
function base64ToUint8Array(b64: string): Uint8Array {
  const binaryStr = atob(b64);
  const bytes = new Uint8Array(binaryStr.length);
  for (let i = 0; i < binaryStr.length; i++) {
    bytes[i] = binaryStr.charCodeAt(i);
  }
  return bytes;
}

/**
 * Convert a UTF-8 string to a Uint8Array using TextEncoder.
 */
function stringToUint8Array(str: string): Uint8Array {
  return new TextEncoder().encode(str);
}

export function useDilithium() {
  const moduleRef = useRef<DilithiumWasmModule | null>(null);
  const [state, setState] = useState<DilithiumState>({
    ready: false,
    loading: true,
    error: null,
  });

  // Load the WASM module on mount
  useEffect(() => {
    let cancelled = false;

    async function loadModule() {
      try {
        // Dynamically inject the Emscripten JS glue script into the DOM
        // to bypass Vite's strict module import analysis on /public assets.
        await new Promise<void>((resolve, reject) => {
           if ((window as any).DilithiumModule) return resolve();
           const script = document.createElement('script');
           script.src = '/wasm/dilithium_wasm.js';
           script.onload = () => resolve();
           script.onerror = () => reject(new Error('Failed to load WASM JS glue script.'));
           document.body.appendChild(script);
        });

        // Initialize the module using the global factory exported by Emscripten
        // via -s EXPORT_NAME='DilithiumModule'
        // @ts-ignore
        const module: DilithiumWasmModule = await (window as any).DilithiumModule();

        if (!cancelled) {
          moduleRef.current = module;
          setState({ ready: true, loading: false, error: null });
          console.log(
            '[PQC] Dilithium5 WASM module loaded. Public key size:',
            module._dilithium5_public_key_len(),
            'Signature size:',
            module._dilithium5_signature_len()
          );
        }
      } catch (err) {
        if (!cancelled) {
          const msg = err instanceof Error ? err.message : String(err);
          console.error('[PQC] Failed to load Dilithium5 WASM:', msg);
          setState({ ready: false, loading: false, error: msg });
        }
      }
    }

    loadModule();
    return () => { cancelled = true; };
  }, []);

  /**
   * Verify a Dilithium5 signature using the WASM module.
   *
   * @param payloadString - The exact signed payload as a string (will be UTF-8 encoded)
   * @param sigBase64     - Base64-encoded Dilithium5 signature
   * @param pubKeyBase64  - Base64-encoded Dilithium5 public key
   * @returns 0 = valid, -1 = invalid signature, -2 = OQS init error, null = module not ready
   *
   * CRITICAL: Uses try/finally to guarantee _free() on all heap pointers.
   */
  const verifySignature = useCallback(
    (payloadString: string, sigBase64: string, pubKeyBase64: string): number | null => {
      const module = moduleRef.current;
      if (!module) {
        console.warn('[PQC] verifySignature called before WASM module is ready');
        return null;
      }

      if (!payloadString || !sigBase64 || !pubKeyBase64) {
        console.warn('[PQC] verifySignature called with empty payload, signature, or public key');
        return -1;
      }

      try {
        // Decode inputs to Uint8Arrays
        const msgBytes = stringToUint8Array(payloadString);
        const sigBytes = base64ToUint8Array(sigBase64);
        const pubBytes = base64ToUint8Array(pubKeyBase64);

        // Allocate WASM heap memory for each buffer
        let msgPtr = 0;
        let sigPtr = 0;
        let pubPtr = 0;

        try {
          msgPtr = module._malloc(msgBytes.length);
          sigPtr = module._malloc(sigBytes.length);
          pubPtr = module._malloc(pubBytes.length);

          if (!msgPtr || !sigPtr || !pubPtr) {
            console.error('[PQC] WASM _malloc returned null pointer');
            return -2;
          }

          // Copy JS Uint8Array data into the WASM linear memory
          module.HEAPU8.set(msgBytes, msgPtr);
          module.HEAPU8.set(sigBytes, sigPtr);
          module.HEAPU8.set(pubBytes, pubPtr);

          // Call the compiled C function
          const result = module._dilithium5_verify(
            msgPtr, msgBytes.length,
            sigPtr, sigBytes.length,
            pubPtr, pubBytes.length
          );

          return result;
        } finally {
          // CRITICAL: Always free allocated WASM heap memory
          if (msgPtr) module._free(msgPtr);
          if (sigPtr) module._free(sigPtr);
          if (pubPtr) module._free(pubPtr);
        }
      } catch (err) {
        console.error('[PQC] Error decoding/verifying signature:', err);
        return -1;
      }
    },
    []
  );

  return {
    ...state,
    verifySignature,
  };
}
