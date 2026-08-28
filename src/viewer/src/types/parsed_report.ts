import { side_value } from "./constants";
import { calculate_pnl } from "./contract";
import { Candle, Fill, OrderUpdate, VarUpdate, FillStatsEntry, InstrumentMeta, Side  } from "./report_types";

function round_to_lot(n:number, l:number) {
    return Math.round(n/l)*l;
}


export interface OrderInstance {
    id: string;
    start: number;
    end: number;
    side: Side;
    price: number;
    quantity: number;
    is_stop: boolean;
    events: OrderUpdate[];
}


export class ParsedInstrumentReport {

    info: InstrumentMeta;
    chart: Candle[] = [];
    fills: Fill[] = [];
    fill_stats : FillStatsEntry[] = [];
    eq_chart: [number,number][] = [];
    pos_chart: [number,number][] = [];
    order_updates: OrderUpdate[] = [];
    order_instances: OrderInstance[] = [];

    constructor(info:InstrumentMeta) {
        this.info = info;
    }

    recalc() {
        let pos = 0;
        let pnl = 0;
        let price = 1;
        this.pos_chart = [];
        this.eq_chart= [];
        this.fills.forEach(f=>{
            const sd = side_value[f.side];
            const dpln = calculate_pnl(this.info, price, f.price, pos);
            price = f.price;
            pos = round_to_lot(pos + sd * f.quantity,this.info.lot)
            pnl = pnl + dpln;
            this.eq_chart.push([f.time,pnl]);
            this.pos_chart.push([f.time, pos]);
        });

        const ords = this.order_updates.filter(x=>("limit_price" in x) || ("stop_price" in x));
        ords.sort((a,b)=>{
            if (a.order_id < b.order_id) return -1;
            if (a.order_id > b.order_id) return 1;
            return a.time - b.time;            
        })
        const order_sets=Object.groupBy(ords, (x)=>x.order_id);
        const ord_insts : OrderInstance[] = [];
        for (const k in order_sets) {
            const items = (order_sets[k]!);
            const ts = items.map(x=>x.time);
            ord_insts.push({
               start: Math.min(...ts),
               end: Math.min(...ts),
               id: k,
               price: items[0].stop_price ?? items[0].limit_price ?? 0,
               side: items[0].side,
               quantity: items[0].quantity,
               is_stop: !!items[0].stop_price,
               events :items
            });
        }
        this.order_instances = ord_insts;
    }
}

export class ParsedReport {

    vars = new  Map<string, VarUpdate[]>();
    instruments = new Map<string, ParsedInstrumentReport>();
    baseInterval?: number;
    start_tp?: number;
    end_tp?:number;

    static async load(stream: ReadableStream<Uint8Array<ArrayBuffer> >, progress: (x:number)=>{}) {
        let baseIntervalSeen = false
        let bytesRead = 0
        let remainder = ''
        const decoder = new TextDecoder()
        const reader = stream.getReader()

        const out = new ParsedReport;

        while (true) {
            const { done, value } = await reader.read()
            if (done) break
            bytesRead += value.byteLength;
            const text = remainder + decoder.decode(value, { stream: true })
            const lines = text.split('\n')
            remainder = lines.pop() ?? ''

            for (const line of lines) {
                if (line.trim()) parseLine(line);
            }
            progress(bytesRead);
        }

        // Final line — flush TextDecoder internal buffer first
        remainder += decoder.decode()
        if (remainder.trim()) parseLine(remainder)        

        function parseLine(line:string) {
            let parsed: [number, number, string, unknown]
            try { parsed = JSON.parse(line) } catch { return }
            const [sec,nsec , ev, payload] = parsed;
            const tp = sec * 1000.0 + nsec /1000000.0
            if (!out.start_tp) out.start_tp = tp;
            out.end_tp = tp;
            switch (ev) {
                case 'I': {
                    const p = payload as InstrumentMeta;
                    out.instruments.set(p.name,new ParsedInstrumentReport(p));
                    break;
                }
                case 'C': {
                    out.baseInterval = (payload as { interval: number }).interval;
                    break;
                }
                case 'c': {
                    const [name, open, high, low, close, volume,time] = payload as [string, number, number, number, number, number,number];
                    const candle: Candle = { time: time, open, high, low, close, volume }                    
                    const instr = out.instruments.get(name);
                    if (instr) instr.chart.push(candle);
                    break;
                }
                case 'f' :{
                    const p = payload as (Fill & {instrument: string});
                    p.time = tp;
                    const instr = out.instruments.get(p.instrument);
                    if (instr) instr.fills.push(p);
                    break;
                }
                case 's':{
                    const p = payload as (FillStatsEntry & {instrument: string});
                    p.time = tp;
                    const instr = out.instruments.get(p.instrument);
                    if (instr) instr.fill_stats.push(p);
                    break;                
                }
                case 'o':{
                    const p = payload as (OrderUpdate & {instrument:string});
                    p.time = tp;
                    const instr = out.instruments.get(p.instrument);
                    if (instr) instr.order_updates.push(p);
                    break;                                    
                }
                case 'v': {
                    const p = payload as VarUpdate;
                    if (p.name) {
                        p.time = tp;
                        let vupt = out.vars.get(p.name);
                        if (!vupt) {
                            vupt = [];
                            out.vars.set(p.name,vupt);
                        }
                        delete p.name;
                        vupt.push(p);
                    }
                    break;
                }

            }
        }

        out.recalc();


        return out;
    }

    recalc() {
        for(const [k,v] of this.instruments) {
            v.recalc();
        }
    }

    get_default_instrumnet(cur_instrument: string) {
        if (this.instruments.get(cur_instrument)) return cur_instrument;
        for (const [k,v] of this.instruments) return k;
        return "";
    }

}