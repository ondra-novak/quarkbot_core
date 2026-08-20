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
      if (bar.time > trade.exitTime) break
      if (bar.time < trade.entryTime) continue
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
