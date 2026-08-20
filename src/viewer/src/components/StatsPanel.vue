<script setup lang="ts">
import type { Stats } from '../types/report'

const props = defineProps<{
  stats: Stats | undefined
  loading: boolean
  instrument: string
  baseInterval: number
  timeframeFactor: number
}>()

function fmt(n: number, decimals = 2) {
  return n.toLocaleString(undefined, { minimumFractionDigits: decimals, maximumFractionDigits: decimals })
}

function fmtDate(unix: number) {
  if (!unix) return '—'
  return new Date(unix * 1000).toISOString().slice(0, 10)
}

function fmtPct(n: number) { return (n * 100).toFixed(1) + '%' }
</script>

<template>
  <aside class="panel">
    <div class="section-title">{{ instrument }}</div>

    <template v-if="stats">
      <!-- Quick stats (available early) -->
      <div class="section">
        <div class="row"><span class="label">Fills</span><span>{{ stats.fillCount }}</span></div>
        <div class="row">
          <span class="label">BUY / SELL</span>
          <span><span class="buy">{{ stats.buyCount }}</span> / <span class="sell">{{ stats.sellCount }}</span></span>
        </div>
        <div class="row"><span class="label">Turnover</span><span>${{ fmt(stats.totalTurnover) }}</span></div>
        <div class="row"><span class="label">Fees</span><span>${{ fmt(stats.totalFees) }}</span></div>
        <div class="row"><span class="label">Net qty</span><span :class="stats.netQty >= 0 ? 'buy' : 'sell'">{{ fmt(stats.netQty, 6) }}</span></div>
      </div>

      <div class="section-title small">Analytics</div>

      <template v-if="loading">
        <div class="skeleton" v-for="i in 8" :key="i" />
      </template>
      <template v-else>
        <div class="section">
          <div class="row"><span class="label">Max profit</span><span class="buy">${{ fmt(stats.maxProfit) }}</span></div>
          <div class="row"><span class="label">Max drawdown</span><span class="sell">${{ fmt(stats.maxDrawdown) }}</span></div>
          <div class="row"><span class="label">P/DD ratio</span><span>{{ fmt(stats.profitDrawdownRatio, 2) }}</span></div>
          <div class="row"><span class="label">Max margin</span><span>${{ fmt(stats.maxMargin) }}</span></div>
          <div class="row"><span class="label">Win rate</span><span>{{ fmtPct(stats.winRate) }}</span></div>
          <div class="row"><span class="label">MAE (worst)</span><span class="sell">{{ fmt(stats.mae) }}</span></div>
          <div class="row"><span class="label">MFE (best)</span><span class="buy">{{ fmt(stats.mfe) }}</span></div>
        </div>
        <div class="section">
          <div class="row"><span class="label">From</span><span>{{ fmtDate(stats.dateFrom) }}</span></div>
          <div class="row"><span class="label">To</span><span>{{ fmtDate(stats.dateTo) }}</span></div>
        </div>
      </template>
    </template>

    <div v-else class="loading-msg">Loading…</div>
  </aside>
</template>

<style scoped>
.panel {
  width: 220px; flex-shrink: 0; overflow-y: auto;
  background: #1a1d2e; border-left: 1px solid #2a2e3e; padding: 10px 0;
}
.section-title {
  padding: 8px 12px 4px; font-size: 11px; text-transform: uppercase;
  letter-spacing: .06em; color: #4fc3f7; font-weight: 600;
}
.section-title.small { font-size: 10px; color: #787b86; margin-top: 8px; }
.section { padding: 0 12px 8px; border-bottom: 1px solid #2a2e3e; }
.row { display: flex; justify-content: space-between; padding: 3px 0; font-size: 12px; }
.label { color: #787b86; }
.buy { color: #26a69a; }
.sell { color: #ef5350; }
.skeleton {
  height: 14px; margin: 5px 12px; border-radius: 3px;
  background: linear-gradient(90deg, #2a2e3e 25%, #363a4a 50%, #2a2e3e 75%);
  background-size: 200% 100%; animation: shimmer 1.2s infinite;
}
@keyframes shimmer { 0% { background-position: 200% 0; } 100% { background-position: -200% 0; } }
.loading-msg { padding: 12px; color: #787b86; font-size: 12px; }
</style>
