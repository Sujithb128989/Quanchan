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
