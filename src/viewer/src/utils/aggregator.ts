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
