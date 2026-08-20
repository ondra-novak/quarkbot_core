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
