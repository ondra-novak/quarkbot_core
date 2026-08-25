<script lang="ts" setup>
import { CandlestickSeries, createChart, CrosshairMode, HistogramSeries, LineSeries } from 'lightweight-charts'
import type { IChartApi, ISeriesApi, SeriesMarker, Time, UTCTimestamp } from 'lightweight-charts'
import { onMounted, onUnmounted, ref } from 'vue';
import { ParsedReport } from '../types/parsed_report';

let chart:IChartApi|null = null;
let resizer :ResizeObserver | null = null;
const chartContainer = ref<HTMLElement>();
let cs_series:ISeriesApi<"Candlestick">;
let vol_series:ISeriesApi<"Histogram">;
let eq_series:ISeriesApi<"Line">;

const props = defineProps<{
    report: ParsedReport
    instrument?: string,
    interval?: number,
}>();



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
    vol_series = chart.addSeries(HistogramSeries,{priceFormat:{type:"volume"}},1);
    eq_series = chart.addSeries(LineSeries, {}, 2);
    update_data();
    chart.timeScale().fitContent();
}
function on_unmounted() {
    resizer?.disconnect();
    chart?.remove();
}

function get_first_instrument() {
    for (const [k,v] of props.report.instruments) {
        return k;
    }
    return "";    
}

function update_data() {
    const instr = props.instrument ?? get_first_instrument();
    const interval = (props.interval ?? props.report.baseInterval ?? 5)*60;
    

    const ohlc_buckets : {open:number, high:number, low:number,close:number, time:Time}[] = [];
    const volume_buckets : {time:Time, value:number}[] = [];
    const eq_buickets : {time:Time, value:number}[] = [];

    const start_bucket = Math.floor((props.report.start_tp ?? 0)/(interval*1000));
    const bucket_index = (x:number)=>Math.floor(x/(interval*1000))-start_bucket;
    const bucket_time = (x:number)=>((x+start_bucket)*interval) as UTCTimestamp;
    const end_bucket = bucket_index(props.report.end_tp ?? 0);

    const idata = props.report.instruments.get(instr);
    if (!idata) return;

    idata.chart.forEach(x=>{
        const bi = bucket_index(x.time);
        const bv = ohlc_buckets[bi];        
        if (!bv) {
            ohlc_buckets[bi] = {...x, time: bucket_time(bi)}            
        } else {
            bv.close = x.close;
            bv.high = Math.max(bv.high, x.high);
            bv.low = Math.min(bv.low,x.low);
        }
        const bvv = volume_buckets[bi];
        if (!bvv) volume_buckets[bi] = {time: bucket_time(bi), value: x.volume};
        else bvv.value += x.volume;
    });

    idata.eq_chart.forEach(x=>{
        const bi = bucket_index(x[0]);
        eq_buickets[bi]={
            time: bucket_time(bi),
            value: x[1]
        }
    });


    cs_series.setData(ohlc_buckets.filter(()=>true));
    vol_series.setData(volume_buckets.filter(()=>true));
    eq_series.setData(eq_buickets.filter(()=>true));


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