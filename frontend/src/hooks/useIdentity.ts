/**
 * useIdentity - Client-Side Identity Vault
 *
 * Generates a local ML-DSA-87 rooted identity, an ML-KEM-1024 keypair,
 * and compatibility metadata for legacy Ed25519 paths on first visit.
 * Stores the material in localStorage and can restore it from an
 * encrypted recovery vault using the 12-word recovery phrase.
 */

import { useState, useEffect, useCallback } from 'react';
import { generatePqcKemKeypair } from '../utils/pqcMessaging';
import { computeDisplayHash, generatePqcIdentityKeypair } from '../utils/identityBinding';
import { computeRecoveryLookupHash, decryptRecoveryVault } from '../utils/recoveryVault';
import { setFounderToken } from '../utils/roleAuth';
import { createDeterministicIdentityFromSeedPhrase, deriveFounderTokenFromSeedPhrase, normalizeSeedPhrase } from '../utils/seedWallet';

const WORDLIST = [
  'abandon','ability','abstract','academy','access','acid','acoustic','action',
  'adapt','address','adjust','admit','advance','afford','agent','agree',
  'ahead','alarm','album','alert','alien','alpha','alter','ambient',
  'anchor','angel','annual','antique','anxiety','apart','apology','approve',
  'arena','armor','arrow','artist','aspect','assault','asset','atlas',
  'atom','auction','audit','august','aurora','autumn','avenue','axis',
  'badge','balance','ballot','banner','barrel','base','beacon','binary',
  'blade','blast','blaze','blind','block','bolt','border','breach',
  'bridge','brutal','bucket','bullet','burden','bypass','cabin','cage',
  'camera','canyon','carbon','cargo','cascade','castle','catalyst','cedar',
  'census','central','chain','chamber','chaos','charge','chrome','cipher',
  'circuit','citadel','claim','clash','client','clock','cluster','cobra',
  'codec','collar','column','combat','comet','command','compact','complex',
  'concert','conduit','console','contact','convert','copper','coral','core',
  'corona','cortex','cosmic','council','coupled','covert','crater','credit',
  'crisis','cross','crown','crucial','crypto','crystal','current','cursor',
];

function generateSeedPhrase(): string {
  const words: string[] = [];
  const entropy = new Uint32Array(12);
  crypto.getRandomValues(entropy);
  for (let i = 0; i < 12; i++) {
    words.push(WORDLIST[entropy[i] % WORDLIST.length]);
  }
  return words.join(' ');
}

export interface Identity {
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

const STORAGE_KEY = 'quanchan_identity';

export function useIdentity() {
  const [identity, setIdentity] = useState<Identity | null>(null);
  const [loading, setLoading] = useState(true);

  const persistIdentity = useCallback((nextIdentity: Identity) => {
    setIdentity(nextIdentity);
    localStorage.setItem(STORAGE_KEY, JSON.stringify(nextIdentity));
  }, []);

  const syncFounderSession = useCallback((nextIdentity: Identity, fallbackFounderToken = '') => {
    if (nextIdentity.walletVersion === 2) {
      deriveFounderTokenFromSeedPhrase(nextIdentity.seedPhrase)
        .then(setFounderToken)
        .catch(err => console.error('Failed to derive founder token from seed phrase:', err));
      return;
    }
    if (fallbackFounderToken) {
      setFounderToken(fallbackFounderToken);
    }
  }, []);

  const hydrateIdentity = useCallback(async (parsed: Partial<Identity>): Promise<Identity | null> => {
    const normalizedSeedPhrase = parsed.seedPhrase ? normalizeSeedPhrase(parsed.seedPhrase) : '';
    if (!normalizedSeedPhrase) {
      return null;
    }

    if (parsed.walletVersion === 2) {
      const derived = await createDeterministicIdentityFromSeedPhrase(normalizedSeedPhrase);
      return {
        ...derived,
        seedPhrase: normalizedSeedPhrase,
        walletVersion: 2,
        username: parsed.username?.trim() || undefined,
      };
    }

    if (!parsed.publicKey || !parsed.privateKey || !parsed.displayHash) {
      return null;
    }

    let nextIdentity: Identity = parsed as Identity;
    if (!parsed.pqcKemPublicKey || !parsed.pqcKemSecretKey) {
      const pqcKeys = generatePqcKemKeypair();
      nextIdentity = {
        ...nextIdentity,
        pqcKemPublicKey: pqcKeys.publicKey,
        pqcKemSecretKey: pqcKeys.secretKey,
        pqcKemScheme: pqcKeys.scheme,
      };
    }
    if (!parsed.pqcIdentityPublicKey || !parsed.pqcIdentitySecretKey) {
      const pqcIdentity = generatePqcIdentityKeypair();
      nextIdentity = {
        ...nextIdentity,
        pqcIdentityPublicKey: pqcIdentity.publicKey,
        pqcIdentitySecretKey: pqcIdentity.secretKey,
        pqcIdentityScheme: pqcIdentity.scheme,
      };
    }
    nextIdentity = {
      ...nextIdentity,
      seedPhrase: normalizedSeedPhrase,
      walletVersion: 1,
      username: parsed.username?.trim() || undefined,
      displayHash: await computeDisplayHash(nextIdentity.pqcIdentityPublicKey),
    };
    return nextIdentity;
  }, []);

  useEffect(() => {
    async function init() {
      const stored = localStorage.getItem(STORAGE_KEY);
      if (stored) {
        try {
          const parsed = JSON.parse(stored) as Partial<Identity>;
          const nextIdentity = await hydrateIdentity(parsed);
          if (nextIdentity) {
            if (
              nextIdentity.displayHash !== parsed.displayHash
              || nextIdentity.publicKey !== parsed.publicKey
              || nextIdentity.privateKey !== parsed.privateKey
              || nextIdentity.pqcKemPublicKey !== parsed.pqcKemPublicKey
              || nextIdentity.pqcIdentityPublicKey !== parsed.pqcIdentityPublicKey
              || nextIdentity.pqcKemScheme !== parsed.pqcKemScheme
              || nextIdentity.pqcIdentityScheme !== parsed.pqcIdentityScheme
              || nextIdentity.walletVersion !== parsed.walletVersion
              || nextIdentity.seedPhrase !== parsed.seedPhrase
            ) {
              localStorage.setItem(STORAGE_KEY, JSON.stringify(nextIdentity));
            }
            syncFounderSession(nextIdentity);
            setIdentity(nextIdentity);
            setLoading(false);
            return;
          }
        } catch {
          localStorage.removeItem(STORAGE_KEY);
        }
      }

      try {
        const seedPhrase = generateSeedPhrase();
        const derived = await createDeterministicIdentityFromSeedPhrase(seedPhrase);
        const newIdentity: Identity = {
          publicKey: derived.publicKey,
          privateKey: derived.privateKey,
          seedPhrase,
          displayHash: derived.displayHash,
          walletVersion: 2,
          pqcKemPublicKey: derived.pqcKemPublicKey,
          pqcKemSecretKey: derived.pqcKemSecretKey,
          pqcKemScheme: derived.pqcKemScheme,
          pqcIdentityPublicKey: derived.pqcIdentityPublicKey,
          pqcIdentitySecretKey: derived.pqcIdentitySecretKey,
          pqcIdentityScheme: derived.pqcIdentityScheme,
        };
        localStorage.setItem(STORAGE_KEY, JSON.stringify(newIdentity));
        setFounderToken(derived.founderToken);
        setIdentity(newIdentity);
      } catch (err) {
        console.error('[Identity] Failed to generate keypair:', err);
      }
      setLoading(false);
    }

    init();
  }, [hydrateIdentity]);

  const setUsername = useCallback((username: string) => {
    if (!identity) return;
    const updated = { ...identity, username: username.trim() || undefined };
    persistIdentity(updated);
  }, [identity, persistIdentity]);

  const restoreIdentityFromRecoveryPhrase = useCallback(async (phrase: string) => {
    const normalizedPhrase = normalizeSeedPhrase(phrase);
    const deterministic = await createDeterministicIdentityFromSeedPhrase(normalizedPhrase);
    let restored: Identity = {
      publicKey: deterministic.publicKey,
      privateKey: deterministic.privateKey,
      seedPhrase: normalizedPhrase,
      displayHash: deterministic.displayHash,
      walletVersion: 2,
      pqcKemPublicKey: deterministic.pqcKemPublicKey,
      pqcKemSecretKey: deterministic.pqcKemSecretKey,
      pqcKemScheme: deterministic.pqcKemScheme,
      pqcIdentityPublicKey: deterministic.pqcIdentityPublicKey,
      pqcIdentitySecretKey: deterministic.pqcIdentitySecretKey,
      pqcIdentityScheme: deterministic.pqcIdentityScheme,
    };
    let founderToken = deterministic.founderToken;

    const recoveryLookupHash = await computeRecoveryLookupHash(normalizedPhrase);
    const res = await fetch('/api/identity/recover', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ recovery_lookup_hash: recoveryLookupHash }),
    });

    if (res.ok) {
      const data = await res.json().catch(() => ({}));
      try {
        const vault = await decryptRecoveryVault(
          data.recovery_bundle_ciphertext,
          data.recovery_bundle_iv,
          normalizedPhrase
        );
        const vaultIdentity = await hydrateIdentity(vault.identity);
        if (vaultIdentity) {
          if (vaultIdentity.displayHash !== restored.displayHash) {
            restored = vaultIdentity;
          } else if (vaultIdentity.username && !restored.username) {
            restored = { ...restored, username: vaultIdentity.username };
          }
        }
        if (restored.walletVersion !== 2 && vault.founderToken) {
          founderToken = vault.founderToken;
        }
      } catch (error) {
        console.error('Recovery vault decrypt failed, falling back to deterministic seed restore:', error);
      }
    } else if (res.status !== 404) {
      console.warn('Recovery lookup was unavailable; continuing with deterministic seed restore.');
    }

    persistIdentity(restored);
    syncFounderSession(restored, founderToken);
    return restored;
  }, [hydrateIdentity, persistIdentity, syncFounderSession]);

  return { identity, loading, setUsername, restoreIdentityFromRecoveryPhrase };
}
