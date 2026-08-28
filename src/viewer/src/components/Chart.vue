<script lang="ts" setup>
import { AreaSeries, BaselineSeries, CandlestickSeries, createChart, createSeriesMarkers, CrosshairMode, HistogramData, HistogramSeries, ISeriesMarkersPluginApi, LineSeries, LineWidth, SeriesType } from 'lightweight-charts'
import { CandlestickData, IChartApi, ISeriesApi, LineData, LineStyle, SeriesMarker, Time, UTCTimestamp } from 'lightweight-charts'
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { ParsedReport } from '../types/parsed_report';
import { DisplayOptions,  PaneType, paneTypes, QLineStyle, SeriesSetupItem } from '../types/options';
import { stringToColor } from '../types/stringToColor';
import { Side } from '../types/report_types';
import { calculate_turnover } from '../types/contract';
import { OrderLinesPrimitive } from './order_lines';

let chart:IChartApi|null = null;
let resizer :ResizeObserver | null = null;
const chartContainer = ref<HTMLElement>();
let cs_series:ISeriesApi<"Candlestick">;
let vol_series:ISeriesApi<"Histogram">;
let markers : ISeriesMarkersPluginApi<Time>|null = null;
let order_lines : OrderLinesPrimitive|null = null;

type SeriesApi = ISeriesApi<"Candlestick">|ISeriesApi<"Histogram">|ISeriesApi<"Line">|ISeriesApi<"Baseline">;
interface SeriesDef {
    api: SeriesApi,
    update:(api:SeriesApi)=>void
    type: SeriesType
};

const props = defineProps<{
    report: ParsedReport,
    options: DisplayOptions
}>();

type AllSeries = Record<PaneType, Record<string,SeriesDef>>;

let all_series : AllSeries  = paneTypes.reduce((a,b)=>{a[b] = {};return a;},{} as Record<string,any>) as AllSeries;


const current_report = computed(()=>{    
    return props.report.instruments.get(props.report.get_default_instrumnet(props.options.instrument));
})

function simple_update(source: ()=>[number, number][]|undefined) {
    return (api: SeriesApi)=>{
        const data = source();
        if (!data) {
            api.setData([]);            
        } else {
            const interval = props.options.interval*60;
            const mapped_data:LineData<Time>[] = [];
            for (const [time, value] of data) {
                const bt = Math.floor(time/(interval*1000))*interval as Time;
                const prev = mapped_data.pop();
                if (prev && prev.time != bt ) {
                    const steps = Math.round((bt as number) - (prev.time  as number))/interval;
                    for (let i = 0; i < steps; ++i) {
                        const bt2 = ((prev.time as number) + ((bt as number) - (prev.time as number)) * i / steps) as Time;
                        mapped_data.push({time:bt2, value: prev.value});
                    }
                }
                mapped_data.push({time:bt, value: value});
            }
            api.setData(mapped_data);
        }        
    }
}

const pane_index : Record<PaneType, [number, string]> = {
    main: [0,"right"],
    secondary: [0,"left"],
    pane_1: [1, "right"],
    pane_2: [2, "right"],
    pane_3: [3, "right"]
} as const;

const line_style : Record<QLineStyle, [LineStyle,LineWidth,SeriesType]> = {
    "dashed": [LineStyle.Dashed,1,"Line"],
    "double": [LineStyle.Solid,2,"Line"],
    "solid": [LineStyle.Solid,1,"Line"],
    "area": [LineStyle.Solid,1,"Area"],
} as const;

function sanity_setup(n: string, stp: SeriesSetupItem|undefined) : SeriesSetupItem{
    return stp?stp:{color: stringToColor(n), line_style:"solid"};
}

function create_serie_for(n:string, stp: SeriesSetupItem, pane:PaneType) : SeriesDef|null{


    const type = line_style[stp.line_style][2];
    let api:SeriesApi|undefined;
    if (type == "Line") {
        api = chart?.addSeries(LineSeries, {
            priceScaleId: pane_index[pane][1],        
            title: n,
            color: stp.color,
            lineStyle: line_style[stp.line_style][0],
            lineWidth: line_style[stp.line_style][1],
            visible: true,
            
        }, pane_index[pane][0])
    } else if (type == "Area") {
        api = chart?.addSeries(BaselineSeries, {            
            priceScaleId: pane_index[pane][1],        
            title: n,            
            topLineColor: stp.color,       
            bottomLineColor: stp.color,       
            lineStyle: line_style[stp.line_style][0],
            lineWidth: line_style[stp.line_style][1],
            visible: true,
            
        }, pane_index[pane][0])
        
    }
    if (!api) return null;        
    if (n == "Equity") {        
        return {
            type,
            api: api,
            update: simple_update(()=>current_report.value?.eq_chart)
        }
    } else if (n == "Position") {
        return {
            type,
            api: api,
            update: simple_update(()=>current_report.value?.pos_chart)
        }
    } else if (props.report.vars.has(n)) {
        const data = props.report.vars.get(n);
        if (!data) return null;
        return {
            type,
            api: api,
            update: simple_update(()=>data.filter(x=>typeof x.val == "number").map(x=>[x.time, x.val]))                
            };        
    } else {
        return null;
    }
}

function update_series_list(force?:boolean) {
    for (const k in props.options.series_to_panes) {
        const s = Object.fromEntries(props.options.series_to_panes[k as PaneType].map(x=>[x,true]));
        const d = all_series[k as PaneType]
        for (const n in s) {
            if (!d[n] || force) {
                if (d[n]) chart?.removeSeries(d[n].api);
                const srs = create_serie_for(n, sanity_setup(n, props.options.setup[n]), k as PaneType);                
                if (srs) {
                    d[n] = srs;
                    srs.update(srs.api);
                }
            }
        }
        for (const n in d) {
            if (!s[n]) {
                chart?.removeSeries(d[n].api);
                delete d[n];
            }
        }
        Object.values(d).forEach(x=>{
            x.api.moveToPane(pane_index[k as PaneType][0]);            
    });
    }
}

function update_setup() {
    for (const x in all_series) {
        const s = all_series[x as PaneType];
        for (const k in s) {
            const item = s[k];
            const stp = sanity_setup(k,props.options.setup[k]);
            if (item.type == line_style[stp.line_style][2]) {
                if (item.type == "Baseline") {
                    item.api.applyOptions({
                        topLineColor: stp.color,
                        bottomLineColor: stp.color,
                        lineStyle: line_style[stp.line_style][0],
                        lineWidth: line_style[stp.line_style][1]
                    })
                } else if (item.type == "Line") {
                    item.api.applyOptions({
                        color: stp.color,
                        lineStyle: line_style[stp.line_style][0],
                        lineWidth: line_style[stp.line_style][1]
                    })
                }
            } else {
                const new_def = create_serie_for(k, stp, x as PaneType);
                if (new_def) {
                    chart?.removeSeries(item.api);
                    s[k] = new_def;
                    new_def.update(new_def.api);
                }
            }
        }
    }
}


let eq_series:ISeriesApi<"Line">;




function on_mounted() {
    chart = createChart(chartContainer.value!, {leftPriceScale: { visible: true }});
    resizer = new ResizeObserver(entries=>{
        const entry = entries[0]
        chart!.resize(
            entry.contentRect.width,
            entry.contentRect.height
        )
    })
    resizer.observe(chartContainer.value!)
    vol_series = chart.addSeries(HistogramSeries, {
        priceFormat:{
            type:"volume",
        },
        priceScaleId:""
    }, 0);
    vol_series.priceScale().applyOptions({
        scaleMargins: {
            top: 0.75,
            bottom: 0
        },
        visible: props.options.volume
    })
    cs_series = chart.addSeries(CandlestickSeries,{},0);
    markers = createSeriesMarkers(cs_series);
    order_lines = new OrderLinesPrimitive();
    cs_series.attachPrimitive(order_lines);
    order_lines.set_visible(props.options.orders);
    update_series_list();
    update_data();
    chart.timeScale().fitContent();
}
function on_unmounted() {
    resizer?.disconnect();
    chart?.remove();
}


function update_fills() {
    const r = current_report.value;
    const interval = props.options.interval*60;
    if (!props.options.fills || !r) {
        markers?.setMarkers([]);
    } else {
        const sell_data : [Time, number,number][] =[];
        const buy_data : [Time, number,number][] =[];

        r.fills.forEach(x=>{
            const bt = Math.floor(x.time/(1000*interval))*interval as Time;
            const issell = x.side == 'sell';
            const t = issell?sell_data:buy_data;
            const p = t.pop();
            const tu = calculate_turnover(r.info, x.price, x.quantity);
            if (!p || p[0]!=bt) {
                if (p) t.push(p);
                t.push([bt, x.quantity,tu]);
            } else {
                p[1] += x.quantity;
                p[2] += tu;
                t.push(p);
            }
        });
        const m : SeriesMarker<Time>[] = buy_data.map(x=>({
            color: '#004000',
            shape: "arrowUp",
            position:'atPriceBottom',
            price: x[2]/x[1],
            text: `${x[1]}`,
            time: x[0]
        }) as SeriesMarker<Time>).concat(sell_data.map(x=>({
            color: '#800000',
            shape: "arrowDown",
            position:'atPriceTop',
            price: x[2]/x[1],
            text: `${x[1]}`,
            time: x[0]
        }) as SeriesMarker<Time>)).sort((a,b)=> (a.time as number) - (b.time as number))
        markers?.setMarkers(m);

    }
}

watch(()=>props.options.series_to_panes, ()=>{
    update_series_list();
},{deep:true});
watch(()=>props.options.interval, ()=>{
    update_data();
})
watch(current_report, ()=>{
    update_data();
})
watch(()=>props.report.vars, ()=>{
    update_series_list(true);
})
watch(()=>props.options.setup, ()=>{
    update_setup();
},{deep:true})
watch(()=>props.options.volume, (v)=>{
    vol_series.applyOptions({
        visible: v
    });
})
watch(()=>props.options.fills, (v)=>{
    update_fills();
})
watch(()=>props.options.orders, (v)=>{
    order_lines?.set_visible(v);
})

function saveChartPosition() {
    if (!chart) return null;
    const timeScale = chart.timeScale();
    const visibleRange = timeScale.getVisibleRange();
    
    if (!visibleRange) return null;

    // Uložíme si krajní viditelné časy (timestamps)
    return {
        from: visibleRange.from,
        to: visibleRange.to
    };
}

function restoreChartPosition(savedPosition:null|{from:Time, to:Time}) {
    if (!savedPosition || !chart) return;

    const timeScale = chart.timeScale();
    
    // Použijeme setTimeout, aby lightweight-charts stihly interně zpracovat nová data
    setTimeout(() => {
        timeScale.setVisibleRange({
            from: savedPosition.from,
            to: savedPosition.to
        });
    }, 0);
}

function update_data() {
    const interval = props.options.interval * 60;
    const report = current_report.value;

    if (!report) return;
    
    const savedPos = saveChartPosition();

    const eq_data:CandlestickData[] = [];
    const vol_data:HistogramData[] = [];
    report.chart.forEach(x=>{
        const bt = Math.floor(x.time/(interval))*interval as Time;
        let prev = eq_data.pop();
        const prev_v = vol_data.pop();

        if (!prev || prev.time != bt) {
            if (prev) eq_data.push(prev);
            prev = {...x, time: bt};
        } else {
            prev = {open: prev.open, close: x.close, high: Math.max(prev.high, x.high), low: Math.min(prev.low,x.low), time: bt};
        }
        eq_data.push(prev);
        const col = prev.open <= prev.close?"#00A00020":"#FF000020";
        if (!prev_v || prev_v.time != bt) {
            if (prev_v) vol_data.push(prev_v);
            vol_data.push({value: x.volume, time: bt, color:col});
        } else {
            prev_v.value += x.volume;
            prev_v.color = col;
            vol_data.push(prev_v);
        }
    });
    cs_series.setData(eq_data);
    vol_series.setData(vol_data);
    order_lines?.set_data(report.order_instances, eq_data.map(x=>x.time as number), interval);
    for (const x in all_series) {
        const s = all_series[x as PaneType];
        for (const k in s) {
            const item = s[k];
            item.update(item.api);
        }
    }
    update_fills();

    restoreChartPosition(savedPos);
}


onMounted(on_mounted);
onUnmounted(on_unmounted);

</script>
<template>
    <div class="lw-chart" ref="chartContainer" v-bind="$attrs"></div>
</template>
<style scoped>
    .lw-chart {
            height: 100%;
            overflow: hidden;
    }
    
</style>