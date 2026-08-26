<script setup lang="ts">
import { computed, onMounted } from 'vue'
import { SeriesSetupItem } from '../types/options'
import { stringToColor } from '../types/stringToColor';


// nedefinovaná položka = model.value je undefined
const model = defineModel<SeriesSetupItem>()
const props = defineProps<{
    name:string
}>();

const DEFAULT: SeriesSetupItem = { color: stringToColor(props.name), line_style: 'solid' }

// jeden generický lens pro libovolné pole
function field<K extends keyof SeriesSetupItem>(key: K) {
  return computed<SeriesSetupItem[K]>({
    get: () => model.value?.[key] ?? DEFAULT[key],
    set: (v) => {
      // při prvním zápisu (color i line_style) se položka založí s defaulty
      model.value = { ...DEFAULT, ...model.value, [key]: v }
    },
  })
}


const color = field('color')
const lineStyle = field('line_style')
</script>

<template>    
  <input type="color" v-model="color" />
  <select v-model="lineStyle">
            <option value="solid">──────</option>
            <option value="double">━━━━━━</option>
            <option value="dashed">------</option>
  </select>
</template>