# Backtest Visualizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a browser-based OHLC chart visualizer for `JsonReport` JSONL output, with fill markers and a statistics panel, launchable as `node src/viewer/server.js /path/to/report.jsonl`.

**Architecture:** A dumb Node.js HTTP server serves the static Vue app build and streams the raw JSONL file. A Web Worker in the browser streams and parses the JSONL, sends progressive updates to the main thread (meta and candles first, fills as they appear, analytics at the end), so the chart renders immediately while statistics load.

**Tech Stack:** Vue 3, TypeScript, Vite, lightweight-charts (TradingView), Vitest, pure Node.js `http` (no Express).

**Spec:** `docs/superpowers/specs/2026-08-20-backtest-visualizer-design.md`

---

## File Map

| File | Responsibility |
|------|---------------|
| `src/viewer/server.js` | HTTP server: serves `dist/` + streams JSONL via `GET /api/report` |
| `src/viewer/package.json` | Dependencies and scripts |
| `src/viewer/vite.config.ts` | Vite config with worker support and dev proxy |
| `src/viewer/tsconfig.json` | TypeScript config |
| `src/viewer/index.html` | HTML entry point with dark background |
| `src/viewer/src/main.ts` | Vue app mount |
| `src/viewer/src/App.vue` | Root component: worker setup, global state, layout |
| `src/viewer/src/types/report.ts` | All TypeScript interfaces and union types |
| `src/viewer/src/utils/aggregator.ts` | Pure: OHLC timeframe merge + fill grouping per candle |
| `src/viewer/src/utils/aggregator.test.ts` | Vitest tests for aggregator |
| `src/viewer/src/utils/analytics.ts` | Pure: P&L curve, ACB, MAE/MFE, drawdown, win rate, margin |
| `src/viewer/src/utils/analytics.test.ts` | Vitest tests for analytics |
| `src/viewer/src/workers/parser.worker.ts` | Web Worker: streaming JSONL parse → computes stats → posts messages |
| `src/viewer/src/components/TopBar.vue` | Instrument selector, timeframe buttons, progress bar |
| `src/viewer/src/components/ChartView.vue` | lightweight-charts wrapper + fill marker rendering |
| `src/viewer/src/components/StatsPanel.vue` | Right panel: quick stats + analytics with skeleton loaders |
| `src/viewer/src/components/FillDetail.vue` | Popup listing fills within a grouped marker |

---

## Task 1: Project Scaffold

**Files:**
- Create: `src/viewer/package.json`
- Create: `src/viewer/vite.config.ts`
- Create: `src/viewer/tsconfig.json`
- Create: `src/viewer/index.html`

- [ ] **Step 1: Create `src/viewer/package.json`**

```json
{
  "name": "quarkbot-viewer",
  "version": "0.1.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "vue-tsc --noEmit && vite build",
    "test": "vitest run",
    "test:watch": "vitest"
  },
  "dependencies": {
    "vue": "^3.5.0",
    "lightweight-charts": "^4.2.0"
  },
  "devDependencies": {
    "@vitejs/plugin-vue": "^5.0.0",
    "@vue/test-utils": "^2.4.0",
    "typescript": "^5.4.0",
    "vite": "^5.2.0",
    "vitest": "^1.6.0",
    "vue-tsc": "^2.0.0"
  }
}
```

- [ ] **Step 2: Create `src/viewer/vite.config.ts`**

```typescript
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  worker: { format: 'es' },
  test: {
    environment: 'node',
  },
  server: {
    proxy: {
      '/api': {
        target: 'http://localhost:3001',
        changeOrigin: true,
      },
    },
  },
})
```

- [ ] **Step 3: Create `src/viewer/tsconfig.json`**

```json
{
  "compilerOptions": {
    "target": "ESNext",
    "useDefineForClassFields": true,
    "module": "ESNext",
    "moduleResolution": "bundler",
    "strict": true,
    "jsx": "preserve",
    "lib": ["ESNext", "DOM", "DOM.Iterable"],
    "skipLibCheck": true,
    "resolveJsonModule": true
  },
  "include": ["src"]
}
```

- [ ] **Step 4: Create `src/viewer/index.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>QuarkBot Report Viewer</title>
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    html, body, #app { height: 100%; background: #131722; color: #d1d4dc; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; font-size: 13px; }
  </style>
</head>
<body>
  <div id="app"></div>
  <script type="module" src="/src/main.ts"></script>
</body>
</html>
```

- [ ] **Step 5: Install dependencies**

```bash
cd src/viewer && npm install
```

Expected: `node_modules/` created, no errors.

- [ ] **Step 6: Verify dev server starts**

```bash
cd src/viewer && npm run dev
```

Expected: Vite prints `Local: http://localhost:5173/` (the page will be blank — that's fine, `src/main.ts` doesn't exist yet). Press Ctrl+C.

- [ ] **Step 7: Commit**

```bash
git add src/viewer/package.json src/viewer/package-lock.json src/viewer/vite.config.ts src/viewer/tsconfig.json src/viewer/index.html
git commit -m "feat(viewer): scaffold vue/vite project"
```

---

## Task 2: TypeScript Types

**Files:**
- Create: `src/viewer/src/types/report.ts`

- [ ] **Step 1: Create `src/viewer/src/types/report.ts`**

```typescript
export interface InstrumentMeta {
  name: string
  leverage: number
  multiplier: number
  type: string
  tickScale: number
}

export interface Candle {
  time: number    // unix seconds (bar close time)
  open: number
  high: number
  low: number
  close: number
  volume: number
}

export interface Fill {
  time: number    // unix seconds
  orderId: string
  side: 'BUY' | 'SELL'
  price: number
  qty: number
  reason: string
  label: string
}

export interface FillStatsEntry {
  orderId: string
  filled: number
  turnover: number
  fees: number
  feesNative: number
}

export interface Stats {
  fillCount: number
  buyCount: number
  sellCount: number
  totalTurnover: number
  totalFees: number
  netQty: number
  maxProfit: number
  maxDrawdown: number
  profitDrawdownRatio: number  // maxProfit / maxDrawdown
  maxMargin: number
  winRate: number              // 0..1
  mae: number                  // worst-case Maximum Adverse Excursion (price units)
  mfe: number                  // worst-case Maximum Favorable Excursion (price units)
  dateFrom: number             // unix seconds of first fill
  dateTo: number               // unix seconds of last fill
}

export interface GroupedFill {
  candleTime: number  // close time of the candle this group belongs to
  fills: Fill[]
}

export type WorkerMessage =
  | { type: 'meta'; instruments: InstrumentMeta[]; baseInterval: number }
  | { type: 'candles'; instrument: string; data: Candle[] }
  | { type: 'fills'; instrument: string; data: Fill[] }
  | { type: 'progress'; percent: number }
  | { type: 'stats'; instrument: string; data: Stats }
  | { type: 'error'; message: string }
```

- [ ] **Step 2: Commit**

```bash
git add src/viewer/src/types/report.ts
git commit -m "feat(viewer): add TypeScript type definitions"
```

---

## Task 3: HTTP Server

**Files:**
- Create: `src/viewer/server.js`

- [ ] **Step 1: Create `src/viewer/server.js`**

```javascript
#!/usr/bin/env node
import http from 'http'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const distDir = path.join(__dirname, 'dist')

const args = process.argv.slice(2)
const reportPath = args.find(a => !a.startsWith('--'))
const portArg = args.find(a => a.startsWith('--port='))
const port = portArg ? parseInt(portArg.split('=')[1]) : 3000

if (!reportPath) {
  console.error('Usage: node server.js <path-to-report.jsonl> [--port=3000]')
  process.exit(1)
}

if (!fs.existsSync(reportPath)) {
  console.error(`File not found: ${reportPath}`)
  process.exit(1)
}

const MIME = {
  '.html': 'text/html',
  '.js': 'application/javascript',
  '.css': 'text/css',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
}

const server = http.createServer((req, res) => {
  if (req.url === '/api/report') {
    const stat = fs.statSync(reportPath)
    res.writeHead(200, {
      'Content-Type': 'text/plain; charset=utf-8',
      'Content-Length': stat.size,
      'Cache-Control': 'no-cache',
    })
    fs.createReadStream(reportPath).pipe(res)
    return
  }

  const urlPath = req.url?.split('?')[0] ?? '/'
  const filePath = path.join(distDir, urlPath === '/' ? 'index.html' : urlPath)
  const ext = path.extname(filePath)

  if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
    res.writeHead(200, { 'Content-Type': MIME[ext] ?? 'application/octet-stream' })
    fs.createReadStream(filePath).pipe(res)
  } else {
    // SPA fallback — serve index.html for client-side routing
    const indexPath = path.join(distDir, 'index.html')
    if (fs.existsSync(indexPath)) {
      res.writeHead(200, { 'Content-Type': 'text/html' })
      fs.createReadStream(indexPath).pipe(res)
    } else {
      res.writeHead(404)
      res.end('Build not found. Run: npm run build')
    }
  }
})

server.listen(port, () => {
  console.log(`QuarkBot Report Viewer → http://localhost:${port}`)
  console.log(`Report: ${path.resolve(reportPath)}`)
})
```

- [ ] **Step 2: Verify server starts and serves the report**

```bash
cd src/viewer
node server.js ../../report.jsonl --port=3001 &
curl -s http://localhost:3001/api/report | head -c 200
kill %1
```

Expected: First bytes of the JSONL file printed, no errors.

- [ ] **Step 3: Commit**

```bash
git add src/viewer/server.js
git commit -m "feat(viewer): add http server streaming jsonl via /api/report"
```

---

## Task 4: Candle Aggregator (TDD)

**Files:**
- Create: `src/viewer/src/utils/aggregator.ts`
- Create: `src/viewer/src/utils/aggregator.test.ts`

- [ ] **Step 1: Write failing tests — `src/viewer/src/utils/aggregator.test.ts`**

```typescript
import { describe, it, expect } from 'vitest'
import { aggregateCandles, groupFillsByCandle } from './aggregator'
import type { Candle, Fill } from '../types/report'

const c = (time: number, o: number, h: number, l: number, cl: number, v = 1): Candle =>
  ({ time, open: o, high: h, low: l, close: cl, volume: v })

const f = (time: number, side: 'BUY' | 'SELL', price: number): Fill =>
  ({ time, orderId: `${time}`, side, price, qty: 1, reason: '', label: '' })

describe('aggregateCandles', () => {
  it('returns original candles for factor 1', () => {
    const candles = [c(60, 10, 12, 9, 11)]
    expect(aggregateCandles(candles, 1)).toEqual(candles)
  })

  it('merges 2 candles into 1', () => {
    const candles = [c(60, 10, 12, 9, 11, 5), c(120, 11, 14, 10, 13, 3)]
    expect(aggregateCandles(candles, 2)).toEqual([
      c(120, 10, 14, 9, 13, 8),
    ])
  })

  it('uses first open and last close', () => {
    const candles = [c(60, 5, 6, 4, 6), c(120, 6, 8, 5, 7)]
    const [merged] = aggregateCandles(candles, 2)
    expect(merged.open).toBe(5)
    expect(merged.close).toBe(7)
  })

  it('handles incomplete last group', () => {
    const candles = [c(60, 1, 2, 0, 2), c(120, 2, 3, 1, 3), c(180, 3, 4, 2, 4)]
    const result = aggregateCandles(candles, 2)
    expect(result).toHaveLength(2)
    expect(result[0].time).toBe(120)
    expect(result[1].time).toBe(180)
    expect(result[1].open).toBe(3)
  })

  it('returns empty for empty input', () => {
    expect(aggregateCandles([], 3)).toEqual([])
  })
})

describe('groupFillsByCandle', () => {
  it('assigns fill to the first candle whose time >= fill.time', () => {
    const candles = [c(60, 10, 12, 9, 11), c(120, 11, 14, 10, 13)]
    const result = groupFillsByCandle([f(55, 'BUY', 10)], candles)
    expect(result).toHaveLength(1)
    expect(result[0].candleTime).toBe(60)
  })

  it('groups multiple fills into the same candle', () => {
    const candles = [c(60, 10, 12, 9, 11)]
    const result = groupFillsByCandle([f(10, 'BUY', 10), f(30, 'SELL', 11)], candles)
    expect(result).toHaveLength(1)
    expect(result[0].fills).toHaveLength(2)
  })

  it('spreads fills across multiple candles', () => {
    const candles = [c(60, 10, 12, 9, 11), c(120, 11, 14, 10, 13)]
    const result = groupFillsByCandle([f(10, 'BUY', 10), f(90, 'SELL', 12)], candles)
    expect(result).toHaveLength(2)
    expect(result[0].candleTime).toBe(60)
    expect(result[1].candleTime).toBe(120)
  })

  it('ignores fills after the last candle', () => {
    const candles = [c(60, 10, 12, 9, 11)]
    const result = groupFillsByCandle([f(200, 'BUY', 10)], candles)
    expect(result).toHaveLength(0)
  })

  it('returns empty for empty inputs', () => {
    expect(groupFillsByCandle([], [])).toEqual([])
  })
})
```

- [ ] **Step 2: Run tests — confirm they fail**

```bash
cd src/viewer && npm test
```

Expected: FAIL — `aggregator.ts` does not exist.

- [ ] **Step 3: Implement `src/viewer/src/utils/aggregator.ts`**

```typescript
import type { Candle, Fill, GroupedFill } from '../types/report'

export function aggregateCandles(candles: Candle[], factor: number): Candle[] {
  if (factor <= 1) return candles
  const result: Candle[] = []
  for (let i = 0; i < candles.length; i += factor) {
    const slice = candles.slice(i, i + factor)
    result.push({
      time: slice[slice.length - 1].time,
      open: slice[0].open,
      high: Math.max(...slice.map(c => c.high)),
      low: Math.min(...slice.map(c => c.low)),
      close: slice[slice.length - 1].close,
      volume: slice.reduce((s, c) => s + c.volume, 0),
    })
  }
  return result
}

// Maps each fill to the candle whose close time is the first >= fill.time.
export function groupFillsByCandle(fills: Fill[], candles: Candle[]): GroupedFill[] {
  if (candles.length === 0 || fills.length === 0) return []
  const groups = new Map<number, Fill[]>()
  for (const fill of fills) {
    const candle = candles.find(c => c.time >= fill.time)
    if (!candle) continue
    const bucket = groups.get(candle.time) ?? []
    bucket.push(fill)
    groups.set(candle.time, bucket)
  }
  return [...groups.entries()].map(([candleTime, fills]) => ({ candleTime, fills }))
}
```

- [ ] **Step 4: Run tests — confirm they pass**

```bash
cd src/viewer && npm test
```

Expected: All 10 aggregator tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/viewer/src/utils/aggregator.ts src/viewer/src/utils/aggregator.test.ts
git commit -m "feat(viewer): add candle aggregator and fill grouping utilities"
```

---

## Task 5: Analytics Computation (TDD)

**Files:**
- Create: `src/viewer/src/utils/analytics.ts`
- Create: `src/viewer/src/utils/analytics.test.ts`

- [ ] **Step 1: Write failing tests — `src/viewer/src/utils/analytics.test.ts`**

```typescript
import { describe, it, expect } from 'vitest'
import { computeStats } from './analytics'
import type { Fill, Candle, InstrumentMeta, FillStatsEntry } from '../types/report'

const meta: InstrumentMeta = { name: 'TEST', leverage: 1, multiplier: 1, type: 'spot', tickScale: 1 }
const noStats = new Map<string, FillStatsEntry>()

const f = (time: number, side: 'BUY' | 'SELL', price: number, qty = 1): Fill =>
  ({ time, orderId: `${time}-${side}`, side, price, qty, reason: '', label: '' })

const c = (time: number, h: number, l: number): Candle =>
  ({ time, open: h, high: h, low: l, close: l, volume: 1 })

describe('computeStats', () => {
  it('returns zero stats for no fills', () => {
    const s = computeStats([], [], noStats, meta)
    expect(s.fillCount).toBe(0)
    expect(s.winRate).toBe(0)
    expect(s.maxDrawdown).toBe(0)
  })

  it('counts buy and sell fills', () => {
    const s = computeStats([f(1, 'BUY', 100), f(2, 'SELL', 110)], [], noStats, meta)
    expect(s.buyCount).toBe(1)
    expect(s.sellCount).toBe(1)
    expect(s.fillCount).toBe(2)
  })

  it('win rate = 1 for a profitable long trade', () => {
    // BUY @ 100, SELL @ 110 → +10
    const s = computeStats([f(1, 'BUY', 100), f(2, 'SELL', 110)], [], noStats, meta)
    expect(s.winRate).toBe(1)
  })

  it('win rate = 0 for a losing long trade', () => {
    // BUY @ 100, SELL @ 90 → -10
    const s = computeStats([f(1, 'BUY', 100), f(2, 'SELL', 90)], [], noStats, meta)
    expect(s.winRate).toBe(0)
  })

  it('computes win rate across multiple trades', () => {
    // Trade 1: BUY 100 → SELL 110 (+10, win)
    // Trade 2: BUY 105 → SELL 100 (-5, loss)
    const fills = [f(1, 'BUY', 100), f(2, 'SELL', 110), f(3, 'BUY', 105), f(4, 'SELL', 100)]
    const s = computeStats(fills, [], noStats, meta)
    expect(s.winRate).toBeCloseTo(0.5)
  })

  it('computes maxProfit and maxDrawdown', () => {
    // Trade 1: BUY 100 → SELL 120 (+20)
    // Trade 2: BUY 110 → SELL 105 (-5) → drawdown from 20 peak
    const fills = [f(1, 'BUY', 100), f(2, 'SELL', 120), f(3, 'BUY', 110), f(4, 'SELL', 105)]
    const s = computeStats(fills, [], noStats, meta)
    expect(s.maxProfit).toBeCloseTo(20)
    expect(s.maxDrawdown).toBeCloseTo(5)
    expect(s.profitDrawdownRatio).toBeCloseTo(4)
  })

  it('computes net open position', () => {
    // BUY 2, SELL 1 → net +1
    const fills = [f(1, 'BUY', 100, 2), f(2, 'SELL', 110, 1)]
    const s = computeStats(fills, [], noStats, meta)
    expect(s.netQty).toBeCloseTo(1)
  })

  it('computes MAE for a long trade using candle lows', () => {
    // BUY @ 100 at time 60, SELL @ 110 at time 180
    // Bar at time 120 has low 92 → adverse = 100 - 92 = 8
    const fills = [f(60, 'BUY', 100), f(180, 'SELL', 110)]
    const candles = [c(60, 105, 98), c(120, 108, 92), c(180, 112, 108)]
    const s = computeStats(fills, candles, noStats, meta)
    expect(s.mae).toBeCloseTo(8)  // 100 - 92
  })

  it('computes MFE for a long trade using candle highs', () => {
    // BUY @ 100, best high = 115
    const fills = [f(60, 'BUY', 100), f(180, 'SELL', 110)]
    const candles = [c(60, 105, 98), c(120, 115, 105), c(180, 112, 108)]
    const s = computeStats(fills, candles, noStats, meta)
    expect(s.mfe).toBeCloseTo(15)  // 115 - 100
  })

  it('computes max margin from leverage and position size', () => {
    // BUY 1 @ 100, leverage 10 → margin = 1 * 100 / 10 = 10
    const metaLev: InstrumentMeta = { ...meta, leverage: 10 }
    const s = computeStats([f(1, 'BUY', 100)], [], noStats, metaLev)
    expect(s.maxMargin).toBeCloseTo(10)
  })

  it('sums turnover and fees from fillStatsMap', () => {
    const statsMap = new Map<string, FillStatsEntry>([
      ['ord1', { orderId: 'ord1', filled: 1, turnover: 500, fees: 0.5, feesNative: 0.005 }],
      ['ord2', { orderId: 'ord2', filled: 1, turnover: 300, fees: 0.3, feesNative: 0.003 }],
    ])
    const s = computeStats([f(1, 'BUY', 100)], [], statsMap, meta)
    expect(s.totalTurnover).toBeCloseTo(800)
    expect(s.totalFees).toBeCloseTo(0.8)
  })
})
```

- [ ] **Step 2: Run tests — confirm they fail**

```bash
cd src/viewer && npm test
```

Expected: FAIL — `analytics.ts` does not exist.

- [ ] **Step 3: Implement `src/viewer/src/utils/analytics.ts`**

```typescript
import type { Fill, Candle, Stats, InstrumentMeta, FillStatsEntry } from '../types/report'

export function computeStats(
  fills: Fill[],
  candles: Candle[],
  fillStatsMap: Map<string, FillStatsEntry>,
  meta: InstrumentMeta,
): Stats {
  if (fills.length === 0) return emptyStats()

  let totalTurnover = 0
  let totalFees = 0
  for (const fs of fillStatsMap.values()) {
    totalTurnover += fs.turnover
    totalFees += fs.fees
  }

  let position = 0   // signed (positive = long, negative = short)
  let acb = 0        // average cost basis
  let realizedPnl = 0
  let peakPnl = 0
  let maxProfit = 0
  let maxDrawdown = 0
  let buyCount = 0
  let sellCount = 0

  interface ClosedTrade {
    pnl: number
    entryTime: number
    exitTime: number
    entryPrice: number
    entrySide: 'LONG' | 'SHORT'
  }
  const closedTrades: ClosedTrade[] = []
  let currentEntry: { time: number; price: number; side: 'LONG' | 'SHORT' } | null = null

  // Margin tracking (parallel pass to avoid interfering with acb)
  let mPos = 0
  let mAcb = 0
  let maxMargin = 0
  const leverage = meta.leverage > 0 ? meta.leverage : 1

  for (const fill of fills) {
    const isBuy = fill.side === 'BUY'
    isBuy ? buyCount++ : sellCount++
    const qty = fill.qty
    const price = fill.price
    const signedQty = isBuy ? qty : -qty
    const newPosition = position + signedQty

    if (position === 0) {
      acb = price
      currentEntry = { time: fill.time, price, side: isBuy ? 'LONG' : 'SHORT' }
    } else if (Math.sign(position) === Math.sign(newPosition)) {
      // Scale into position: update ACB
      acb = (Math.abs(position) * acb + qty * price) / Math.abs(newPosition)
    } else {
      // Closing or reversing
      const closedQty = Math.min(Math.abs(position), qty)
      const tradePnl = position > 0
        ? (price - acb) * closedQty
        : (acb - price) * closedQty
      realizedPnl += tradePnl
      if (currentEntry) {
        closedTrades.push({ pnl: tradePnl, entryTime: currentEntry.time, exitTime: fill.time, entryPrice: currentEntry.price, entrySide: currentEntry.side })
        currentEntry = null
      }
      if (Math.abs(newPosition) > 0) {
        acb = price
        currentEntry = { time: fill.time, price, side: newPosition > 0 ? 'LONG' : 'SHORT' }
      }
    }
    position = newPosition

    if (realizedPnl > peakPnl) peakPnl = realizedPnl
    if (realizedPnl > maxProfit) maxProfit = realizedPnl
    const dd = peakPnl - realizedPnl
    if (dd > maxDrawdown) maxDrawdown = dd

    // Margin
    const mNew = mPos + signedQty
    if (mPos === 0) {
      mAcb = price
    } else if (Math.sign(mPos) === Math.sign(mNew)) {
      mAcb = (Math.abs(mPos) * mAcb + qty * price) / Math.abs(mNew)
    } else {
      mAcb = Math.abs(mNew) > 0 ? price : 0
    }
    mPos = mNew
    const margin = (Math.abs(mPos) * (mAcb || price)) / leverage
    if (margin > maxMargin) maxMargin = margin
  }

  const wins = closedTrades.filter(t => t.pnl > 0).length
  const winRate = closedTrades.length > 0 ? wins / closedTrades.length : 0

  let worstMae = 0
  let bestMfe = 0
  for (const trade of closedTrades) {
    for (const bar of candles) {
      if (bar.time < trade.entryTime || bar.time > trade.exitTime) continue
      const adverse = trade.entrySide === 'LONG' ? trade.entryPrice - bar.low : bar.high - trade.entryPrice
      const favorable = trade.entrySide === 'LONG' ? bar.high - trade.entryPrice : trade.entryPrice - bar.low
      if (adverse > worstMae) worstMae = adverse
      if (favorable > bestMfe) bestMfe = favorable
    }
  }

  return {
    fillCount: fills.length,
    buyCount,
    sellCount,
    totalTurnover,
    totalFees,
    netQty: position,
    maxProfit,
    maxDrawdown,
    profitDrawdownRatio: maxDrawdown > 0 ? maxProfit / maxDrawdown : 0,
    maxMargin,
    winRate,
    mae: worstMae,
    mfe: bestMfe,
    dateFrom: fills[0].time,
    dateTo: fills[fills.length - 1].time,
  }
}

function emptyStats(): Stats {
  return {
    fillCount: 0, buyCount: 0, sellCount: 0,
    totalTurnover: 0, totalFees: 0, netQty: 0,
    maxProfit: 0, maxDrawdown: 0, profitDrawdownRatio: 0,
    maxMargin: 0, winRate: 0, mae: 0, mfe: 0,
    dateFrom: 0, dateTo: 0,
  }
}
```

- [ ] **Step 4: Run tests — confirm they all pass**

```bash
cd src/viewer && npm test
```

Expected: All 22 tests (10 aggregator + 12 analytics) PASS.

- [ ] **Step 5: Commit**

```bash
git add src/viewer/src/utils/analytics.ts src/viewer/src/utils/analytics.test.ts
git commit -m "feat(viewer): add P&L analytics computation (MAE/MFE, drawdown, win rate)"
```

---

## Task 6: Parser Web Worker

**Files:**
- Create: `src/viewer/src/workers/parser.worker.ts`

The worker is initialized by the main thread with `{ url: '/api/report', contentLength: number }`. It fetches the URL, reads line by line, and posts progressive `WorkerMessage` updates.

- [ ] **Step 1: Create `src/viewer/src/workers/parser.worker.ts`**

```typescript
import type { WorkerMessage, InstrumentMeta, Candle, Fill, FillStatsEntry } from '../types/report'
import { computeStats } from '../utils/analytics'

const CANDLE_CHUNK = 1000
const FILL_CHUNK = 500

self.onmessage = async (e: MessageEvent<{ url: string; contentLength: number }>) => {
  const { url, contentLength } = e.data
  try {
    await run(url, contentLength)
  } catch (err) {
    post({ type: 'error', message: String(err) })
  }
}

function post(msg: WorkerMessage) {
  self.postMessage(msg)
}

async function run(url: string, contentLength: number) {
  const response = await fetch(url)
  if (!response.ok || !response.body) throw new Error(`HTTP ${response.status}`)

  const instruments = new Map<string, InstrumentMeta>()
  let baseInterval = 1
  let metaSent = false

  const candleBuf = new Map<string, Candle[]>()    // flush buffer
  const allCandles = new Map<string, Candle[]>()   // kept for analytics
  const fillBuf = new Map<string, Fill[]>()
  const allFills = new Map<string, Fill[]>()
  const fillStatsMap = new Map<string, FillStatsEntry>()

  let bytesRead = 0
  let remainder = ''
  const decoder = new TextDecoder()
  const reader = response.body.getReader()

  while (true) {
    const { done, value } = await reader.read()
    if (done) break

    bytesRead += value.byteLength
    const text = remainder + decoder.decode(value, { stream: true })
    const lines = text.split('\n')
    remainder = lines.pop() ?? ''

    for (const line of lines) {
      if (line.trim()) parseLine(line)
    }

    if (contentLength > 0) {
      post({ type: 'progress', percent: Math.min(99, Math.round((bytesRead / contentLength) * 100)) })
    }

    // Send meta as soon as we have seen at least one instrument and the interval
    if (!metaSent && instruments.size > 0) {
      post({ type: 'meta', instruments: [...instruments.values()], baseInterval })
      metaSent = true
    }

    // Flush full chunks
    for (const [instr, buf] of candleBuf) {
      if (buf.length >= CANDLE_CHUNK) {
        post({ type: 'candles', instrument: instr, data: buf.splice(0) })
      }
    }
    for (const [instr, buf] of fillBuf) {
      if (buf.length >= FILL_CHUNK) {
        post({ type: 'fills', instrument: instr, data: buf.splice(0) })
      }
    }
  }

  // Final line
  if (remainder.trim()) parseLine(remainder)

  // Flush remaining partial chunks
  for (const [instr, buf] of candleBuf) {
    if (buf.length > 0) post({ type: 'candles', instrument: instr, data: buf })
  }
  for (const [instr, buf] of fillBuf) {
    if (buf.length > 0) post({ type: 'fills', instrument: instr, data: buf })
  }

  post({ type: 'progress', percent: 100 })

  // Compute analytics per instrument
  for (const [instr, meta] of instruments) {
    const candles = allCandles.get(instr) ?? []
    const fills = allFills.get(instr) ?? []
    const instrStats = new Map(
      [...fillStatsMap.entries()].filter(([, v]) => {
        // fill_stats don't carry instrument name directly; use all of them for the single instrument case.
        // For multi-instrument files this would need to be partitioned by matching order IDs.
        return true
      })
    )
    const stats = computeStats(fills, candles, instrStats, meta)
    post({ type: 'stats', instrument: instr, data: stats })
  }

  function parseLine(line: string) {
    let parsed: [number, number, string, unknown]
    try { parsed = JSON.parse(line) } catch { return }
    const [sec, , ev, payload] = parsed

    if (ev === 'I') {
      const p = payload as { name: string; leverage: number; multiplier: number; type: string; tick_scale: number }
      instruments.set(p.name, { name: p.name, leverage: p.leverage, multiplier: p.multiplier, type: p.type, tickScale: p.tick_scale })
      candleBuf.set(p.name, [])
      allCandles.set(p.name, [])
      fillBuf.set(p.name, [])
      allFills.set(p.name, [])
    } else if (ev === 'C') {
      baseInterval = (payload as { interval: number }).interval
    } else if (ev === 'c') {
      const [name, open, high, low, close, volume] = payload as [string, number, number, number, number, number]
      const candle: Candle = { time: sec, open, high, low, close, volume }
      candleBuf.get(name)?.push(candle)
      allCandles.get(name)?.push(candle)
    } else if (ev === 'f') {
      const p = payload as { instrument: string; order_id: string; price: number; quantity: number; side: string; reason: string; label: string }
      const fill: Fill = { time: sec, orderId: p.order_id, side: p.side.toUpperCase() as 'BUY' | 'SELL', price: p.price, qty: p.quantity, reason: p.reason, label: p.label }
      fillBuf.get(p.instrument)?.push(fill)
      allFills.get(p.instrument)?.push(fill)
    } else if (ev === 's') {
      const p = payload as { order_id: string; filled: number; turnover: number; fees: number; fees_native: number }
      fillStatsMap.set(p.order_id, { orderId: p.order_id, filled: p.filled, turnover: p.turnover, fees: p.fees, feesNative: p.fees_native })
    }
    // 'v' (var_update) and 'o' (order_status) are ignored in phase 1
  }
}
```

- [ ] **Step 2: Commit**

```bash
git add src/viewer/src/workers/parser.worker.ts
git commit -m "feat(viewer): add streaming JSONL parser web worker"
```

---

## Task 7: App.vue + main.ts — Worker Wiring and State

**Files:**
- Create: `src/viewer/src/main.ts`
- Create: `src/viewer/src/App.vue`

- [ ] **Step 1: Create `src/viewer/src/main.ts`**

```typescript
import { createApp } from 'vue'
import App from './App.vue'

createApp(App).mount('#app')
```

- [ ] **Step 2: Create `src/viewer/src/App.vue`**

```vue
<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import type { InstrumentMeta, Candle, Fill, Stats, WorkerMessage } from './types/report'
import ParserWorker from './workers/parser.worker?worker'
import TopBar from './components/TopBar.vue'
import ChartView from './components/ChartView.vue'
import StatsPanel from './components/StatsPanel.vue'
import FillDetail from './components/FillDetail.vue'
import type { GroupedFill } from './types/report'

// --- State ---
const instruments = ref<InstrumentMeta[]>([])
const selectedInstrument = ref('')
const baseInterval = ref(1)
const timeframeFactor = ref(1)
const progress = ref(0)
const loadingStats = ref(true)
const error = ref('')

const candlesMap = ref(new Map<string, Candle[]>())
const fillsMap = ref(new Map<string, Fill[]>())
const statsMap = ref(new Map<string, Stats>())

// Fill detail popup
const detailFills = ref<GroupedFill | null>(null)

// --- Computed for selected instrument ---
const currentCandles = computed(() => candlesMap.value.get(selectedInstrument.value) ?? [])
const currentFills = computed(() => fillsMap.value.get(selectedInstrument.value) ?? [])
const currentStats = computed(() => statsMap.value.get(selectedInstrument.value))

// --- Worker setup ---
let worker: Worker | null = null

onMounted(() => {
  worker = new ParserWorker()
  worker.onmessage = (e: MessageEvent<WorkerMessage>) => {
    const msg = e.data
    if (msg.type === 'meta') {
      instruments.value = msg.instruments
      baseInterval.value = msg.baseInterval
      if (!selectedInstrument.value && msg.instruments.length > 0) {
        selectedInstrument.value = msg.instruments[0].name
      }
      // Initialize maps for each instrument
      for (const instr of msg.instruments) {
        if (!candlesMap.value.has(instr.name)) candlesMap.value.set(instr.name, [])
        if (!fillsMap.value.has(instr.name)) fillsMap.value.set(instr.name, [])
      }
    } else if (msg.type === 'candles') {
      const existing = candlesMap.value.get(msg.instrument) ?? []
      candlesMap.value.set(msg.instrument, [...existing, ...msg.data])
      // Trigger reactivity
      candlesMap.value = new Map(candlesMap.value)
    } else if (msg.type === 'fills') {
      const existing = fillsMap.value.get(msg.instrument) ?? []
      fillsMap.value.set(msg.instrument, [...existing, ...msg.data])
      fillsMap.value = new Map(fillsMap.value)
    } else if (msg.type === 'progress') {
      progress.value = msg.percent
    } else if (msg.type === 'stats') {
      statsMap.value.set(msg.instrument, msg.data)
      statsMap.value = new Map(statsMap.value)
      loadingStats.value = false
    } else if (msg.type === 'error') {
      error.value = msg.message
    }
  }

  // Fetch Content-Length first so the worker can report accurate progress
  fetch('/api/report', { method: 'HEAD' }).then(r => {
    const contentLength = parseInt(r.headers.get('content-length') ?? '0')
    worker!.postMessage({ url: '/api/report', contentLength })
  }).catch(() => {
    worker!.postMessage({ url: '/api/report', contentLength: 0 })
  })
})

onUnmounted(() => worker?.terminate())
</script>

<template>
  <div class="app">
    <TopBar
      :instruments="instruments"
      :selected-instrument="selectedInstrument"
      :base-interval="baseInterval"
      :timeframe-factor="timeframeFactor"
      :progress="progress"
      @update:selected-instrument="selectedInstrument = $event"
      @update:timeframe-factor="timeframeFactor = $event"
    />

    <div v-if="error" class="error">{{ error }}</div>

    <div class="main" v-else>
      <ChartView
        :candles="currentCandles"
        :fills="currentFills"
        :timeframe-factor="timeframeFactor"
        @fill-clicked="detailFills = $event"
      />
      <StatsPanel
        :stats="currentStats"
        :loading="loadingStats"
        :instrument="selectedInstrument"
        :base-interval="baseInterval"
        :timeframe-factor="timeframeFactor"
      />
    </div>

    <FillDetail
      v-if="detailFills"
      :group="detailFills"
      @close="detailFills = null"
    />
  </div>
</template>

<style>
.app { display: flex; flex-direction: column; height: 100vh; }
.main { display: flex; flex: 1; overflow: hidden; }
.error { padding: 20px; color: #ef5350; }
</style>
```

- [ ] **Step 3: Verify the app compiles**

```bash
cd src/viewer && npm run dev
```

Expected: Vite starts (will show errors about missing components — that's expected, we add them next). Check the terminal for TypeScript compile errors only in `App.vue` itself; missing component imports are fine at this stage.

- [ ] **Step 4: Commit**

```bash
git add src/viewer/src/main.ts src/viewer/src/App.vue
git commit -m "feat(viewer): add App.vue with worker wiring and reactive state"
```

---

## Task 8: TopBar Component

**Files:**
- Create: `src/viewer/src/components/TopBar.vue`

- [ ] **Step 1: Create `src/viewer/src/components/TopBar.vue`**

```vue
<script setup lang="ts">
import type { InstrumentMeta } from '../types/report'

const props = defineProps<{
  instruments: InstrumentMeta[]
  selectedInstrument: string
  baseInterval: number
  timeframeFactor: number
  progress: number
}>()

const emit = defineEmits<{
  'update:selectedInstrument': [value: string]
  'update:timeframeFactor': [value: number]
}>()

const FACTORS = [1, 3, 5, 15, 60]
</script>

<template>
  <div class="topbar">
    <span class="brand">⬡ QuarkBot Viewer</span>

    <select
      v-if="instruments.length > 1"
      :value="selectedInstrument"
      @change="emit('update:selectedInstrument', ($event.target as HTMLSelectElement).value)"
      class="select"
    >
      <option v-for="instr in instruments" :key="instr.name" :value="instr.name">
        {{ instr.name }}
      </option>
    </select>
    <span v-else class="instrument-name">{{ selectedInstrument }}</span>

    <div class="tf-buttons">
      <button
        v-for="f in FACTORS"
        :key="f"
        :class="['tf-btn', { active: timeframeFactor === f }]"
        @click="emit('update:timeframeFactor', f)"
      >
        {{ f === 1 ? `${baseInterval}m` : `${baseInterval * f}m` }}
      </button>
    </div>

    <div class="progress-bar" v-if="progress < 100">
      <div class="progress-fill" :style="{ width: progress + '%' }" />
      <span class="progress-label">{{ progress }}%</span>
    </div>
  </div>
</template>

<style scoped>
.topbar {
  display: flex; align-items: center; gap: 12px;
  padding: 6px 12px; background: #1e2230; border-bottom: 1px solid #2a2e3e;
  flex-shrink: 0;
}
.brand { color: #4fc3f7; font-weight: 600; }
.instrument-name { color: #d1d4dc; font-weight: 500; }
.select { background: #2a2e3e; color: #d1d4dc; border: 1px solid #363a4a; padding: 3px 6px; border-radius: 4px; }
.tf-buttons { display: flex; gap: 4px; margin-left: 8px; }
.tf-btn {
  background: #2a2e3e; color: #787b86; border: none; padding: 3px 10px;
  border-radius: 3px; cursor: pointer; font-size: 12px;
}
.tf-btn.active, .tf-btn:hover { background: #363a4a; color: #d1d4dc; }
.progress-bar {
  margin-left: auto; position: relative; width: 140px; height: 6px;
  background: #2a2e3e; border-radius: 3px; overflow: hidden;
}
.progress-fill { height: 100%; background: #4fc3f7; transition: width 0.15s; }
.progress-label {
  position: absolute; right: -32px; top: -5px;
  font-size: 11px; color: #787b86;
}
</style>
```

- [ ] **Step 2: Commit**

```bash
git add src/viewer/src/components/TopBar.vue
git commit -m "feat(viewer): add TopBar with instrument selector and timeframe buttons"
```

---

## Task 9: ChartView Component

**Files:**
- Create: `src/viewer/src/components/ChartView.vue`

- [ ] **Step 1: Create `src/viewer/src/components/ChartView.vue`**

```vue
<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted, computed } from 'vue'
import { createChart, CrosshairMode } from 'lightweight-charts'
import type { IChartApi, ISeriesApi, SeriesMarker, Time } from 'lightweight-charts'
import type { Candle, Fill, GroupedFill } from '../types/report'
import { aggregateCandles, groupFillsByCandle } from '../utils/aggregator'

const props = defineProps<{
  candles: Candle[]
  fills: Fill[]
  timeframeFactor: number
}>()

const emit = defineEmits<{
  'fill-clicked': [group: GroupedFill]
}>()

const container = ref<HTMLDivElement | null>(null)
let chart: IChartApi | null = null
let series: ISeriesApi<'Candlestick'> | null = null

const aggregated = computed(() => aggregateCandles(props.candles, props.timeframeFactor))
const grouped = computed(() => groupFillsByCandle(props.fills, aggregated.value))

onMounted(() => {
  if (!container.value) return

  chart = createChart(container.value, {
    width: container.value.clientWidth,
    height: container.value.clientHeight,
    layout: { background: { color: '#131722' }, textColor: '#d1d4dc' },
    grid: { vertLines: { color: '#1e2231' }, horzLines: { color: '#1e2231' } },
    crosshair: { mode: CrosshairMode.Normal },
    rightPriceScale: { borderColor: '#2a2e3e' },
    timeScale: { borderColor: '#2a2e3e', timeVisible: true, secondsVisible: false },
  })

  series = chart.addCandlestickSeries({
    upColor: '#26a69a', downColor: '#ef5350',
    borderVisible: false,
    wickUpColor: '#26a69a', wickDownColor: '#ef5350',
  })

  const ro = new ResizeObserver(() => {
    if (container.value && chart) {
      chart.applyOptions({ width: container.value.clientWidth, height: container.value.clientHeight })
    }
  })
  ro.observe(container.value)
  onUnmounted(() => { ro.disconnect(); chart?.remove() })
})

// Update candles whenever aggregated data changes
watch(aggregated, (candles) => {
  if (!series) return
  series.setData(candles.map(c => ({
    time: c.time as Time,
    open: c.open, high: c.high, low: c.low, close: c.close,
  })))
  applyMarkers()
})

// Re-apply markers when fills change
watch(grouped, () => applyMarkers())

function applyMarkers() {
  if (!series) return
  const markers: SeriesMarker<Time>[] = []
  for (const group of grouped.value) {
    const hasBuy = group.fills.some(f => f.side === 'BUY')
    const hasSell = group.fills.some(f => f.side === 'SELL')
    const count = group.fills.length
    const label = count > 1 ? `${count}` : ''

    if (hasBuy) {
      markers.push({
        time: group.candleTime as Time,
        position: 'belowBar',
        color: '#26a69a',
        shape: 'arrowUp',
        text: label,
        size: 1,
      })
    }
    if (hasSell) {
      markers.push({
        time: group.candleTime as Time,
        position: 'aboveBar',
        color: '#ef5350',
        shape: 'arrowDown',
        text: label,
        size: 1,
      })
    }
  }
  // Sort by time (required by lightweight-charts)
  markers.sort((a, b) => (a.time as number) - (b.time as number))
  series.setMarkers(markers)
}

function onChartClick(e: MouseEvent) {
  if (!chart || !container.value) return
  const rect = container.value.getBoundingClientRect()
  const x = e.clientX - rect.left
  const y = e.clientY - rect.top
  const logical = chart.timeScale().coordinateToLogical(x)
  if (logical === null) return

  // Find the closest candle to click position
  const candles = aggregated.value
  if (logical < 0 || logical >= candles.length) return
  const clickedCandle = candles[Math.round(logical)]
  if (!clickedCandle) return

  const group = grouped.value.find(g => g.candleTime === clickedCandle.time)
  if (group) emit('fill-clicked', group)
}
</script>

<template>
  <div class="chart-wrap" ref="container" @click="onChartClick" />
</template>

<style scoped>
.chart-wrap { flex: 1; overflow: hidden; cursor: crosshair; }
</style>
```

- [ ] **Step 2: Commit**

```bash
git add src/viewer/src/components/ChartView.vue
git commit -m "feat(viewer): add ChartView with lightweight-charts and grouped fill markers"
```

---

## Task 10: FillDetail Popup

**Files:**
- Create: `src/viewer/src/components/FillDetail.vue`

- [ ] **Step 1: Create `src/viewer/src/components/FillDetail.vue`**

```vue
<script setup lang="ts">
import type { GroupedFill } from '../types/report'

defineProps<{ group: GroupedFill }>()
const emit = defineEmits<{ close: [] }>()

function formatTime(unix: number) {
  return new Date(unix * 1000).toISOString().replace('T', ' ').slice(0, 19) + ' UTC'
}
</script>

<template>
  <div class="overlay" @click.self="emit('close')">
    <div class="panel">
      <div class="header">
        <span>Fills in candle</span>
        <button class="close-btn" @click="emit('close')">✕</button>
      </div>
      <table class="table">
        <thead>
          <tr><th>Time</th><th>Side</th><th>Price</th><th>Qty</th><th>Reason</th></tr>
        </thead>
        <tbody>
          <tr v-for="(fill, i) in group.fills" :key="i" :class="fill.side.toLowerCase()">
            <td>{{ formatTime(fill.time) }}</td>
            <td>{{ fill.side }}</td>
            <td>{{ fill.price.toLocaleString() }}</td>
            <td>{{ fill.qty }}</td>
            <td>{{ fill.reason }}</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<style scoped>
.overlay {
  position: fixed; inset: 0; background: rgba(0,0,0,.55);
  display: flex; align-items: center; justify-content: center; z-index: 100;
}
.panel {
  background: #1e2230; border: 1px solid #363a4a; border-radius: 6px;
  min-width: 520px; max-height: 60vh; overflow-y: auto;
}
.header {
  display: flex; justify-content: space-between; align-items: center;
  padding: 10px 14px; border-bottom: 1px solid #363a4a; font-weight: 600; color: #d1d4dc;
}
.close-btn { background: none; border: none; color: #787b86; cursor: pointer; font-size: 16px; }
.close-btn:hover { color: #d1d4dc; }
.table { width: 100%; border-collapse: collapse; font-size: 12px; }
.table th { padding: 6px 14px; text-align: left; color: #787b86; border-bottom: 1px solid #2a2e3e; }
.table td { padding: 6px 14px; border-bottom: 1px solid #1e2230; }
tr.buy td { color: #26a69a; }
tr.sell td { color: #ef5350; }
</style>
```

- [ ] **Step 2: Commit**

```bash
git add src/viewer/src/components/FillDetail.vue
git commit -m "feat(viewer): add FillDetail popup for grouped fill markers"
```

---

## Task 11: StatsPanel Component

**Files:**
- Create: `src/viewer/src/components/StatsPanel.vue`

- [ ] **Step 1: Create `src/viewer/src/components/StatsPanel.vue`**

```vue
<script setup lang="ts">
import type { Stats } from '../types/report'

const props = defineProps<{
  stats: Stats | undefined
  loading: boolean
  instrument: string
  baseInterval: number
  timeframeFactor: number
}>()

function fmt(n: number, decimals = 2) {
  return n.toLocaleString(undefined, { minimumFractionDigits: decimals, maximumFractionDigits: decimals })
}

function fmtDate(unix: number) {
  if (!unix) return '—'
  return new Date(unix * 1000).toISOString().slice(0, 10)
}

function fmtPct(n: number) { return (n * 100).toFixed(1) + '%' }
</script>

<template>
  <aside class="panel">
    <div class="section-title">{{ instrument }}</div>

    <template v-if="stats">
      <!-- Quick stats (available early) -->
      <div class="section">
        <div class="row"><span class="label">Fills</span><span>{{ stats.fillCount }}</span></div>
        <div class="row">
          <span class="label">BUY / SELL</span>
          <span><span class="buy">{{ stats.buyCount }}</span> / <span class="sell">{{ stats.sellCount }}</span></span>
        </div>
        <div class="row"><span class="label">Turnover</span><span>${{ fmt(stats.totalTurnover) }}</span></div>
        <div class="row"><span class="label">Fees</span><span>${{ fmt(stats.totalFees) }}</span></div>
        <div class="row"><span class="label">Net qty</span><span :class="stats.netQty >= 0 ? 'buy' : 'sell'">{{ fmt(stats.netQty, 6) }}</span></div>
      </div>

      <div class="section-title small">Analytics</div>

      <template v-if="loading">
        <div class="skeleton" v-for="i in 8" :key="i" />
      </template>
      <template v-else>
        <div class="section">
          <div class="row"><span class="label">Max profit</span><span class="buy">${{ fmt(stats.maxProfit) }}</span></div>
          <div class="row"><span class="label">Max drawdown</span><span class="sell">${{ fmt(stats.maxDrawdown) }}</span></div>
          <div class="row"><span class="label">P/DD ratio</span><span>{{ fmt(stats.profitDrawdownRatio, 2) }}</span></div>
          <div class="row"><span class="label">Max margin</span><span>${{ fmt(stats.maxMargin) }}</span></div>
          <div class="row"><span class="label">Win rate</span><span>{{ fmtPct(stats.winRate) }}</span></div>
          <div class="row"><span class="label">MAE (worst)</span><span class="sell">{{ fmt(stats.mae) }}</span></div>
          <div class="row"><span class="label">MFE (best)</span><span class="buy">{{ fmt(stats.mfe) }}</span></div>
        </div>
        <div class="section">
          <div class="row"><span class="label">From</span><span>{{ fmtDate(stats.dateFrom) }}</span></div>
          <div class="row"><span class="label">To</span><span>{{ fmtDate(stats.dateTo) }}</span></div>
        </div>
      </template>
    </template>

    <div v-else class="loading-msg">Loading…</div>
  </aside>
</template>

<style scoped>
.panel {
  width: 220px; flex-shrink: 0; overflow-y: auto;
  background: #1a1d2e; border-left: 1px solid #2a2e3e; padding: 10px 0;
}
.section-title {
  padding: 8px 12px 4px; font-size: 11px; text-transform: uppercase;
  letter-spacing: .06em; color: #4fc3f7; font-weight: 600;
}
.section-title.small { font-size: 10px; color: #787b86; margin-top: 8px; }
.section { padding: 0 12px 8px; border-bottom: 1px solid #2a2e3e; }
.row { display: flex; justify-content: space-between; padding: 3px 0; font-size: 12px; }
.label { color: #787b86; }
.buy { color: #26a69a; }
.sell { color: #ef5350; }
.skeleton {
  height: 14px; margin: 5px 12px; border-radius: 3px;
  background: linear-gradient(90deg, #2a2e3e 25%, #363a4a 50%, #2a2e3e 75%);
  background-size: 200% 100%; animation: shimmer 1.2s infinite;
}
@keyframes shimmer { 0% { background-position: 200% 0; } 100% { background-position: -200% 0; } }
.loading-msg { padding: 12px; color: #787b86; font-size: 12px; }
</style>
```

- [ ] **Step 2: Commit**

```bash
git add src/viewer/src/components/StatsPanel.vue
git commit -m "feat(viewer): add StatsPanel with analytics and skeleton loaders"
```

---

## Task 12: Integration — Build and Test

- [ ] **Step 1: Run full test suite**

```bash
cd src/viewer && npm test
```

Expected: All tests pass (aggregator + analytics, ~22 tests).

- [ ] **Step 2: Build the Vue app**

```bash
cd src/viewer && npm run build
```

Expected: `dist/` directory created, no TypeScript errors, no build errors.

- [ ] **Step 3: Start server and open in browser**

```bash
cd src/viewer && node server.js ../../report.jsonl
```

Open `http://localhost:3000` in a browser.

Expected:
- Progress bar appears and fills while JSONL is parsed
- BTCUSD OHLC chart renders as candles arrive
- Fill markers (arrows) appear on candles
- Stats panel shows fill counts and turnover quickly, then analytics fill in after parse completes
- Timeframe buttons (5m, 15m, 25m, 75m, 300m) aggregate candles correctly
- Clicking a candle with multiple fills opens the FillDetail popup

- [ ] **Step 4: Verify timeframe switching**

Click each timeframe button and confirm the chart re-aggregates correctly. The number of candles should decrease as the factor increases. Fill markers should move to the correct aggregated candle.

- [ ] **Step 5: Verify fill detail popup**

Find a candle where multiple fills occur (check the JSONL — many fills happen at `t=1785900000`). Click that area on the chart and verify the popup lists individual fills with time, side, price, qty.

- [ ] **Step 6: Add `dist/` to gitignore and commit**

```bash
echo "src/viewer/dist/" >> .gitignore
echo "src/viewer/node_modules/" >> .gitignore
git add .gitignore
git commit -m "chore: gitignore viewer dist and node_modules"
```

- [ ] **Step 7: Final commit**

```bash
git add src/viewer/src/
git commit -m "feat(viewer): complete backtest report visualizer v1"
```

---

## Self-Review Notes

- All `WorkerMessage` union type variants are consumed in `App.vue` — coverage complete.
- `groupFillsByCandle` uses `candle.time >= fill.time` — this assigns a fill to the candle that closes at or after the fill's timestamp, matching how the report emits bars (close time = timestamp).
- The `fill_stats` (`"s"` events) do not carry an instrument field; in the current single-instrument test file this is fine. For multi-instrument files the worker notes this limitation — a future improvement would partition by matching order IDs to fills.
- `maxMargin` with `leverage=0` in the sample data: the worker falls back to `leverage=1` via `meta.leverage > 0 ? meta.leverage : 1`, so margin = full position value (conservative).
- `timeframe-factor` labels use `baseInterval * factor` and append `m` — this assumes the base interval unit is minutes. If the unit is something else, the label will be wrong but the aggregation logic will still be correct.
