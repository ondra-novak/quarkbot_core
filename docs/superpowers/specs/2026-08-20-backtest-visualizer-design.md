# Backtest Visualizer — Design Spec

**Date:** 2026-08-20  
**Scope:** Phase 1 — per-instrument analysis of a single JSONL report file  
**Out of scope:** multi-instrument cross-analysis, strategy variable overlays (phase 2)

---

## Overview

A browser-based visualizer for `JsonReport` JSONL output. Launched with a single command:

```bash
node src/viewer/server.js /path/to/report.jsonl
```

Opens at `http://localhost:3000`. Displays a TradingView-style OHLC chart with fill markers and an analytics panel.

---

## JSONL Format

Each line: `[unix_seconds, nanoseconds, event_char, payload]`

| Event char | Meaning         | Payload                                                              |
|------------|-----------------|----------------------------------------------------------------------|
| `I`        | instrument_info | `{name, leverage, multiplier, type, tick_scale}`                     |
| `C`        | chart_setup     | `{interval}` — base bar count (integer multiplier of data source)    |
| `c`        | OHLC bar        | `[instrument_name, open, high, low, close, volume]`                  |
| `f`        | fill            | `{id, instrument, order_id, price, quantity, side, reason, label}`   |
| `s`        | fill_stats      | `{filled, turnover, fees, fees_native, instrument, order_id, ...}`   |
| `o`        | order_status    | `{instrument, order_id, type, side, status, quantity, limit_price?}` |
| `v`        | var_update      | `{name, rev:[ordered, random], val}` — ignored in phase 1            |

`"v"` events with `val: null` are tombstones for old revisions — ignored entirely in phase 1.

---

## Architecture

### Server (`server.js`)

- Pure Node.js `http` module — zero npm dependencies
- CLI: `node server.js <path-to-report.jsonl> [--port 3000]`
- Routes:
  - `GET /` → `dist/index.html`
  - `GET /assets/*` → Vite build assets
  - `GET /api/report` → streams the JSONL file directly from disk (no buffering)

### Client (Vue 3 + TypeScript + Vite)

On load, the client fetches `/api/report` and passes the response stream to a **Web Worker** via `ReadableStream.pipeTo()`. The worker reads line by line using `TextDecoderStream`, processes events, and sends progressive messages back to the main thread.

**Development:**
```bash
npm run dev   # Vite dev server with proxy /api/* → server.js
npm run build # produces dist/
```

---

## Web Worker — Progressive Updates

The worker sends typed messages to the main thread as data becomes available:

```typescript
type WorkerMessage =
  | { type: 'meta';     instruments: InstrumentMeta[]; baseInterval: number }
  | { type: 'candles';  instrument: string; data: Candle[] }   // chunks of ~1000
  | { type: 'fills';    instrument: string; data: Fill[] }
  | { type: 'progress'; percent: number }
  | { type: 'stats';    instrument: string; data: Stats }
  | { type: 'error';    message: string }
```

**Phases:**

1. **Immediately** — `I` and `C` events appear at the top of the file: send `meta` so the UI can set up the instrument selector and an empty chart.
2. **Continuously** — send `candles` and `fills` in chunks as they are parsed; the chart renders progressively. Send `progress` percent.
3. **At end** — once the full file is parsed, compute all analytics and send `stats` per instrument.

---

## Data Model

```typescript
type Candle = {
  time: number;   // unix seconds (bar close time)
  open: number; high: number; low: number; close: number; volume: number;
}

type Fill = {
  time: number;
  orderId: string;
  side: 'BUY' | 'SELL';
  price: number;
  qty: number;
  reason: string;
  label: string;
}

type InstrumentMeta = {
  name: string;
  leverage: number;
  multiplier: number;
  type: string;
  tickScale: number;
}

type Stats = {
  fillCount: number;
  buyCount: number;
  sellCount: number;
  totalTurnover: number;
  totalFees: number;
  netQty: number;
  maxProfit: number;
  maxDrawdown: number;         // max loss after last profit peak
  profitDrawdownRatio: number; // maxProfit / maxDrawdown
  maxMargin: number;           // max(open_position_value / leverage)
  winRate: number;             // fraction of profitable closed trades (trade = position reaches 0 or reverses)
  mae: number;                 // worst-case Maximum Adverse Excursion across all trades
  mfe: number;                 // worst-case Maximum Favorable Excursion across all trades
  dateFrom: number;
  dateTo: number;
}
```

---

## Analytics Computation

All computed at end of parse from fills + OHLC data:

- **P&L curve** — reconstructed via ACB (Average Cost Basis) from fill sequence. Each fill adjusts position size and realized P&L.
- **Max profit / Max drawdown** — running peak and trough of P&L curve. Drawdown defined as maximum loss measured from the most recent P&L peak.
- **Win rate** — per closed trade (a trade closes when position returns to zero or reverses direction): fraction where realized P&L on close > 0.
- **MAE/MFE** — per trade, scan OHLC bars during the trade's lifetime for worst/best price excursion against entry. Report worst-case (max MAE, max MFE) across all trades.
- **Max margin** — `max(abs(net_position) × price / leverage)` sampled at each fill event.
- **Turnover & fees** — summed from `fill_stats` (`s`) events.

---

## UI Components

```
App.vue
├── TopBar.vue          — file name, InstrumentSelector (if >1 instrument), TimeframeSelector, progressbar
├── ChartView.vue       — lightweight-charts CandlestickSeries wrapper
│   └── fill marker logic — groups fills by candle, renders arrows with count badge
├── StatsPanel.vue      — right panel (fixed width ~220px)
│   ├── quick stats     — fill counts, turnover, fees (available early from progressive data)
│   └── analytics       — MAE/MFE, drawdown, win rate, max margin (skeleton until stats arrive)
└── FillDetail.vue      — popup on click of grouped fill marker
```

### Chart — Fill Markers

- Style: TradingView-style arrows (lightweight-charts `SeriesMarker`)
- BUY: green arrow below candle. SELL: red arrow above candle.
- **Grouping**: multiple fills within the same candle → single marker with count label (e.g. `▲3`)
- Clicking a grouped marker opens `FillDetail` popup listing each fill: time, side, price, qty, reason

### Timeframe Selector

Buttons: `1×` `3×` `5×` `15×` `60×` (multiples of base interval).  
Aggregation is client-side from already-loaded raw candles — no server roundtrip:
- `open` = first candle's open
- `high` = max of highs
- `low` = min of lows
- `close` = last candle's close
- `volume` = sum of volumes

Fill grouping recalculates on timeframe change.

### StatsPanel

Two sections:
- **Upper** (available early): fill count, buy/sell split, total turnover, total fees
- **Lower** (after full parse): win rate, max profit, max drawdown, ratio, max margin, MAE, MFE, date range

Lower section shows skeleton loaders until `stats` message arrives.

---

## Directory Structure

```
src/viewer/
├── package.json          — vue, vite, @vitejs/plugin-vue, lightweight-charts
├── vite.config.ts        — dev proxy: /api → localhost:3001
├── server.js             — pure Node.js http server
│
├── src/
│   ├── main.ts
│   ├── App.vue
│   ├── components/
│   │   ├── TopBar.vue
│   │   ├── ChartView.vue
│   │   ├── StatsPanel.vue
│   │   └── FillDetail.vue
│   ├── workers/
│   │   └── parser.worker.ts
│   ├── types/
│   │   └── report.ts
│   └── utils/
│       ├── analytics.ts       — P&L curve, MAE/MFE, drawdown, ACB, win rate
│       └── aggregator.ts      — OHLC timeframe merge
│
└── dist/                      — gitignored
```

---

## Out of Scope (Phase 1)

- Strategy variable overlays (`v` events) — phase 2: time-series viewer per variable
- Multi-instrument cross-analysis
- Order book / order status visualization
- Saving/exporting charts
