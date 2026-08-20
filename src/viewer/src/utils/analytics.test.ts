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
