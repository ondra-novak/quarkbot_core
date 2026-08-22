import type { Fill, Candle } from '../types/report'
import type { LineData, Time } from 'lightweight-charts'

export function computeEquityCurve(fills: Fill[], candles: Candle[]): LineData[] {
  if (candles.length === 0) return []

  // Compute ACB-based realized P&L at each fill (raw per-fill points)
  let position = 0
  let acb = 0
  let realizedPnl = 0
  const rawPoints: { time: number; value: number }[] = []

  for (const fill of fills) {
    const qty = fill.qty
    const price = fill.price
    const isBuy = fill.side === 'BUY'
    const signedQty = isBuy ? qty : -qty
    const newPosition = position + signedQty

    if (position === 0) {
      acb = price
    } else if (Math.sign(position) === Math.sign(newPosition)) {
      acb = (Math.abs(position) * acb + qty * price) / Math.abs(newPosition)
    } else {
      const closedQty = Math.min(Math.abs(position), qty)
      realizedPnl += position > 0
        ? (price - acb) * closedQty
        : (acb - price) * closedQty
      if (Math.abs(newPosition) > 0) acb = price
    }
    position = newPosition
    rawPoints.push({ time: fill.time, value: realizedPnl })
  }

  // Aggregate to candle close timestamps.
  // Candle.time is the bar CLOSE time. A fill with fill.time <= candle.time
  // belongs to this candle. Carry the last known equity forward so the chart
  // always has the same number of points as the candlestick chart — this keeps
  // logical indices aligned for time-scale synchronization.
  const result: LineData[] = []
  let fillIdx = 0
  let lastEquity = 0

  for (const candle of candles) {
    while (fillIdx < rawPoints.length && rawPoints[fillIdx].time <= candle.time) {
      lastEquity = rawPoints[fillIdx].value
      fillIdx++
    }
    result.push({ time: candle.time as Time, value: lastEquity })
  }

  return result
}
