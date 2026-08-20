<script setup lang="ts">
import type { InstrumentMeta } from '../types/report'

const props = defineProps<{
  instruments: InstrumentMeta[]
  selectedInstrument: string
  baseInterval: number
  timeframeFactor: number
  progress: number
}>()

const emit = defineEmits<{
  'update:selectedInstrument': [value: string]
  'update:timeframeFactor': [value: number]
}>()

const FACTORS = [1, 3, 5, 15, 60]
</script>

<template>
  <div class="topbar">
    <span class="brand">⬡ QuarkBot Viewer</span>

    <select
      v-if="instruments.length > 1"
      :value="selectedInstrument"
      @change="emit('update:selectedInstrument', ($event.target as HTMLSelectElement).value)"
      class="select"
    >
      <option v-for="instr in instruments" :key="instr.name" :value="instr.name">
        {{ instr.name }}
      </option>
    </select>
    <span v-else class="instrument-name">{{ selectedInstrument }}</span>

    <div class="tf-buttons">
      <button
        v-for="f in FACTORS"
        :key="f"
        :class="['tf-btn', { active: timeframeFactor === f }]"
        @click="emit('update:timeframeFactor', f)"
      >
        {{ f === 1 ? `${baseInterval}m` : `${baseInterval * f}m` }}
      </button>
    </div>

    <div class="progress-bar" v-if="progress < 100">
      <div class="progress-fill" :style="{ width: progress + '%' }" />
      <span class="progress-label">{{ progress }}%</span>
    </div>
  </div>
</template>

<style scoped>
.topbar {
  display: flex; align-items: center; gap: 12px;
  padding: 6px 12px; background: #1e2230; border-bottom: 1px solid #2a2e3e;
  flex-shrink: 0;
}
.brand { color: #4fc3f7; font-weight: 600; }
.instrument-name { color: #d1d4dc; font-weight: 500; }
.select { background: #2a2e3e; color: #d1d4dc; border: 1px solid #363a4a; padding: 3px 6px; border-radius: 4px; }
.tf-buttons { display: flex; gap: 4px; margin-left: 8px; }
.tf-btn {
  background: #2a2e3e; color: #787b86; border: none; padding: 3px 10px;
  border-radius: 3px; cursor: pointer; font-size: 12px;
}
.tf-btn.active, .tf-btn:hover { background: #363a4a; color: #d1d4dc; }
.progress-bar {
  margin-left: auto; position: relative; width: 140px; height: 6px;
  background: #2a2e3e; border-radius: 3px; overflow: hidden;
}
.progress-fill { height: 100%; background: #4fc3f7; transition: width 0.15s; }
.progress-label {
  position: absolute; right: -32px; top: -5px;
  font-size: 11px; color: #787b86;
}
</style>
