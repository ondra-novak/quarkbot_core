<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted, computed, nextTick } from 'vue'
import { createChart, CrosshairMode } from 'lightweight-charts'
import type { IChartApi, ISeriesApi, SeriesMarker, Time } from 'lightweight-charts'
import type { Candle, Fill, GroupedFill } from '../types/report'
import { aggregateCandles, groupFillsByCandle } from '../utils/aggregator'
import { computeEquityCurve } from '../utils/equity'

const props = defineProps<{
  candles: Candle[]
  fills: Fill[]
  timeframeFactor: number
  enabledPanels: string[]
}>()

const emit = defineEmits<{
  'fill-clicked': [group: GroupedFill]
}>()

const mainContainer = ref<HTMLDivElement | null>(null)
const equityContainer = ref<HTMLDivElement | null>(null)
const volumeContainer = ref<HTMLDivElement | null>(null)

let chart: IChartApi | null = null
let equityChart: IChartApi | null = null
let volumeChart: IChartApi | null = null
let series: ISeriesApi<'Candlestick'> | null = null
let equitySeries: ISeriesApi<'Baseline'> | null = null
let volumeSeries: ISeriesApi<'Histogram'> | null = null

let isSyncing = false

const aggregated = computed(() => aggregateCandles(props.candles, props.timeframeFactor))
const grouped = computed(() => groupFillsByCandle(props.fills, aggregated.value))

const SHARED_OPTS = {
  layout: { background: { color: '#131722' }, textColor: '#d1d4dc' },
  grid: { vertLines: { color: '#1e2231' }, horzLines: { color: '#1e2231' } },
  crosshair: { mode: CrosshairMode.Normal },
  rightPriceScale: { borderColor: '#2a2e3e' },
  timeScale: { borderColor: '#2a2e3e', timeVisible: true, secondsVisible: false },
}

function syncTimeScale(source: IChartApi) {
  if (isSyncing) return
  isSyncing = true
  const range = source.timeScale().getVisibleLogicalRange()
  if (range !== null) {
    for (const c of [chart, equityChart, volumeChart]) {
      if (c && c !== source) c.timeScale().setVisibleLogicalRange(range)
    }
  }
  isSyncing = false
}

onMounted(() => {
  if (!mainContainer.value || !equityContainer.value || !volumeContainer.value) return

  // Main candlestick chart
  chart = createChart(mainContainer.value, {
    ...SHARED_OPTS,
    width: mainContainer.value.clientWidth,
    height: mainContainer.value.clientHeight,
  })
  series = chart.addCandlestickSeries({
    upColor: '#26a69a', downColor: '#ef5350',
    borderVisible: false,
    wickUpColor: '#26a69a', wickDownColor: '#ef5350',
  })
  chart.timeScale().subscribeVisibleLogicalRangeChange(() => syncTimeScale(chart!))

  // Equity panel chart
  equityChart = createChart(equityContainer.value, {
    ...SHARED_OPTS,
    width: equityContainer.value.clientWidth,
    height: equityContainer.value.clientHeight,
  })
  equitySeries = equityChart.addBaselineSeries({
    topLineColor: '#26a69a',
    topFillColor1: 'rgba(38,166,154,0.28)',
    topFillColor2: 'rgba(38,166,154,0.05)',
    bottomLineColor: '#ef5350',
    bottomFillColor1: 'rgba(239,83,80,0.05)',
    bottomFillColor2: 'rgba(239,83,80,0.28)',
    baseValue: { type: 'price', price: 0 },
    lineWidth: 1,
  })
  equityChart.timeScale().subscribeVisibleLogicalRangeChange(() => syncTimeScale(equityChart!))

  // Volume panel chart
  volumeChart = createChart(volumeContainer.value, {
    ...SHARED_OPTS,
    width: volumeContainer.value.clientWidth,
    height: volumeContainer.value.clientHeight,
  })
  volumeSeries = volumeChart.addHistogramSeries({
    priceFormat: { type: 'volume' },
    priceScaleId: '',
  })
  volumeChart.timeScale().subscribeVisibleLogicalRangeChange(() => syncTimeScale(volumeChart!))

  const ro = new ResizeObserver(() => {
    if (mainContainer.value && chart)
      chart.applyOptions({ width: mainContainer.value.clientWidth, height: mainContainer.value.clientHeight })
    if (equityContainer.value?.clientWidth && equityChart)
      equityChart.applyOptions({ width: equityContainer.value.clientWidth, height: equityContainer.value.clientHeight })
    if (volumeContainer.value?.clientWidth && volumeChart)
      volumeChart.applyOptions({ width: volumeContainer.value.clientWidth, height: volumeContainer.value.clientHeight })
  })
  ro.observe(mainContainer.value)
  ro.observe(equityContainer.value)
  ro.observe(volumeContainer.value)

  onUnmounted(() => {
    ro.disconnect()
    chart?.remove(); equityChart?.remove(); volumeChart?.remove()
  })
})

// When candles change: update main series, markers, volume
watch(aggregated, (candles) => {
  if (!series) return
  series.setData(candles.map(c => ({
    time: c.time as Time,
    open: c.open, high: c.high, low: c.low, close: c.close,
  })))
  applyMarkers()
  updateVolume()
})

// When fills change: update equity
watch(() => props.fills, () => {
  updateEquity()
})

// When panels toggled: re-trigger resize (v-show may have changed element dimensions)
watch(() => props.enabledPanels, async () => {
  await nextTick()
  if (equityContainer.value && equityChart) {
    const w = equityContainer.value.clientWidth
    const h = equityContainer.value.clientHeight
    if (w > 0) equityChart.applyOptions({ width: w, height: h })
  }
  if (volumeContainer.value && volumeChart) {
    const w = volumeContainer.value.clientWidth
    const h = volumeContainer.value.clientHeight
    if (w > 0) volumeChart.applyOptions({ width: w, height: h })
  }
  // Sync newly-visible panel to current main chart range
  const range = chart?.timeScale().getVisibleLogicalRange()
  if (range !== null && range !== undefined) {
    equityChart?.timeScale().setVisibleLogicalRange(range)
    volumeChart?.timeScale().setVisibleLogicalRange(range)
  }
}, { deep: true })

function updateEquity() {
  equitySeries?.setData(computeEquityCurve(props.fills))
}

function updateVolume() {
  volumeSeries?.setData(aggregated.value.map(c => ({
    time: c.time as Time,
    value: c.volume,
    color: c.close >= c.open ? 'rgba(38,166,154,0.5)' : 'rgba(239,83,80,0.5)',
  })))
}

function applyMarkers() {
  if (!series) return
  const markers: SeriesMarker<Time>[] = []
  for (const group of grouped.value) {
    const hasBuy = group.fills.some(f => f.side === 'BUY')
    const hasSell = group.fills.some(f => f.side === 'SELL')
    const count = group.fills.length
    const label = count > 1 ? `${count}` : ''

    if (hasBuy) markers.push({ time: group.candleTime as Time, position: 'belowBar', color: '#26a69a', shape: 'arrowUp', text: label, size: 1 })
    if (hasSell) markers.push({ time: group.candleTime as Time, position: 'aboveBar', color: '#ef5350', shape: 'arrowDown', text: label, size: 1 })
  }
  markers.sort((a, b) => (a.time as number) - (b.time as number))
  series.setMarkers(markers)
}

function onChartClick(e: MouseEvent) {
  if (!chart || !mainContainer.value) return
  const rect = mainContainer.value.getBoundingClientRect()
  const x = e.clientX - rect.left
  const logical = chart.timeScale().coordinateToLogical(x)
  if (logical === null) return
  const candles = aggregated.value
  if (logical < 0 || logical >= candles.length) return
  const clickedCandle = candles[Math.round(logical)]
  if (!clickedCandle) return
  const group = grouped.value.find(g => g.candleTime === clickedCandle.time)
  if (group) emit('fill-clicked', group)
}
</script>

<template>
  <div class="charts-col">
    <div class="main-chart" ref="mainContainer" @click="onChartClick" />
    <div
      class="panel-chart"
      ref="equityContainer"
      v-show="enabledPanels.includes('equity')"
    />
    <div
      class="panel-chart"
      ref="volumeContainer"
      v-show="enabledPanels.includes('volume')"
    />
  </div>
</template>

<style scoped>
.charts-col {
  display: flex;
  flex-direction: column;
  flex: 1;
  overflow: hidden;
}
.main-chart {
  flex: 1;
  min-height: 0;
  cursor: crosshair;
}
.panel-chart {
  height: 150px;
  flex-shrink: 0;
  cursor: crosshair;
}
</style>
