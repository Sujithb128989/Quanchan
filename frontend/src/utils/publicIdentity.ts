const IDENTITY_PREFIX = 'Identity!';
const HASH_PATTERN = /^[a-f0-9]{16}$/i;

export interface ParsedPublicIdentity {
    isIdentity: boolean;
    label: string;
    target: string | null;
    stableHash: string | null;
}

function normalizeHash(value?: string | null): string | null {
    const trimmed = (value || '').trim().replace(/^!/, '');
    return HASH_PATTERN.test(trimmed) ? trimmed.toLowerCase() : null;
}

export function buildPublicIdentityMarker(label: string, displayHash: string): string {
    const safeLabel = label.trim() || displayHash;
    return `${IDENTITY_PREFIX}${safeLabel}#${displayHash}`;
}

export function parsePublicIdentityMarker(name?: string | null, tripcode?: string | null): ParsedPublicIdentity {
    const rawName = (name || '').trim();
    const hashFromTripcode = normalizeHash(tripcode);

    if (hashFromTripcode) {
        return {
            isIdentity: true,
            label: rawName || hashFromTripcode,
            target: hashFromTripcode,
            stableHash: hashFromTripcode,
        };
    }

    if (!rawName.startsWith(IDENTITY_PREFIX)) {
        return {
            isIdentity: false,
            label: rawName || 'Anonymous',
            target: null,
            stableHash: null,
        };
    }

    const payload = rawName.slice(IDENTITY_PREFIX.length).trim();
    const markerIndex = payload.lastIndexOf('#');
    if (markerIndex > 0) {
        const label = payload.slice(0, markerIndex).trim();
        const stableHash = normalizeHash(payload.slice(markerIndex + 1));
        if (stableHash) {
            return {
                isIdentity: true,
                label: label || stableHash,
                target: stableHash,
                stableHash,
            };
        }
    }

    const legacyTarget = payload.trim();
    const legacyStableHash = normalizeHash(legacyTarget);
    return {
        isIdentity: true,
        label: legacyTarget || 'Anonymous',
        target: legacyTarget || null,
        stableHash: legacyStableHash,
    };
}
