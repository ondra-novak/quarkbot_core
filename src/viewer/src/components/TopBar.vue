<script setup lang="ts">
import { DisplayOptions, paneTypes } from '../types/options';
import { ParsedReport } from '../types/parsed_report';
import SeriesSelector from './SeriesSelector.vue';


const model = defineModel<DisplayOptions>();
const props = defineProps<{
    report:ParsedReport
}>();
const emit = defineEmits(["reload"]);

const interval_list = {
    "1m": 1,
    "3m": 3,
    "5m": 5,
    "10m": 10,
    "15m": 15,
    "30m": 30,
    "1h": 60,
    "4h": 240,
    "6h": 360,
    "8h": 480,
    "1D": 1440,
    "3D": 4320,
    "1W":10080,
    "1M":43200
};

function invoke_reload() {
    emit("reload");
}

</script>

<template>
<div class="topbar" v-if="model">
    <div class="buttons">
        <button @click="invoke_reload()">↻</button>
    </div>
    <select v-model="model.instrument">
        <option v-for="v of report.instruments" :key="v[0]" :value="v[0]"> {{ v[0] }}</option>
    </select>
    <div class="buttons">
        <template v-for="(v,k) of interval_list" :key="v">
        <button v-if="v >= (report.baseInterval ?? 0)" :key="v" :class="{active: v==model.interval}" @click="model.interval = v">{{ k }}</button>
        </template>
    </div>
    <div class="buttons">
        <button :class="{active: model.volume}" @click="model.volume = !model.volume">Vol</button>
        <button :class="{active: model.fills}" @click="model.fills = !model.fills">Fills</button>
        <button :class="{active: model.orders}" @click="model.orders = !model.orders">Ords</button>
    </div>
    <template v-for="v of paneTypes">
        <SeriesSelector v-model:main="model.series_to_panes[v]" v-model:setup="model.setup" :report="report" :name="v" class="pane"></SeriesSelector>
    </template>
</div>
</template>

<style lang="css" scoped>
.topbar {
    display: flex;
    gap: 0.2rem
}
button {
    width:3rem;
    cursor: pointer;
    border: 1px solid;
    height: 2rem
}
button.active {
    background-color: #ccc;
}
select {
    max-width: 15rem;
}
.pane {
    flex-grow: 1;
}
</style>
