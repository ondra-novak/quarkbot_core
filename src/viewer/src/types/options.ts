export type QLineStyle = "solid" | "double" | "dashed" | "area";

export interface SeriesSetupItem {
    color: string;
    line_style: QLineStyle;
};

export type SeriesSetup = Record<string, SeriesSetupItem>




export const paneTypes = ["main", "secondary", "pane_1", "pane_2","pane_3"] as const;

export type PaneType = (typeof paneTypes)[number];


export type SeriesToPanes = Record<PaneType, string[]>;


export interface DisplayOptions  {
    instrument:string;
    interval: number;
    series_to_panes: SeriesToPanes;
    fills: boolean;
    orders: boolean;
    volume: boolean;
    setup: SeriesSetup;
};

export const qLineStyles = ["solid", "double", "dashed", "area"] as const;

/** Výchozí nastavení pro čistý profil. Vždy nová instance. */
export function default_options(): DisplayOptions {
    return {
        instrument: "",
        interval: 5,
        fills: true,
        orders: false,
        series_to_panes: {main: [], secondary: [], pane_1: ["Equity"], pane_2: [], pane_3: []},
        volume: true,
        setup: {
            "Equity": {color: "#000000", line_style: "area"},
            "Position": {color: "#000000", line_style: "area"}
        }
    };
}

function is_record(x: unknown): x is Record<string, unknown> {
    return typeof x === "object" && x !== null && !Array.isArray(x);
}

function pick_bool(v: unknown, def: boolean): boolean {
    return typeof v === "boolean" ? v : def;
}

function pick_string(v: unknown, def: string): string {
    return typeof v === "string" ? v : def;
}

function pick_interval(v: unknown, def: number): number {
    return typeof v === "number" && Number.isFinite(v) && v > 0 ? v : def;
}

function pick_setup_item(v: unknown): SeriesSetupItem | null {
    if (!is_record(v)) return null;
    if (typeof v.color !== "string") return null;
    if (!(qLineStyles as readonly string[]).includes(v.line_style as string)) return null;
    return {color: v.color, line_style: v.line_style as QLineStyle};
}

/**
 * Přetaví options načtené z profilu na disku do platného DisplayOptions.
 *
 * Profil je soubor, který mohl vzniknout starší verzí vieweru nebo být ručně
 * upraven, takže se mu nedá věřit: chybějící klíče se doplní z defaultů,
 * neznámé se zahodí a hodnoty špatného typu se nahradí defaultem. Vstup se
 * nemodifikuje.
 */
export function sanitize_options(raw: unknown): DisplayOptions {
    const def = default_options();
    if (!is_record(raw)) return def;

    const out: DisplayOptions = {
        instrument: pick_string(raw.instrument, def.instrument),
        interval: pick_interval(raw.interval, def.interval),
        fills: pick_bool(raw.fills, def.fills),
        orders: pick_bool(raw.orders, def.orders),
        volume: pick_bool(raw.volume, def.volume),
        series_to_panes: def.series_to_panes,
        setup: def.setup
    };

    if (is_record(raw.series_to_panes)) {
        const src = raw.series_to_panes;
        // jen známé pane typy; chybějící zůstanou prázdné z defaultů
        for (const pane of paneTypes) {
            if (!(pane in src)) continue;
            const list = src[pane];
            out.series_to_panes[pane] = Array.isArray(list)
                ? list.filter((x): x is string => typeof x === "string")
                : [];
        }
    }

    if (is_record(raw.setup)) {
        // názvy proměnných neznáme dopředu, validuje se jen tvar záznamu
        for (const name in raw.setup) {
            const item = pick_setup_item(raw.setup[name]);
            if (item) out.setup[name] = item;
        }
    }

    return out;
}
