import {Chart} from './charts.js';
import {LiveConnection} from './websocket.js';
const $ = id => document.getElementById(id);
const escape = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const fixed = (v, digits = 2) => v === null || v === undefined || v === '' || !Number.isFinite(Number(v)) ? '—' : Number(v).toFixed(digits);
const clock = value => new Date(value).toLocaleTimeString([], {hour12:false});
const duration = seconds => `${Math.floor(seconds / 60).toString().padStart(2,'0')}:${Math.floor(seconds % 60).toString().padStart(2,'0')}`;
let state, config, connected = false, page = 'overview', toastTimer, lastRefresh = 0, lastEvents = '';
const flowMetricsEnabled = () => config?.flow_metrics_enabled !== false;
const velocity = new Chart($('velocity-chart'), {yMin:0, yMax:45});
const history = [];

document.querySelectorAll('[data-overview]').forEach(button => button.addEventListener('click', () => {
  $('page-overview').dataset.view = button.dataset.overview;
  document.querySelectorAll('[data-overview]').forEach(item => item.setAttribute('aria-pressed', String(item === button)));
  velocity.draw();
}));
const compactScreen = matchMedia('(max-width: 1000px)');
const adaptStatus = () => { $('status-panel').open = !compactScreen.matches; };
compactScreen.addEventListener('change', adaptStatus); adaptStatus();

async function api(path, body) {
  const response = await fetch('/api/' + path, body === undefined ? {} : {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
  const data = await response.json();
  if (!response.ok) throw new Error(typeof data.detail === 'string' ? data.detail : 'Invalid request');
  return data;
}
function toast(message, error=false) { clearTimeout(toastTimer); $('toast').textContent=message; $('toast').className=error?'error':''; $('toast').style.display='block'; toastTimer=setTimeout(()=>$('toast').style.display='none',5000); }
function action(id, fn) { const el=$(id); if (!el) return; el.addEventListener('click', async()=>{el.disabled=true; try{await fn();}catch(e){toast(e.message,true);}finally{el.disabled=false;updateControls();}}); }
function badge(id,text){const el=$(id);if(!el)return;el.textContent=text;el.className='tag '+(['FAULT','ERROR','NO DATA'].includes(text)?'error':['IDLE','COMPLETED'].includes(text)?'muted':['WAITING','IN DEVELOPMENT','SAVING'].includes(text)?'amber':'');}
function updateControls(){
  const recording=state?.measurement.state==='RECORDING';
  const valid=/^[A-Za-z0-9][A-Za-z0-9 _.-]{0,79}$/.test($('measurement-name').value.trim());
  $('measurement-name').disabled=recording;
  $('record-start').disabled=!connected || !state?.sensor_fresh || recording || !valid;
  $('record-stop').disabled=!connected || !recording;
  $('recording-rate').disabled=recording;
}
const pages={overview:['Tunnel overview','Live flow data from the USB-connected pitot measurement node.'],raw:['Raw sensor data','Inspect the Arduino Uno / ASPD-4525 telemetry frame.'],cp:['Cp analysis','Multi-point pressure distribution is the next measurement subsystem.'],measurements:['Measurements','Record and review real pitot-tube measurements.'],storage:['Storage & export','Keep data local and export complete runs when needed.'],settings:['System settings','Current acquisition configuration and hardware status.']};
function showPage(name){page=name;document.querySelectorAll('.page').forEach(el=>el.classList.toggle('active',el.id==='page-'+name));document.querySelectorAll('[data-page]').forEach(el=>el.classList.toggle('active',el.dataset.page===name));$('page-title').textContent=pages[name][0];$('page-description').textContent=pages[name][1];$('section-number').textContent=String(Object.keys(pages).indexOf(name)+1).padStart(2,'0');if(['measurements','storage'].includes(name))refresh().catch(e=>toast(e.message,true));renderPage();velocity.draw();}
document.querySelectorAll('[data-page]').forEach(el=>el.addEventListener('click',()=>showPage(el.dataset.page)));
$('collapse').addEventListener('click',()=>document.body.classList.toggle('collapsed'));
action('exit-hmi',async()=>{if(!window.confirm('Close the Windtunnel HMI and return to the Raspberry Pi desktop?'))return;$('exit-hmi').textContent='Closing…';await api('system/exit-hmi',{});});
$('measurement-name').addEventListener('input',updateControls);
action('record-start',async()=>{await api('measurement/start',{name:$('measurement-name').value.trim(),recording_rate_hz:Number($('recording-rate').value)});toast('Recording started · live pitot data');});
action('record-stop',async()=>{await api('measurement/stop',{});toast('Measurement saved');await refresh();});
action('refresh-measurements',refresh);
action('export-selected',async()=>{if(!$('export-select').value)throw new Error('Select a completed measurement');await api('storage/export/'+encodeURIComponent($('export-select').value),{});toast('Measurement exported');await refresh();});
action('export-all',async()=>{await api('storage/export-all',{});toast('All completed measurements exported');await refresh();});
action('eject',async()=>{const result=await api('storage/eject',{});toast(result.message);await refresh();});
action('reconnect-usb',async()=>{await api('storage/reconnect',{});toast('USB storage connected');await refresh();});

function stabilityLabel(v){if(!flowMetricsEnabled())return 'Disabled in Settings';if(v===null||v===undefined)return 'Flow too low';if(v>=98)return 'Excellent';if(v>=95)return 'Good';if(v>=90)return 'Moderate';return 'Unstable';}
function renderPage(){
  if(!state?.sample?.host_timestamp)return;
  const s=state.sample;
  if(page==='raw'){
    const hasLiveSensor=Boolean(state?.sensor_fresh);
    const empty=$('raw-empty-state'),content=$('raw-data-content');
    if(empty) empty.hidden=hasLiveSensor;
    if(content) content.hidden=!hasLiveSensor;
    if(!hasLiveSensor){
      $('raw-table').innerHTML='';
      $('raw-json').textContent='';
      return;
    }
    const rows=[
      ['Pitot differential pressure',s.differential_pressure_pa,'Pa','MEASURED'],
      ['Dynamic pressure',s.dynamic_pressure_pa,'Pa','DERIVED'],
      ['Airspeed',s.airspeed_ms,'m/s','DERIVED'],
      ['Airspeed',s.airspeed_kmh,'km/h','DERIVED'],
      ['Sensor temperature',s.temperature_c,'°C','MEASURED'],
      ['Flow stability',flowMetricsEnabled()?s.flow_stability_pct:null,'%',flowMetricsEnabled()?'5 S WINDOW':'DISABLED'],
      ['Static pressure',null,'Pa','IN DEVELOPMENT'],
      ['Angle of attack',null,'°','IN DEVELOPMENT'],
      ['Fan / VFD output',null,'%','IN DEVELOPMENT']
    ];
    $('raw-table').innerHTML=rows.map(([name,value,unit,status])=>`<tr><td>${escape(name)}</td><td>${fixed(value,4)}</td><td>${fixed(value)}</td><td>${unit}</td><td>${clock(s.host_timestamp)}</td><td><span class="tag ${status==='IN DEVELOPMENT'?'amber':''}">${status}</span></td></tr>`).join('');
    $('raw-json').textContent=JSON.stringify(s,null,2);
  }
}
function onFrame(frame){
  state=frame;const s=frame.sample,m=frame.measurement;
  const hasSample=Boolean(s?.host_timestamp);
  if(hasSample){
    $('airspeed').textContent=fixed(s.airspeed_ms,2);
    $('dynamic').textContent=fixed(s.dynamic_pressure_pa,1);
    $('temperature').textContent=fixed(s.temperature_c,1);
    $('stability').textContent=flowMetricsEnabled()?fixed(s.flow_stability_pct,1):'OFF';
    $('stability-note').textContent=flowMetricsEnabled()?`${stabilityLabel(s.flow_stability_pct)} · 5 s rolling window`:'Disabled in Settings';
    $('control-speed').innerHTML=`${fixed(s.airspeed_ms)} <small>m/s</small>`;
    $('control-stability').innerHTML=flowMetricsEnabled()?`${fixed(s.flow_stability_pct,1)} <small>%</small>`:'OFF';
    $('flow-mean').innerHTML=flowMetricsEnabled()?`${fixed(s.flow_mean_ms,2)}<small> m/s</small>`:'OFF';
    $('flow-std').innerHTML=flowMetricsEnabled()?`${fixed(s.flow_std_ms,3)}<small> m/s</small>`:'OFF';
    $('flow-range').textContent=flowMetricsEnabled()?`${fixed(s.flow_min_ms,2)} / ${fixed(s.flow_max_ms,2)} m/s`:'OFF';
    const t=Date.parse(s.host_timestamp)/1000;
    history.push({x:t,y:s.airspeed_ms,mean:s.flow_mean_ms});while(history.length&&history[0].x<t-60)history.shift();
    if(page==='overview'){const series=[{color:'#c8291e',width:3.2,points:history}];if(flowMetricsEnabled())series.push({color:'#2f3338',width:2.0,dashed:true,points:history.map(p=>({x:p.x,y:p.mean}))});velocity.update(series,t-60,t);}
    $('stream-note').textContent=`LIVE ARDUINO USB · ${frame.sample_age_s===null?'—':frame.sample_age_s.toFixed(1)} s sample age`;
  }
  badge('tunnel-state','IN DEVELOPMENT');badge('measurement-badge',m.state);
  $('elapsed').textContent=duration(m.elapsed_s);$('samples').textContent=m.samples.toLocaleString();$('sample-rate').textContent=m.actual_sample_rate_hz?`${Number(m.actual_sample_rate_hz).toFixed(1)} Hz`:`Target ${m.recording_rate_hz||'—'} Hz`;
  $('file-status').textContent=m.error||(m.name?`${m.file_status} · ${m.name}`:(frame.sensor_fresh?'Ready · enter a measurement name':'Connect Arduino Uno via USB to record.'));
  if(m.state==='RECORDING')$('measurement-name').value=m.name;
  const labels={host:'Host / Raspberry Pi',arduino:'Arduino Uno',serial:'USB serial',pitot:'Pitot sensor',pressure_sensors:'Cp pressure taps',vfd:'Fan / VFD',logger:'Measurement logger',storage:'Storage'};
  $('system-health').innerHTML=Object.entries(frame.system).map(([key,value])=>`<div class="health-row"><span>${escape(labels[key]||key)}</span><b>${escape(value)}</b></div>`).join('');
  $('warnings').textContent=[...frame.errors,...frame.warnings].join(' · ')||'No warnings';
  const eventSignature=JSON.stringify(frame.events);if(eventSignature!==lastEvents){$('event-log').innerHTML=frame.events.slice(0,8).map(e=>`<div class="event"><time>${clock(e.timestamp)}</time>${escape(e.message)}</div>`).join('');lastEvents=eventSignature;}
  $('mode-tag').textContent=frame.sensor_fresh?'● LIVE HARDWARE':'WAITING FOR ARDUINO';$('mode-tag').className='tag '+(frame.sensor_fresh?'':'amber');
  $('hardware-mode-badge').textContent=frame.sensor_fresh?'ARDUINO CONNECTED':'ARDUINO USB';
  const filter=frame.arduino_filter;
  if(filter){
    $('filter-status').textContent=filter.confirmed
      ?`Arduino confirms ${Number(filter.configured_cutoff_hz).toFixed(1)} Hz.`
      :filter.reported_cutoff_hz!==null&&filter.reported_cutoff_hz!==undefined
        ?`Waiting for ${Number(filter.configured_cutoff_hz).toFixed(1)} Hz; Arduino reports ${Number(filter.reported_cutoff_hz).toFixed(1)} Hz.`
        :frame.hardware_mode?'Waiting for Arduino firmware to confirm the filter setting.':'Simulation mode · the setting will be sent when Arduino hardware connects.';
  }
  updateControls();renderPage();
}
async function refresh(){
  const [runs,storage]=await Promise.all([api('measurements'),api('storage')]);
  const hasRuns=runs.length>0;
  $('empty-measurements').hidden=hasRuns;
  $('measurement-table-wrap').hidden=!hasRuns;
  $('measurement-detail').hidden=!hasRuns;
  $('measurement-table').innerHTML=runs.map(m=>`<tr><td><button data-run="${escape(m.id)}">${escape(m.name)}</button><div class="hint">${escape(m.state)}</div></td><td>${escape(new Date(m.timestamp).toLocaleString())}</td><td>${duration(m.duration_s)}</td><td>${fixed(m.average_airspeed_ms)}</td><td>${m.samples}</td><td><span class="tag ${m.exported?'':'muted'}">${m.exported?'EXPORTED':'LOCAL'}</span></td></tr>`).join('');
  document.querySelectorAll('[data-run]').forEach(el=>el.addEventListener('click',()=>loadDetail(el.dataset.run).catch(e=>toast(e.message,true))));
  const el=$('export-select'),value=el.value;el.innerHTML='<option value="">Choose a completed run</option>'+runs.filter(m=>m.state==='COMPLETED').map(m=>`<option value="${escape(m.id)}">${escape(m.name)} · ${escape(new Date(m.timestamp).toLocaleString())}</option>`).join('');el.value=value;
  renderStorage(storage);
}
function renderStorage(storage){
  $('usb-health').textContent=storage.connected?(storage.simulated?'SIM · CONNECTED':'CONNECTED'):'DISCONNECTED';
  $('local-space').textContent=`${(storage.local_free_bytes/1e9).toFixed(1)} GB`;$('usb-space').textContent=storage.connected?`${(storage.usb_free_bytes/1e9).toFixed(1)} GB`:'Disconnected';
  $('usb-label').textContent=storage.simulated?'SIMULATED USB · local test folder':'Physical USB mount';$('unexported').textContent=storage.unexported;$('export-progress').value=storage.progress.percent;$('export-message').textContent=storage.connected?storage.progress.message:'USB disconnected. Local recording remains available.';
  $('reconnect-usb').hidden=!storage.simulated;$('export-selected').disabled=$('export-all').disabled=!storage.connected||storage.progress.state==='EXPORTING';
}
async function loadDetail(id){
  const data=await api('measurements/'+encodeURIComponent(id));
  const table=rows=>rows.length?`<div class="table-wrap"><table><thead><tr>${Object.keys(rows[0]).map(k=>`<th>${escape(k)}</th>`).join('')}</tr></thead><tbody>${rows.map(row=>`<tr>${Object.values(row).map(v=>`<td>${escape(v)}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`:'<p class="hint">No data available</p>';
  const rawUrl=`/api/measurements/${encodeURIComponent(id)}/files/raw_data.csv`,processedUrl=`/api/measurements/${encodeURIComponent(id)}/files/processed_data.csv`;
  $('measurement-detail').innerHTML=`<div class="panel-heading"><h2>${escape(data.metadata.name)}</h2><span class="tag">${data.metadata.exported?'EXPORTED':'LOCAL COPY'}</span></div><div class="run-summary"><div><span>AVERAGE AIRSPEED</span><strong>${fixed(data.metadata.average_airspeed_ms,2)} m/s</strong></div><div><span>DURATION</span><strong>${duration(data.metadata.duration_s)}</strong></div><div><span>SAMPLES</span><strong>${data.metadata.samples}</strong></div><div><span>TARGET RATE</span><strong>${data.metadata.recording_rate_hz??'—'} Hz</strong></div><div><span>ACTUAL RATE</span><strong>${fixed(data.metadata.actual_sample_rate_hz,1)} Hz</strong></div></div><p class="hint">The tables below are previews of the first 200 rows. Download the full CSV for every recorded sample.</p><div class="button-row"><a class="download-link" href="${rawUrl}" download>Download complete raw CSV</a><a class="download-link" href="${processedUrl}" download>Download complete processed CSV</a></div><details><summary>Experiment metadata</summary><pre>${escape(JSON.stringify(data.metadata,null,2))}</pre></details><details><summary>Raw pitot data · preview</summary>${table(data.raw)}</details><details><summary>Processed flow data · preview</summary>${table(data.processed)}</details><div class="development-banner compact"><strong>CP ANALYSIS · IN DEVELOPMENT</strong><span>Pressure-tap hardware is not connected yet.</span></div>`;
}
async function init(){
  config=await api('config');$('density').value=config.air_density;
  const flowToggle=$('flow-metrics-toggle');flowToggle.checked=flowMetricsEnabled();flowToggle.addEventListener('change',async()=>{flowToggle.disabled=true;try{const result=await api('settings/flow-metrics',{enabled:flowToggle.checked});config.flow_metrics_enabled=result.enabled;history.forEach(p=>p.mean=null);toast(result.enabled?'5 s flow statistics enabled':'5 s flow statistics disabled');if(state)onFrame(state);}catch(e){flowToggle.checked=!flowToggle.checked;toast(e.message,true);}finally{flowToggle.disabled=false;}});velocity.setYRange(0,Math.max(45,Math.ceil(config.max_airspeed_ms/5)*5));
  const fields={simulation:'Simulation mode',sample_rate_hz:'Expected pitot rate · Hz',filter_cutoff_hz:'Arduino filter cutoff · Hz',air_density:'Air density · kg/m³',max_airspeed_ms:'Graph maximum airspeed · m/s',serial_port:'Arduino serial port',baud_rate:'Baud rate',data_directory:'Local data directory',usb_mount_path:'USB mount path',simulate_usb:'Simulated USB export'};
  $('settings-grid').innerHTML=Object.entries(fields).map(([key,label])=>`<div class="setting"><span>${label}</span>${escape(String(config[key]??'')||'Not configured')}</div>`).join('');
  $('filter-cutoff').value=Number(config.filter_cutoff_hz??5).toFixed(1);
  $('apply-filter').addEventListener('click',async()=>{
    const button=$('apply-filter'),cutoff=Number($('filter-cutoff').value);
    button.disabled=true;
    try{
      const result=await api('settings/arduino-filter',{cutoff_hz:cutoff});
      config.filter_cutoff_hz=result.cutoff_hz;
      $('filter-status').textContent=result.device_confirmed?'Arduino confirmed the new filter setting.':result.command_sent?'Command sent; waiting for Arduino telemetry confirmation.':'Setting saved; it will be sent when Arduino connects.';
      toast(`Arduino pressure filter set to ${result.cutoff_hz.toFixed(1)} Hz`);
    }catch(e){toast(e.message,true);}
    finally{button.disabled=false;}
  });
  const initial=await api('status');onFrame(initial);
  new LiveConnection(onFrame,online=>{connected=online;badge('connection',online?'● HMI ONLINE':'RECONNECTING');$('connection').classList.toggle('amber',!online);document.body.classList.toggle('offline',!online);if(!online)$('stream-note').textContent='HMI websocket disconnected · values may be stale';updateControls();});
  await refresh();
}
setInterval(()=>{$('clock').textContent=new Date().toLocaleTimeString([],{hour12:false});if(Date.now()-lastRefresh>3000){lastRefresh=Date.now();api('storage').then(renderStorage).catch(()=>{});}},1000);
init().catch(e=>toast('Initialization failed: '+e.message+'. Reload to retry.',true));
