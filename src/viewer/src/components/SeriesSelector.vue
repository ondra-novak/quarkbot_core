<script setup lang="ts">
import { computed, ref } from 'vue';
import { LineStyle, SeriesSetup, SeriesSetupItem } from '../types/options';
import { ParsedReport } from '../types/parsed_report';
import SeriesItemEditor from './SeriesItemEditor.vue';
import { stringToColor } from '../types/stringToColor';


const model = defineModel<string[]>("main");
const setup = defineModel<SeriesSetup>("setup");
const props = defineProps<{
    report: ParsedReport,
    name: string
}>();
const opened = ref(false);

const options = computed(()=>{
        const mp : Record<string, boolean> = {};
        mp["Equity"] = false;
        mp["Position"] = false;
        for (const [k, v] of props.report.vars) {
            mp[k] = false;
        }
        for (const v of model.value ?? []) {
            mp[v] = true;
        }
        return mp;
    }
)

function swap_option(x:string) {
    const mp = options.value;
    mp[x] = !mp[x];
    const out = [];
    for (const k in mp) {
        if (mp[k]) out.push(k);
    }
    model.value = out;
}

const listref = ref<HTMLElement>();

function close_popup() {
    opened.value = false;
    window.removeEventListener("mouseup", close_popup_ms);
    window.removeEventListener("keydown", close_popup_key);
}

function close_popup_ms(ev: MouseEvent) {
    let t = ev.target as HTMLElement;
    while (t && t != listref.value) t = t.parentElement;
    if (t) return;
    setTimeout(close_popup,100);
}

function close_popup_key(ev: KeyboardEvent) {
    if (ev.key == "Escape") {
        close_popup();
        ev.stopPropagation();
        ev.preventDefault();
    }
}

function open_popup() {
    if (!opened.value) {
        opened.value = true;
        window.addEventListener("mouseup", close_popup_ms);
        window.addEventListener("keydown", close_popup_key);
    } 
}

function item_color(v:string) {
    const stp = setup.value!;
    const c = stp[v];
    if (!c) return stringToColor(v);
    else return c.color;

}

</script>
<template>
<div class="m" v-if="model && setup" v-bind="$attrs">
    <div class="ovr">{{ name }}</div>
    <div class="mm" @click="open_popup" tabindex="1">
        <div class="l" v-for="v of model" :style="{color: item_color(v)}"> {{ v }}</div>
    </div>
    
<div class="s" v-if="opened" ref="listref">
    <div v-for="(v,k) of options" :key="k">
        <div :class="{enabled: v}" @click="swap_option(k)" > {{ k }}</div>
        <SeriesItemEditor v-model="setup[k]" :name="k"></SeriesItemEditor>
    </div>
</div>
</div>
</template>
<style lang="css" scoped>
div.m {
    height: 2rem;    
    position: relative;
    background-color: #eee;
    color:black;
    padding: 0 0.1rem;
}
div.ovr {
    color: #0003;
    font-weight: bold;
    position: absolute;
    inset: 0;
    text-align: right;
    font-size: 1.8rem;
    pointer-events: none;
    overflow: hidden;

}
div.mm {
    font-size: 0.8rem;
    overflow: hidden;
    cursor: pointer; 
    position: absolute;   
    inset:0;
    display:flex;
    flex-wrap: wrap;
    gap: 0.2rem;
}
div.l::after {
    content: ",";
    color:black;
}
div.l:last-child::after {
    content: "";
}
div.s {
    position: absolute;
    z-index:100;
    background-color: #666;
    color:white;
    font-size: 0.8rem;    
    padding: 0.2rem 0.2rem 0.2rem 1rem  ;
    border: 1px solid black;
    right: 0;
    top:100%;    
    
}
div.s > div {
    display: flex;    
    position: relative;
}
div.s > div > *:first-child {
    flex-grow: 1;
    overflow: hidden;
    text-overflow: ellipsis;
    cursor: pointer;
    padding-right: 0.2rem;
}
div.s > div > .enabled::before {
    content:"✔";    
    display: block;
    position: absolute;
    left: -0.8rem;
}
div.l {
    text-shadow: 0px 0px 2px white;
    font-weight: bold;
}

.s :deep(select) {
    font-size: 0.8rem;
    width: 3rem;
}
.s :deep(input) {
    height: 1.2rem;
    padding: 0;
    margin: 0;
    width: 2rem;
}

</style>