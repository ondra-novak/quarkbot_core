<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted, computed } from 'vue'
import { createChart, CrosshairMode } from 'lightweight-charts'
import type { IChartApi, ISeriesApi, SeriesMarker, Time } from 'lightweight-charts'
import type { Candle, Fill, GroupedFill } from '../types/report'
import { aggregateCandles, groupFillsByCandle } from '../utils/aggregator'

const props = defineProps<{
  candles: Candle[]
  fills: Fill[]
  timeframeFactor: number
}>()

const emit = defineEmits<{
  'fill-clicked': [group: GroupedFill]
}>()

const container = ref<HTMLDivElement | null>(null)
let chart: IChartApi | null = null
let series: ISeriesApi<'Candlestick'> | null = null

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
})

// Update candles whenever aggregated data changes
watch(aggregated, (candles) => {
  if (!series) return
  series.setData(candles.map(c => ({
    time: c.time as Time,
    open: c.open, high: c.high, low: c.low, close: c.close,
  })))
  applyMarkers()
})

// Re-apply markers when fills change
watch(grouped, () => applyMarkers())

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
  // Sort by time (required by lightweight-charts)
  markers.sort((a, b) => (a.time as number) - (b.time as number))
  series.setMarkers(markers)
}

function onChartClick(e: MouseEvent) {
  if (!chart || !container.value) return
  const rect = container.value.getBoundingClientRect()
  const x = e.clientX - rect.left
  const y = e.clientY - rect.top
  const logical = chart.timeScale().coordinateToLogical(x)
  if (logical === null) return

  // Find the closest candle to click position
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
