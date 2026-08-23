import { side_value } from '../types/constants';
import { calculate_pnl } from '../types/contract';
import type { WorkerMessage, InstrumentMeta, Candle, Fill, FillStatsEntry } from '../types/report'
import { computeStats } from '../utils/analytics'


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
  
  const candleBuf = new Map<string, Candle[]>()    // flush buffer
  const allCandles = new Map<string, Candle[]>()   // kept for analytics
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


  }

  // Final line — flush TextDecoder internal buffer first
  remainder += decoder.decode()
  if (remainder.trim()) parseLine(remainder)

  // Fallback: if file has no C event, send meta now
  if (instruments.size > 0) {
    post({ type: 'meta', instruments: [...instruments.values()], baseInterval })
  }

  // Flush remaining partial chunks
  for (const [instr, buf] of candleBuf) {
    if (buf.length > 0) post({ type: 'candles', instrument: instr, data: buf })
  }
  for (const [instr, buf] of allFills) {
    if (buf.length > 0) post({ type: 'fills', instrument: instr, data: buf })
  }

  post({ type: 'progress', percent: 100 })

  for (const [instr, buf] of allFills) {
      let val = 0;
      let pos = 0;
      let prev_close = 1;
      const instr_meta = instruments.get(instr);
      if (instr_meta) {
        const eq = buf.map(x=>{
          const sd = side_value[x.side];
          const pnl = calculate_pnl(instr_meta, prev_close, x.price, pos);
          pos = pos + sd * x.qty;
          prev_close = x.price;
          val = val + pnl;
          return [x.time, val] as [number,number];
        });
        post({type: 'equity', instrument:instr, series: eq});
        pos = 0;
        const pq = buf.map(x=>{
          const sd = side_value[x.side];
          pos = pos + sd * x.qty;
          return [x.time, pos] as [number,number];
        })
        post({type: 'position', instrument:instr, series: pq});
      }
  }

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
      if (!allFills.has(p.instrument)) { console.warn(`[parser] fill for unknown instrument: ${p.instrument}`); return }
      allFills.get(p.instrument)!.push(fill)
    } else if (ev === 's') {
      const p = payload as { instrument: string; order_id: string; filled: number; turnover: number; fees: number; fees_native: number }
      if (!fillStatsMap.has(p.instrument)) fillStatsMap.set(p.instrument, new Map())
      fillStatsMap.get(p.instrument)!.set(p.order_id, { orderId: p.order_id, filled: p.filled, turnover: p.turnover, fees: p.fees, feesNative: p.fees_native })
    }
    // 'v' (var_update) and 'o' (order_status) are ignored in phase 1
  }
}
