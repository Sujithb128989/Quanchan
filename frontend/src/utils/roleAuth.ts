export type ProfileRole = 'user' | 'moderator' | 'founder';

export const FOUNDER_TOKEN_STORAGE_KEY = 'quanchan_founder_token';

export function getFounderToken(): string {
    if (typeof window === 'undefined') return '';
    return localStorage.getItem(FOUNDER_TOKEN_STORAGE_KEY) || '';
}

export function setFounderToken(token: string) {
    if (typeof window === 'undefined') return;
    if (token) {
        localStorage.setItem(FOUNDER_TOKEN_STORAGE_KEY, token);
    } else {
        localStorage.removeItem(FOUNDER_TOKEN_STORAGE_KEY);
    }
    window.dispatchEvent(new CustomEvent('quanchan:founder-token', { detail: { token } }));
}

export function normalizeProfileRole(value?: string): ProfileRole {
    if (value === 'founder' || value === 'moderator') {
        return value;
    }
    return 'user';
}
