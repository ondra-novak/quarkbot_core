<script setup lang="ts">
const PANELS = [
  { id: 'equity', label: 'Equity' },
  { id: 'volume', label: 'Volume' },
] as const

defineProps<{ enabledPanels: string[] }>()
const emit = defineEmits<{ toggle: [id: string] }>()
</script>

<template>
  <div class="panel-btns">
    <span class="sep" />
    <button
      v-for="p in PANELS"
      :key="p.id"
      :class="['panel-btn', { active: enabledPanels.includes(p.id) }]"
      @click="emit('toggle', p.id)"
    >
      {{ p.label }}
    </button>
  </div>
</template>

<style scoped>
.panel-btns { display: flex; align-items: center; gap: 4px; }
.sep { width: 1px; height: 16px; background: #363a4a; margin: 0 4px; }
.panel-btn {
  background: #2a2e3e; color: #787b86; border: none; padding: 3px 10px;
  border-radius: 3px; cursor: pointer; font-size: 12px;
}
.panel-btn.active { background: #1e3040; color: #4fc3f7; }
.panel-btn:hover { background: #363a4a; color: #d1d4dc; }
</style>
