<<<<<<< HEAD
import { createApp } from 'vue'
=======
import {
    createApp
} from 'vue'
>>>>>>> s-codeRunTesting
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

<<<<<<< HEAD
// Vxe Table
import VxeTable from 'vxe-table'
import 'vxe-table/lib/style.css'
import VxeUI from 'vxe-pc-ui'
import 'vxe-pc-ui/lib/style.css'
=======
import VxeUIAll from 'vxe-pc-ui'
import 'vxe-pc-ui/es/style.css'

// Vxe Table
import VxeUITable from 'vxe-table'
import 'vxe-table/es/style.css'
>>>>>>> s-codeRunTesting

const app = createApp(App)

app.component('VueDatePicker', VueDatePicker);

app.use(router, axios)

app.use(store)

<<<<<<< HEAD
app.use(VxeUI)

app.use(VxeTable)

app.mount('#app')
=======
app.use(VxeUIAll)

app.use(VxeUITable)

app.mount('#app')
>>>>>>> s-codeRunTesting
