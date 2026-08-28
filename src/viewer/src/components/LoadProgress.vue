<script setup lang="ts">
import { computed } from 'vue';


const props = defineProps<{
    bytes: number,
    total: number|null
    error: string|null
}>();


const progress= computed(()=>{
    if (props.total) {
        const s=  `${((props.bytes * 100)/props.total).toFixed(1)}%`;
        return s;
    } else {
        return "0";
    }
})
</script>

<template>
<div class="t">
    <div>
        <h1 v-if="error" class="error">{{error}}</h1>
        <h1 v-else>Loading...</h1>
        <div class="p">
            <div :style="{width: progress}"></div>
        </div>

        <div>{{ Math.round(bytes / 1024) }} KB</div>
    </div>
</div>
</template>

<style lang="css" scoped>
div.t {
    display:flex;
    align-items: center;
    position: fixed;
    inset: 0;
    background-color: #0008;    
    z-index: 1000;
}
div.t > div {
    flex-grow: 1;
    text-align: center;
}
div.p {
    height: 1rem;
    width: 50vw;
    background-color: #444;
    margin: auto
}
div.p > div {
    background-color: green;
    height: 100%;
}
h1.error {
    color: #F88;
}
</style>
