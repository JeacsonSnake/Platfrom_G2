import {
    createApp
} from 'vue'
import App from './App.vue'
import router from './router'
import store from './store'

// axios
import axios from 'axios'
axios.defaults.baseURL = 'http://127.0.0.1:8000'
// axios.defaults.baseURL = 'http://192.168.31.74:8000'

// date picker
import VueDatePicker from '@vuepic/vue-datepicker'
import '@vuepic/vue-datepicker/dist/main.css'

import VxeUIAll from 'vxe-pc-ui'
import 'vxe-pc-ui/es/style.css'

// Vxe Table
import VxeUITable from 'vxe-table'
import 'vxe-table/es/style.css'

const app = createApp(App)

app.component('VueDatePicker', VueDatePicker);

app.use(router, axios)

app.use(store)

app.use(VxeUIAll)

app.use(VxeUITable)

app.mount('#app')
