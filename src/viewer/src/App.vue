<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import type { InstrumentMeta, Candle, Fill, Stats, WorkerMessage } from './types/report'
import ParserWorker from './workers/parser.worker?worker'
import TopBar from './components/TopBar.vue'
import ChartView from './components/ChartView.vue'
import StatsPanel from './components/StatsPanel.vue'
import FillDetail from './components/FillDetail.vue'
import type { GroupedFill } from './types/report'

// --- State ---
const instruments = ref<InstrumentMeta[]>([])
const selectedInstrument = ref('')
const baseInterval = ref(1)
const timeframeFactor = ref(1)
const progress = ref(0)
const loadingStats = ref(true)
const error = ref('')

const candlesMap = ref(new Map<string, Candle[]>())
const fillsMap = ref(new Map<string, Fill[]>())
const statsMap = ref(new Map<string, Stats>())

// Fill detail popup
const detailFills = ref<GroupedFill | null>(null)

// --- Computed for selected instrument ---
const currentCandles = computed(() => candlesMap.value.get(selectedInstrument.value) ?? [])
const currentFills = computed(() => fillsMap.value.get(selectedInstrument.value) ?? [])
const currentStats = computed(() => statsMap.value.get(selectedInstrument.value))

// --- Worker setup ---
let worker: Worker | null = null

onMounted(() => {
  worker = new ParserWorker()
  worker.onmessage = (e: MessageEvent<WorkerMessage>) => {
    const msg = e.data
    if (msg.type === 'meta') {
      instruments.value = msg.instruments
      baseInterval.value = msg.baseInterval
      if (!selectedInstrument.value && msg.instruments.length > 0) {
        selectedInstrument.value = msg.instruments[0].name
      }
      // Initialize maps for each instrument
      for (const instr of msg.instruments) {
        if (!candlesMap.value.has(instr.name)) candlesMap.value.set(instr.name, [])
        if (!fillsMap.value.has(instr.name)) fillsMap.value.set(instr.name, [])
      }
    } else if (msg.type === 'candles') {
      const existing = candlesMap.value.get(msg.instrument) ?? []
      candlesMap.value.set(msg.instrument, [...existing, ...msg.data])
      // Trigger reactivity
      candlesMap.value = new Map(candlesMap.value)
    } else if (msg.type === 'fills') {
      const existing = fillsMap.value.get(msg.instrument) ?? []
      fillsMap.value.set(msg.instrument, [...existing, ...msg.data])
      fillsMap.value = new Map(fillsMap.value)
    } else if (msg.type === 'progress') {
      progress.value = msg.percent
    } else if (msg.type === 'stats') {
      statsMap.value.set(msg.instrument, msg.data)
      statsMap.value = new Map(statsMap.value)
      loadingStats.value = false
    } else if (msg.type === 'error') {
      error.value = msg.message
    }
  }

  // Fetch Content-Length first so the worker can report accurate progress
  fetch('/api/report', { method: 'HEAD' }).then(r => {
    const contentLength = parseInt(r.headers.get('content-length') ?? '0')
    worker!.postMessage({ url: '/api/report', contentLength })
  }).catch(() => {
    worker!.postMessage({ url: '/api/report', contentLength: 0 })
  })
})

onUnmounted(() => worker?.terminate())
</script>

<template>
  <div class="app">
    <TopBar
      :instruments="instruments"
      :selected-instrument="selectedInstrument"
      :base-interval="baseInterval"
      :timeframe-factor="timeframeFactor"
      :progress="progress"
      @update:selected-instrument="selectedInstrument = $event"
      @update:timeframe-factor="timeframeFactor = $event"
    />

    <div v-if="error" class="error">{{ error }}</div>

    <div class="main" v-else>
      <ChartView
        :candles="currentCandles"
        :fills="currentFills"
        :timeframe-factor="timeframeFactor"
        @fill-clicked="detailFills = $event"
      />
      <StatsPanel
        :stats="currentStats"
        :loading="loadingStats"
        :instrument="selectedInstrument"
        :base-interval="baseInterval"
        :timeframe-factor="timeframeFactor"
      />
    </div>

    <FillDetail
      v-if="detailFills"
      :group="detailFills"
      @close="detailFills = null"
    />
  </div>
</template>

<style>
.app { display: flex; flex-direction: column; height: 100vh; }
.main { display: flex; flex: 1; overflow: hidden; }
.error { padding: 20px; color: #ef5350; }
</style>
