import type { Fill } from '../types/report'
import type { LineData, Time } from 'lightweight-charts'

export function computeEquityCurve(fills: Fill[]): LineData[] {
  if (fills.length === 0) return []

  let position = 0
  let acb = 0
  let realizedPnl = 0
  const result: LineData[] = []

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

    // Deduplicate same-second fills: overwrite last point
    const last = result[result.length - 1]
    if (last && (last.time as number) === fill.time) {
      last.value = realizedPnl
    } else {
      result.push({ time: fill.time as Time, value: realizedPnl })
    }
  }

  return result
}
