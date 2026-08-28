import { describe, it, expect } from 'vitest';
import { default_options, sanitize_options, paneTypes } from './options';

describe('default_options', () => {
    it('returns a fresh object each call', () => {
        const a = default_options();
        const b = default_options();
        expect(a).not.toBe(b);
        a.series_to_panes.main.push('X');
        expect(b.series_to_panes.main).toEqual([]);
    });

    it('has an entry for every pane type', () => {
        const d = default_options();
        for (const p of paneTypes) {
            expect(Array.isArray(d.series_to_panes[p])).toBe(true);
        }
    });
});

describe('sanitize_options', () => {
    it('returns defaults for missing input', () => {
        expect(sanitize_options(undefined)).toEqual(default_options());
        expect(sanitize_options(null)).toEqual(default_options());
        expect(sanitize_options({})).toEqual(default_options());
    });

    it('returns defaults for non-object input', () => {
        expect(sanitize_options('nonsense')).toEqual(default_options());
        expect(sanitize_options(42)).toEqual(default_options());
        expect(sanitize_options([])).toEqual(default_options());
    });

    it('keeps valid scalar values', () => {
        const r = sanitize_options({ instrument: 'BTCUSD', interval: 60, fills: false, orders: true, volume: false });
        expect(r.instrument).toBe('BTCUSD');
        expect(r.interval).toBe(60);
        expect(r.fills).toBe(false);
        expect(r.orders).toBe(true);
        expect(r.volume).toBe(false);
    });

    it('falls back to defaults for wrong scalar types', () => {
        const d = default_options();
        const r = sanitize_options({ instrument: 123, interval: 'abc', fills: 'yes', orders: null, volume: 0 });
        expect(r.instrument).toBe(d.instrument);
        expect(r.interval).toBe(d.interval);
        expect(r.fills).toBe(d.fills);
        expect(r.orders).toBe(d.orders);
        expect(r.volume).toBe(d.volume);
    });

    it('rejects a non-positive or non-finite interval', () => {
        const d = default_options();
        expect(sanitize_options({ interval: 0 }).interval).toBe(d.interval);
        expect(sanitize_options({ interval: -5 }).interval).toBe(d.interval);
        expect(sanitize_options({ interval: NaN }).interval).toBe(d.interval);
    });

    it('drops unknown top-level keys', () => {
        const r = sanitize_options({ instrument: 'X', bogus: 'whatever' }) as unknown as Record<string, unknown>;
        expect('bogus' in r).toBe(false);
    });

    it('fills in pane types missing from a stored profile', () => {
        const r = sanitize_options({ series_to_panes: { main: ['Equity'] } });
        expect(r.series_to_panes.main).toEqual(['Equity']);
        for (const p of paneTypes) {
            expect(Array.isArray(r.series_to_panes[p])).toBe(true);
        }
    });

    it('drops unknown pane names and non-string series entries', () => {
        const r = sanitize_options({
            series_to_panes: { main: ['Equity', 7, null, 'Position'], pane_9: ['X'] },
        });
        expect(r.series_to_panes.main).toEqual(['Equity', 'Position']);
        expect('pane_9' in r.series_to_panes).toBe(false);
    });

    it('replaces a non-array pane value with an empty array', () => {
        const r = sanitize_options({ series_to_panes: { main: 'Equity' } });
        expect(r.series_to_panes.main).toEqual([]);
    });

    it('keeps setup entries for arbitrary variable names', () => {
        const r = sanitize_options({
            setup: { 'BTCUSD::ema_master': { color: '#112233', line_style: 'dashed' } },
        });
        expect(r.setup['BTCUSD::ema_master']).toEqual({ color: '#112233', line_style: 'dashed' });
    });

    it('drops setup entries with an invalid line style or color', () => {
        const r = sanitize_options({
            setup: {
                good: { color: '#ffffff', line_style: 'area' },
                bad_style: { color: '#ffffff', line_style: 'zigzag' },
                bad_color: { color: 42, line_style: 'solid' },
                not_an_object: 'nope',
            },
        });
        expect(Object.keys(r.setup).sort()).toEqual(['Equity', 'Position', 'good']);
    });

    it('lets a stored setup entry override a default one', () => {
        const r = sanitize_options({ setup: { Equity: { color: '#ff0000', line_style: 'solid' } } });
        expect(r.setup['Equity']).toEqual({ color: '#ff0000', line_style: 'solid' });
    });

    it('does not modify its input', () => {
        const input = { series_to_panes: { main: ['Equity'] }, setup: { a: { color: '#fff', line_style: 'solid' } } };
        const snapshot = JSON.parse(JSON.stringify(input));
        sanitize_options(input);
        expect(input).toEqual(snapshot);
    });

    it('survives a round trip through JSON', () => {
        const d = default_options();
        d.instrument = 'BTCUSD';
        d.setup['x'] = { color: '#010203', line_style: 'double' };
        expect(sanitize_options(JSON.parse(JSON.stringify(d)))).toEqual(d);
    });
});
