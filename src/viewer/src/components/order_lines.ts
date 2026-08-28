import type { CanvasRenderingTarget2D } from 'fancy-canvas';
import type {
    IChartApiBase,
    IPrimitivePaneRenderer,
    IPrimitivePaneView,
    ISeriesApi,
    ISeriesPrimitive,
    Logical,
    PrimitivePaneViewZOrder,
    SeriesAttachedParameter,
    SeriesType,
    Time,
} from 'lightweight-charts';
import type { OrderInstance } from '../types/parsed_report';

/**
 * Maximální počet úseček, který se ještě vykreslí. Nad tímto počtem se vrstva
 * skryje - při takovém zoomu by z ní byla jen nerozlišitelná kaše a překreslení
 * na každý frame by trhalo panování. Vrstva se vrátí sama po přiblížení.
 */
const MAX_VISIBLE = 50000;

/**
 * Hranice (v barech) mezi krátkými a dlouhými pokyny. Krátké se hledají binárně
 * podle počátečního baru, dlouhé se procházejí lineárně - viz select().
 */
const LONG_SPAN = 512;

/** buy/sell x limit/stop - index do STYLES */
const enum OrderStyle {
    buy_limit = 0,
    buy_stop = 1,
    sell_limit = 2,
    sell_stop = 3,
}

const STYLES: { color: string, dash: number[] }[] = [
    { color: '#26a69a80', dash: [] },      // buy limit  - zeď, solid
    { color: '#26a69a80', dash: [3, 3] },  // buy stop   - reaguje na propad, dashed
    { color: '#ef535080', dash: [] },      // sell limit
    { color: '#ef535080', dash: [3, 3] },  // sell stop
];

const STYLE_COUNT = STYLES.length;

/**
 * Skupina úseček. `x0` je vzestupně setřídněný, takže lze binárně najít začátek
 * viditelného okna; `max_span` říká, o kolik barů je nutné hledání přestřelit
 * doleva, aby se nezahodily úsečky, které začaly před oknem a končí v něm.
 */
interface Bucket {
    x0: Int32Array;     // index baru začátku, vzestupně
    x1: Int32Array;     // index baru konce
    py: Float64Array;   // cena pokynu
    st: Uint8Array;     // OrderStyle
    max_span: number;
}

function empty_bucket(): Bucket {
    return {
        x0: new Int32Array(0),
        x1: new Int32Array(0),
        py: new Float64Array(0),
        st: new Uint8Array(0),
        max_span: 0,
    };
}

/** Najde index prvního prvku s hodnotou >= t. Vrací arr.length, pokud takový není. */
function lower_bound(arr: ArrayLike<number>, len: number, t: number): number {
    let lo = 0;
    let hi = len;
    while (lo < hi) {
        const mid = (lo + hi) >>> 1;
        if (arr[mid] < t) lo = mid + 1; else hi = mid;
    }
    return lo;
}

/**
 * Vrstva krátkých vodorovných úseček znázorňujících instance pokynů - jedna
 * úsečka na pokyn, od začátku svíčky prvního po konec svíčky posledního
 * záznamu, na ceně pokynu. Slouží ke čtení "houfu" pokynů kolem svíček: kam
 * cena dosáhla a kam už ne.
 *
 * Data drží v typed arrays setřídněných podle indexu počátečního baru, takže
 * viditelné okno se vybere binárním hledáním a kreslí se jen ono. Náklady na
 * frame tedy závisí na počtu viditelných, ne celkových instancí.
 */
export class OrderLinesPrimitive implements ISeriesPrimitive<Time> {

    private series: ISeriesApi<SeriesType, Time> | null = null;
    private chart: IChartApiBase<Time> | null = null;
    private request_update: (() => void) | null = null;

    private visible = false;

    /** pokyny se spanem <= LONG_SPAN; naprostá většina */
    private short = empty_bucket();
    /** pokyny žijící přes stovky barů; vzácné, ale kazily by binární hledání */
    private long = empty_bucket();

    /** scratch buffer pro viditelné úsečky: [xa, xb, y] * n */
    private buf = new Float32Array(MAX_VISIBLE * 3);
    private buf_st = new Uint8Array(MAX_VISIBLE);
    private visible_count = 0;

    private readonly pane_view: IPrimitivePaneView = {
        zOrder: (): PrimitivePaneViewZOrder => 'normal',
        renderer: (): IPrimitivePaneRenderer | null => {
            if (!this.visible) return null;
            if (this.short.x0.length === 0 && this.long.x0.length === 0) return null;
            return { draw: (target) => this.draw(target) };
        },
    };

    private readonly views = [this.pane_view];

    attached(param: SeriesAttachedParameter<Time, SeriesType>): void {
        this.series = param.series;
        this.chart = param.chart;
        this.request_update = param.requestUpdate;
    }

    detached(): void {
        this.series = null;
        this.chart = null;
        this.request_update = null;
    }

    paneViews(): readonly IPrimitivePaneView[] {
        return this.views;
    }

    set_visible(v: boolean) {
        if (this.visible === v) return;
        this.visible = v;
        this.request_update?.();
    }

    /**
     * Přemapuje instance pokynů na indexy barů. Volat při změně reportu nebo
     * timeframe - nikoliv při panování.
     *
     * @param instances instance pokynů (čas v ms)
     * @param bar_times časy barů zobrazené řady, vzestupně, v sekundách
     * @param interval délka baru v sekundách
     */
    set_data(instances: OrderInstance[], bar_times: number[], interval: number) {
        const n = instances.length;
        const bar_count = bar_times.length;
        if (n === 0 || bar_count === 0) {
            this.short = empty_bucket();
            this.long = empty_bucket();
            this.request_update?.();
            return;
        }

        const last_bar = bar_count - 1;
        const t0 = new Int32Array(n);
        const t1 = new Int32Array(n);
        const price = new Float64Array(n);
        const style = new Uint8Array(n);
        const short_idx: number[] = [];
        const long_idx: number[] = [];

        for (let i = 0; i < n; ++i) {
            const inst = instances[i];
            // zaokrouhlení na začátek svíčky; "konec svíčky" u end řeší kreslení
            // tím, že úsečku vede k pravé hraně baru
            const bt0 = Math.floor(inst.start / (1000 * interval)) * interval;
            const bt1 = Math.floor(inst.end / (1000 * interval)) * interval;
            if (bt1 < bar_times[0] || bt0 > bar_times[last_bar]) continue;  // mimo data

            const a = lower_bound(bar_times, bar_count, bt0);
            let b = lower_bound(bar_times, bar_count, bt1);
            if (b > last_bar || bar_times[b] > bt1) --b;  // poslední bar s časem <= bt1
            if (b < a || a > last_bar) continue;          // celý pokyn padl do mezery

            const k = short_idx.length + long_idx.length;
            t0[k] = a;
            t1[k] = b;
            price[k] = inst.price;
            style[k] = inst.side === 'sell'
                ? (inst.is_stop ? OrderStyle.sell_stop : OrderStyle.sell_limit)
                : (inst.is_stop ? OrderStyle.buy_stop : OrderStyle.buy_limit);
            (b - a > LONG_SPAN ? long_idx : short_idx).push(k);
        }

        const build = (idx: number[]): Bucket => {
            // setřídit podle počátečního baru - order_instances chodí seskupené
            // podle order_id, tedy podle času nesetřídněné
            idx.sort((p, q) => t0[p] - t0[q]);
            const c = idx.length;
            const b: Bucket = {
                x0: new Int32Array(c),
                x1: new Int32Array(c),
                py: new Float64Array(c),
                st: new Uint8Array(c),
                max_span: 0,
            };
            for (let i = 0; i < c; ++i) {
                const j = idx[i];
                b.x0[i] = t0[j];
                b.x1[i] = t1[j];
                b.py[i] = price[j];
                b.st[i] = style[j];
                const span = t1[j] - t0[j];
                if (span > b.max_span) b.max_span = span;
            }
            return b;
        };

        this.short = build(short_idx);
        this.long = build(long_idx);
        this.request_update?.();
    }

    /**
     * Nasbírá viditelné úsečky ze skupiny do scratch bufferu.
     * @returns false při přetečení hard capu - vrstva se pak nekreslí vůbec
     */
    private select(bucket: Bucket, from: number, to: number,
                   i0: number, c0: number, spacing: number): boolean {
        const { x0, x1, py, st } = bucket;
        const count = x0.length;
        if (count === 0) return true;

        const series = this.series!;
        const half = spacing / 2;
        const buf = this.buf;
        const buf_st = this.buf_st;

        // začneme o max_span dřív, aby neušly úsečky začínající před oknem
        const start_at = Math.floor(from) - bucket.max_span;
        const lo = lower_bound(x0, count, start_at);

        for (let i = lo; i < count; ++i) {
            const a = x0[i];
            if (a > to) break;          // dál už jsou jen pozdější
            const b = x1[i];
            if (b < from) continue;     // skončil před oknem
            const y = series.priceToCoordinate(py[i]);
            if (y === null) continue;   // mimo cenový rozsah
            if (this.visible_count === MAX_VISIBLE) return false;
            const o = this.visible_count * 3;
            buf[o] = c0 + (a - i0) * spacing - half;
            buf[o + 1] = c0 + (b - i0) * spacing + half;
            buf[o + 2] = y;
            buf_st[this.visible_count] = st[i];
            ++this.visible_count;
        }
        return true;
    }

    private draw(target: CanvasRenderingTarget2D) {
        const series = this.series;
        const chart = this.chart;
        if (!series || !chart) return;

        const time_scale = chart.timeScale();
        const range = time_scale.getVisibleLogicalRange();
        if (!range) return;

        const from = range.from;
        const to = range.to;
        const spacing = time_scale.options().barSpacing;
        // afinní mapování logický index -> media x; logicalToCoordinate voláme
        // jednou, ne 2x na každou úsečku
        const i0 = Math.floor(from);
        const c0 = time_scale.logicalToCoordinate(i0 as Logical);
        if (c0 === null) return;

        this.visible_count = 0;
        if (!this.select(this.short, from, to, i0, c0, spacing)) return;
        if (!this.select(this.long, from, to, i0, c0, spacing)) return;

        const visible = this.visible_count;
        if (visible === 0) return;

        const buf = this.buf;
        const buf_st = this.buf_st;

        target.useBitmapCoordinateSpace(({ context: ctx, horizontalPixelRatio: hr, verticalPixelRatio: vr }) => {
            ctx.save();
            ctx.lineWidth = Math.max(1, Math.floor(vr));
            // jeden stroke na kombinaci směr x typ, ne na úsečku
            for (let s = 0; s < STYLE_COUNT; ++s) {
                let started = false;
                for (let i = 0; i < visible; ++i) {
                    if (buf_st[i] !== s) continue;
                    if (!started) {
                        ctx.beginPath();
                        started = true;
                    }
                    const o = i * 3;
                    // půl pixelu, aby tenká linka nebyla rozmazaná přes dva řádky
                    const y = Math.round(buf[o + 2] * vr) + 0.5;
                    ctx.moveTo(buf[o] * hr, y);
                    ctx.lineTo(buf[o + 1] * hr, y);
                }
                if (started) {
                    ctx.strokeStyle = STYLES[s].color;
                    ctx.setLineDash(STYLES[s].dash.map(x => x * hr));
                    ctx.stroke();
                }
            }
            ctx.restore();
        });
    }
}
