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
  const fillStatsMap = new Map<string, Map<string, FillStatsEntry>>()

  let baseIntervalSeen = false
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
    if (!metaSent && instruments.size > 0 && baseIntervalSeen) {
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

  // Final line — flush TextDecoder internal buffer first
  remainder += decoder.decode()
  if (remainder.trim()) parseLine(remainder)

  // Fallback: if file has no C event, send meta now
  if (!metaSent && instruments.size > 0) {
    post({ type: 'meta', instruments: [...instruments.values()], baseInterval })
    metaSent = true
  }

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
    const instrStats = fillStatsMap.get(instr) ?? new Map()
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
      baseIntervalSeen = true
    } else if (ev === 'c') {
      const [name, open, high, low, close, volume] = payload as [string, number, number, number, number, number]
      const candle: Candle = { time: sec, open, high, low, close, volume }
      if (!candleBuf.has(name)) { console.warn(`[parser] candle for unknown instrument: ${name}`); return }
      candleBuf.get(name)!.push(candle)
      allCandles.get(name)!.push(candle)
    } else if (ev === 'f') {
      const p = payload as { instrument: string; order_id: string; price: number; quantity: number; side: string; reason: string; label: string }
      const fill: Fill = { time: sec, orderId: p.order_id, side: p.side.toUpperCase() as 'BUY' | 'SELL', price: p.price, qty: p.quantity, reason: p.reason, label: p.label }
      if (!fillBuf.has(p.instrument)) { console.warn(`[parser] fill for unknown instrument: ${p.instrument}`); return }
      fillBuf.get(p.instrument)!.push(fill)
      allFills.get(p.instrument)!.push(fill)
    } else if (ev === 's') {
      const p = payload as { instrument: string; order_id: string; filled: number; turnover: number; fees: number; fees_native: number }
      if (!fillStatsMap.has(p.instrument)) fillStatsMap.set(p.instrument, new Map())
      fillStatsMap.get(p.instrument)!.set(p.order_id, { orderId: p.order_id, filled: p.filled, turnover: p.turnover, fees: p.fees, feesNative: p.fees_native })
    }
    // 'v' (var_update) and 'o' (order_status) are ignored in phase 1
  }
}
