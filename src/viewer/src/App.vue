<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue';
import { ParsedReport } from './types/parsed_report';
import LoadProgress from './components/LoadProgress.vue';
import Chart from './components/Chart.vue';
import TopBar from './components/TopBar.vue';
import { DisplayOptions } from './types/options';

const report_url = '/api/report';

const parsed_data = ref<ParsedReport>();

const loading_info = reactive<{loading:boolean, bytes:number, total:number|null, error:string|null}>({
    loading: false,
    bytes: 0,
    total: null,
    error:null
})

function emit_error(s:string) {
    alert(`Error: ${s}`);
}

async function load_data() {
    try {
        loading_info.loading = true;
        const response = await fetch(report_url);
        if (response) {
            if (response.status==200) {
                const len = response.headers.get('content-length');
                if (len) {
                    loading_info.total = parseInt(len);
                } else {
                    loading_info.total = null;
                }
                loading_info.bytes = 0;
                if (response.body) {
                    try {
                        parsed_data.value = await ParsedReport.load(response.body, x=>loading_info.bytes = x);                    
                        loading_info.loading = false;
                        options.value.instrument = parsed_data.value.get_default_instrumnet(options.value.instrument);  
                        options.value.interval = Math.max(options.value.interval, parsed_data.value.baseInterval ??0);
                    } catch (e) {
                        loading_info.error = `Error reading report: ${(e as Error).message}`;
                    }
                } else {
                    loading_info.error = "No data arrived";
                }
            } else {
                loading_info.error = `Server unexpected status error: ${response.status}`;            
            }
        } else {
            loading_info.error = `Failed to receive data`;
        }
    } catch (e) {
        loading_info.error = `Failed to receive report file: ${(e as Error).message}`;
    }
}


onMounted(load_data);

const options = ref<DisplayOptions>({
    instrument: "",
    interval: 5,
    fills:true,
    orders: false,  
    series:["Volume","Equity"]
})

</script>

<template>
    <LoadProgress v-if="loading_info.loading" :bytes="loading_info.bytes" :total="loading_info.total" :error="loading_info.error"></LoadProgress>
    <div v-else-if="parsed_data" class="split">
        <TopBar :report="parsed_data" v-model="options"></TopBar>
        <Chart :report="parsed_data"></Chart>
    </div>
    
</template>

<style lang="css" scoped>
div.split {
    height: 100%;
    display: flex;
    flex-direction: column;
}
</style>
