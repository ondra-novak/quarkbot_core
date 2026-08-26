<script lang="ts" setup>
import { CandlestickSeries, createChart, CrosshairMode, HistogramSeries, LineSeries, LineWidth } from 'lightweight-charts'
import { CandlestickData, IChartApi, ISeriesApi, LineData, LineStyle, SeriesMarker, Time, UTCTimestamp } from 'lightweight-charts'
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { ParsedReport } from '../types/parsed_report';
import { DisplayOptions,  PaneType, paneTypes, QLineStyle, SeriesSetupItem } from '../types/options';
import { stringToColor } from '../types/stringToColor';

let chart:IChartApi|null = null;
let resizer :ResizeObserver | null = null;
const chartContainer = ref<HTMLElement>();
let cs_series:ISeriesApi<"Candlestick">;

type SeriesApi = ISeriesApi<"Candlestick">|ISeriesApi<"Histogram">|ISeriesApi<"Line">;
interface SeriesDef {
    api: SeriesApi,
    update:(api:SeriesApi)=>void
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
                    mapped_data.push(prev);
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
    pane_2: [2, "right"]
} as const;

const line_style : Record<QLineStyle, [LineStyle,LineWidth]> = {
    "dashed": [LineStyle.Dashed,1],
    "double": [LineStyle.Solid,2],
    "solid": [LineStyle.Solid,1],
} as const;

function sanity_setup(n: string, stp: SeriesSetupItem|undefined) : SeriesSetupItem{
    return stp?stp:{color: stringToColor(n), line_style:"solid"};
}

function create_serie_for(n:string, stp: SeriesSetupItem, pane:PaneType) : SeriesDef|null{


    const api = chart?.addSeries(LineSeries, {
        priceScaleId: pane_index[pane][1],        
        title: n,
        color: stp.color,
        lineStyle: line_style[stp.line_style][0],
        lineWidth: line_style[stp.line_style][1],
        visible: true
    }, pane_index[pane][0])
    if (!api) return null;        
    if (n == "Equity") {        
        return {
            api: api,
            update: simple_update(()=>current_report.value?.eq_chart)
        }
    } else if (n == "Position") {
        return {
            api: api,
            update: simple_update(()=>current_report.value?.pos_chart)
        }
    } else if (props.report.vars.has(n)) {
        const data = props.report.vars.get(n);
        if (!data) return null;
        return {
            api: api,
            update: simple_update(()=>data.filter(x=>typeof x.val == "number").map(x=>[x.time, x.val]))                
            };        
    } else {
        return null;
    }
}

function update_series_list() {
    for (const k in props.options.series_to_panes) {
        const s = Object.fromEntries(props.options.series_to_panes[k as PaneType].map(x=>[x,true]));
        const d = all_series[k as PaneType]
        for (const n in s) {
            if (!d[n]) {
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
    }
}

function update_setup() {
    for (const x in all_series) {
        const s = all_series[x as PaneType];
        for (const k in s) {
            const item = s[k];
            const stp = sanity_setup(k,props.options.setup[k]);
            item.api.applyOptions({
                color: stp.color,
                lineStyle: line_style[stp.line_style][0],
                lineWidth: line_style[stp.line_style][1]
            })
        }
    }
}


let eq_series:ISeriesApi<"Line">;




function on_mounted() {
    chart = createChart(chartContainer.value!);
    resizer = new ResizeObserver(entries=>{
        const entry = entries[0]
        chart!.resize(
            entry.contentRect.width,
            entry.contentRect.height
        )
    })
    resizer.observe(chartContainer.value!)
    cs_series = chart.addSeries(CandlestickSeries,{},0);
    update_series_list();
    update_data();
    chart.timeScale().fitContent();
}
function on_unmounted() {
    resizer?.disconnect();
    chart?.remove();
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
watch(()=>props.options.setup, ()=>{
    update_setup();
},{deep:true})

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
    report.chart.forEach(x=>{
        const bt = Math.floor(x.time/(interval*1000))*interval as Time;
        const prev = eq_data.pop();
        if (!prev) eq_data.push({...x, time: bt});
        else if (prev.time != bt) {
            eq_data.push(prev);
            eq_data.push({...x, time: bt});
        } else {
            eq_data.push({open: prev.open, close: x.close, high: Math.max(prev.high, x.high), low: Math.min(prev.low,x.low), time: bt});
        }
    });
    cs_series.setData(eq_data);
    for (const x in all_series) {
        const s = all_series[x as PaneType];
        for (const k in s) {
            const item = s[k];
            item.update(item.api);
        }
    }

    restoreChartPosition(savedPos);
}


onMounted(on_mounted);
onUnmounted(on_unmounted);

</script>
<template>
    <div class="lw-chart" ref="chartContainer"></div>
</template>
<style scoped>
    .lw-chart {
            height: 100%;
            overflow: hidden;
    }
    
</style>