<script setup lang="ts">
import type { GroupedFill } from '../types/report'

defineProps<{ group: GroupedFill }>()
const emit = defineEmits<{ close: [] }>()

function formatTime(unix: number) {
  return new Date(unix * 1000).toISOString().replace('T', ' ').slice(0, 19) + ' UTC'
}
</script>

<template>
  <div class="overlay" @click.self="emit('close')">
    <div class="panel">
      <div class="header">
        <span>Fills in candle</span>
        <button class="close-btn" @click="emit('close')">✕</button>
      </div>
      <table class="table">
        <thead>
          <tr><th>Time</th><th>Side</th><th>Price</th><th>Qty</th><th>Reason</th></tr>
        </thead>
        <tbody>
          <tr v-for="(fill, i) in group.fills" :key="i" :class="fill.side.toLowerCase()">
            <td>{{ formatTime(fill.time) }}</td>
            <td>{{ fill.side }}</td>
            <td>{{ fill.price.toLocaleString() }}</td>
            <td>{{ fill.qty }}</td>
            <td>{{ fill.reason }}</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<style scoped>
.overlay {
  position: fixed; inset: 0; background: rgba(0,0,0,.55);
  display: flex; align-items: center; justify-content: center; z-index: 100;
}
.panel {
  background: #1e2230; border: 1px solid #363a4a; border-radius: 6px;
  min-width: 520px; max-height: 60vh; overflow-y: auto;
}
.header {
  display: flex; justify-content: space-between; align-items: center;
  padding: 10px 14px; border-bottom: 1px solid #363a4a; font-weight: 600; color: #d1d4dc;
}
.close-btn { background: none; border: none; color: #787b86; cursor: pointer; font-size: 16px; }
.close-btn:hover { color: #d1d4dc; }
.table { width: 100%; border-collapse: collapse; font-size: 12px; }
.table th { padding: 6px 14px; text-align: left; color: #787b86; border-bottom: 1px solid #2a2e3e; }
.table td { padding: 6px 14px; border-bottom: 1px solid #1e2230; }
tr.buy td { color: #26a69a; }
tr.sell td { color: #ef5350; }
</style>
