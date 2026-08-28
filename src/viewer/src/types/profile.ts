import { DisplayOptions, sanitize_options } from './options';

const OPTIONS_URL = '/api/options';

/**
 * Prodleva mezi první změnou a zápisem. Timer se u dalších změn NEresetuje, jen
 * se aktualizuje ukládaný obsah - jinak by se při plynulé změně (tažení
 * posuvníku barvy) zápis odkládal donekonečna.
 */
const SAVE_DELAY = 500;

/**
 * Načte options z profilu na serveru. Jakékoliv selhání (chybějící profil,
 * nedostupný server, poškozený obsah) skončí výchozím nastavením - viewer se
 * kvůli profilu nesmí odmítnout spustit.
 */
export async function load_options(): Promise<DisplayOptions> {
    try {
        const r = await fetch(OPTIONS_URL);
        if (r.status === 204) return sanitize_options(undefined);  // profil zatím není
        if (!r.ok) {
            console.warn(`Cannot load options profile: HTTP ${r.status}`);
            return sanitize_options(undefined);
        }
        return sanitize_options(await r.json());
    } catch (e) {
        console.warn(`Cannot load options profile: ${(e as Error).message}`);
        return sanitize_options(undefined);
    }
}

let pending: string | null = null;
let timer: ReturnType<typeof setTimeout> | undefined;

function put(body: string, keepalive: boolean) {
    return fetch(OPTIONS_URL, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body,
        keepalive
    }).catch(e => {
        console.warn(`Cannot save options profile: ${(e as Error).message}`);
    });
}

function flush(keepalive = false) {
    if (timer !== undefined) {
        clearTimeout(timer);
        timer = undefined;
    }
    if (pending === null) return;
    const body = pending;
    pending = null;
    put(body, keepalive);
}

/** Uloží options; zápisy se slučují po SAVE_DELAY. */
export function save_options(options: DisplayOptions) {
    pending = JSON.stringify(options);
    if (timer !== undefined) return;
    timer = setTimeout(() => flush(), SAVE_DELAY);
}

// pagehide je jediná událost, která spolehlivě přijde i při zavření tabu;
// keepalive drží požadavek naživu, i když už dokument mizí
window.addEventListener('pagehide', () => flush(true));
