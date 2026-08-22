<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted, computed } from 'vue'
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

const container = ref<HTMLDivElement | null>(null)
let chart: IChartApi | null = null
let series: ISeriesApi<'Candlestick'> | null = null
let equitySeries: ISeriesApi<'Baseline'> | null = null
let volumeSeries: ISeriesApi<'Histogram'> | null = null

const aggregated = computed(() => aggregateCandles(props.candles, props.timeframeFactor))
const grouped = computed(() => groupFillsByCandle(props.fills, aggregated.value))

onMounted(() => {
  if (!container.value) return

  chart = createChart(container.value, {
    width: container.value.clientWidth,
    height: container.value.clientHeight,
    layout: { background: { color: '#131722' }, textColor: '#d1d4dc' },
    grid: { vertLines: { color: '#1e2231' }, horzLines: { color: '#1e2231' } },
    crosshair: { mode: CrosshairMode.Normal },
    rightPriceScale: { borderColor: '#2a2e3e' },
    timeScale: { borderColor: '#2a2e3e', timeVisible: true, secondsVisible: false },
  })

  series = chart.addCandlestickSeries({
    upColor: '#26a69a', downColor: '#ef5350',
    borderVisible: false,
    wickUpColor: '#26a69a', wickDownColor: '#ef5350',
  })

  const ro = new ResizeObserver(() => {
    if (container.value && chart) {
      chart.applyOptions({ width: container.value.clientWidth, height: container.value.clientHeight })
    }
  })
  ro.observe(container.value)
  onUnmounted(() => { ro.disconnect(); chart?.remove() })

  rebuildPanels()
})

watch(aggregated, (candles) => {
  if (!series) return
  series.setData(candles.map(c => ({
    time: c.time as Time,
    open: c.open, high: c.high, low: c.low, close: c.close,
  })))
  applyMarkers()
  updateVolume()
})

watch(() => props.fills, () => {
  updateEquity()
})

watch(() => props.enabledPanels, () => {
  rebuildPanels()
}, { deep: true })

function rebuildPanels() {
  if (!chart) return

  if (equitySeries) { chart.removeSeries(equitySeries); equitySeries = null }
  if (volumeSeries) { chart.removeSeries(volumeSeries); volumeSeries = null }

  // pane option exists at runtime but is absent from v4.2.3 TS types — cast to any
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const withPane = (n: number, opts: object) => ({ pane: n, ...opts } as any)

  let pane = 1
  for (const id of props.enabledPanels) {
    if (id === 'equity') {
      equitySeries = chart.addBaselineSeries(withPane(pane++, {
        topLineColor: '#26a69a',
        topFillColor1: 'rgba(38,166,154,0.28)',
        topFillColor2: 'rgba(38,166,154,0.05)',
        bottomLineColor: '#ef5350',
        bottomFillColor1: 'rgba(239,83,80,0.05)',
        bottomFillColor2: 'rgba(239,83,80,0.28)',
        baseValue: { type: 'price', price: 0 },
        lineWidth: 1,
      }))
      updateEquity()
    } else if (id === 'volume') {
      volumeSeries = chart.addHistogramSeries(withPane(pane++, {
        priceFormat: { type: 'volume' },
        priceScaleId: '',
      }))
      updateVolume()
    }
    // future panels: add more cases here
  }
}

function updateEquity() {
  if (!equitySeries) return
  equitySeries.setData(computeEquityCurve(props.fills))
}

function updateVolume() {
  if (!volumeSeries) return
  volumeSeries.setData(aggregated.value.map(c => ({
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

    if (hasBuy) {
      markers.push({
        time: group.candleTime as Time,
        position: 'belowBar',
        color: '#26a69a',
        shape: 'arrowUp',
        text: label,
        size: 1,
      })
    }
    if (hasSell) {
      markers.push({
        time: group.candleTime as Time,
        position: 'aboveBar',
        color: '#ef5350',
        shape: 'arrowDown',
        text: label,
        size: 1,
      })
    }
  }
  markers.sort((a, b) => (a.time as number) - (b.time as number))
  series.setMarkers(markers)
}

function onChartClick(e: MouseEvent) {
  if (!chart || !container.value) return
  const rect = container.value.getBoundingClientRect()
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
  <div class="chart-wrap" ref="container" @click="onChartClick" />
</template>

<style scoped>
.chart-wrap { flex: 1; overflow: hidden; cursor: crosshair; }
</style>
