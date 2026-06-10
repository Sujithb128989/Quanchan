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
