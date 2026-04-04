import { useEffect, useState } from 'react';

const PHONE_MEDIA_QUERY = '(max-width: 900px)';

export function useIsPhone() {
    const [isPhone, setIsPhone] = useState(() => window.matchMedia(PHONE_MEDIA_QUERY).matches);

    useEffect(() => {
        const mediaQuery = window.matchMedia(PHONE_MEDIA_QUERY);
        const sync = () => setIsPhone(mediaQuery.matches);
        sync();
        mediaQuery.addEventListener('change', sync);
        return () => mediaQuery.removeEventListener('change', sync);
    }, []);

    return isPhone;
}
