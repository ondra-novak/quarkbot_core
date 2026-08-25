export type ContractType = "spot"|"margin"|"contract"|"inverse_contract";

export type OrderStatus = "unknown"|"sent"|"pending_trigger"|"open"|"filled"|"canceled"|"rejected"|"replaced"|"restored"|"lost";

export type Side = 'buy'|'sell';

export type OrderType =  "ALERT" |    "MARKET" |    "LIMIT" |    "LIMIT(post)" |    "STOP" |    "STOPLIMIT" |    "Unknown" ;

export interface InstrumentMeta {
  name: string
  leverage: number
  multiplier: number
  type: ContractType
  tick_scale: number
  lot: number
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
  side: Side
  price: number
  quantity: number
  reason: string
  label: string
}

export interface OrderUpdate {
    time: number;
    limit_price?:number,
    stop_price?:number,
    order_id:string,
    quantity:number,
    replaced_order_id?:string,
    side:Side,
    status:OrderStatus,
    type: OrderType    
}

export interface FillStatsEntry {
  time: number;
  orderId: string
  filled: number
  turnover: number
  fees: number
  feesNative: number
}

export interface VarUpdate {
    name?: string;
    time: number;
    revision: [number, number],
    val: any
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
  | { type: 'equity'; instrument: string; series: [number, number][]}
  | { type: 'position'; instrument: string; series: [number, number][]}



