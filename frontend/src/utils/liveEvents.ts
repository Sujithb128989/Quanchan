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
interface LiveHandlers<T> {
    onUpdate: (payload: T) => void;
    onTimeout?: (payload: T) => void;
    onOpen?: () => void;
    onError?: () => void;
}

function parseEventPayload<T>(event: MessageEvent<string>, callback?: (payload: T) => void) {
    if (!callback) return;
    try {
        callback(JSON.parse(event.data) as T);
    } catch (error) {
        console.error('Failed to parse live event payload:', error);
    }
}

export function subscribeToLiveEvent<T>(url: string, handlers: LiveHandlers<T>) {
    if (typeof window === 'undefined' || typeof EventSource === 'undefined') {
        return null;
    }

    const source = new EventSource(url);
    source.addEventListener('open', () => handlers.onOpen?.());
    source.addEventListener('update', event => parseEventPayload(event as MessageEvent<string>, handlers.onUpdate));
    source.addEventListener('timeout', event => parseEventPayload(event as MessageEvent<string>, handlers.onTimeout));
    source.onerror = () => {
        if (source.readyState === EventSource.CLOSED) {
            handlers.onError?.();
        }
    };

    return () => {
        source.close();
    };
}
