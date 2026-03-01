import { createApp, ref, onMounted } from 'vue'

const API = 'http://127.0.0.1:18080/api'

const App = {
  setup() {
    const state = ref({ playing: false, sync: false, frame: 0, sources: [] })
    const addPath = ref('')

    const pullState = async () => {
      const r = await fetch(`${API}/state`)
      state.value = await r.json()
    }

    const call = async (path) => {
      await fetch(`${API}${path}`, { method: 'POST' })
      await pullState()
    }

    onMounted(async () => {
      await pullState()
      setInterval(pullState, 500)
    })

    return { state, addPath, pullState, call }
  },
  template: `
    <main style="font-family: system-ui; margin: 16px;">
      <h2>Multi-Cam 6DoF Tracker (Electron + Vue)</h2>
      <div style="display:flex; gap:8px; margin-bottom:12px;">
        <input v-model="addPath" placeholder="video path" style="min-width:320px; padding:6px;" />
        <button @click="call('/sources/add?path=' + encodeURIComponent(addPath))">Add Video</button>
        <button @click="call('/sources/remove_last')">Remove Last</button>
      </div>
      <div style="display:flex; gap:8px; margin-bottom:12px;">
        <button @click="call('/play?sync=true')">Play (Sync)</button>
        <button @click="call('/pause')">Pause</button>
        <button @click="call('/step?delta=-1')">Prev Frame</button>
        <button @click="call('/step?delta=1')">Next Frame</button>
      </div>

      <pre style="background:#111; color:#ddd; padding:12px; border-radius:6px;">{{ JSON.stringify(state, null, 2) }}</pre>
      <p style="color:#666;">This is migration phase-1 UI shell. Next step wires real video canvases and drawing overlays from backend streams.</p>
    </main>
  `
}

createApp(App).mount('#app')
