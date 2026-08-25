<script setup lang="ts">
import { DisplayOptions } from '../types/options';
import { ParsedReport } from '../types/parsed_report';


const model = defineModel<DisplayOptions>();
const props = defineProps<{
    report:ParsedReport
}>();

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

function add_serie() {
    const m = model.value;
    if (!m) return;
    const sr = m.series.filter(v=>v);
    sr.unshift("");
    m.series = sr;
}

</script>

<template>
<div class="topbar" v-if="model">
    <select v-model="model.instrument">
        <option v-for="v of report.instruments" :key="v[0]" :value="v[0]"> {{ v[0] }}</option>
    </select>
    <div class="buttons">
        <template v-for="(v,k) of interval_list" :key="v">
        <button v-if="v >= (report.baseInterval ?? 0)" :key="v" :class="{active: v==model.interval}" @click="model.interval = v">{{ k }}</button>
        </template>
    </div>
    <div class="buttons">
        <button :class="{active: model.fills}" @click="model.fills = !model.fills">Fills</button>
        <button :class="{active: model.orders}" @click="model.orders = !model.orders">Ords</button>
        <button v-if="model.series.length>0 && model.series[0] != ''" @click="add_serie">+</button>
    </div>
    <select v-for="(v,k) of model.series" :key="k" v-model="model.series[k]">
        <option value="">(disabled)</option>    
        <option>Equity</option>
        <option>Position</option>
        <option>Volume</option>
        <option v-for="v of report.vars" :key="v[0]" :value="v[0]"> {{ v[0]}}</option>
    </select>

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
</style>
