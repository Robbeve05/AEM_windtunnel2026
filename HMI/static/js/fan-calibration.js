/* Settings tab; active calibration is always the ESP32-reported state. */
(() => {
  const el=id=>document.getElementById(id);
  let current=null, runs=[];
  async function request(path,body) {
    const r=await fetch('/api/calibration/fan/'+path,body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const data=await r.json();if(!r.ok)throw new Error(typeof data.detail==='string'?data.detail:JSON.stringify(data.detail));return data;
  }
  function tab(cal) {
    el('general-settings').hidden=cal;el('calibration-settings').hidden=!cal;
    el('general-tab').setAttribute('aria-selected',String(!cal));el('calibration-tab').setAttribute('aria-selected',String(cal));
    if(cal) refresh();
  }
  el('general-tab').onclick=()=>tab(false);el('calibration-tab').onclick=()=>tab(true);
  const number=v=>Number.isFinite(v)?v.toFixed(3):'—';
  function render(s) {
    current=s;const e=s.esp32||{};const verified=runs.find(r=>r.deployment_status==='VERIFIED'&&r.crc32===e.cal_crc);
    el('fan-active-status').textContent=e.stale||!e.connected?'ESP32 disconnected or stale':`${e.cal_source||'UNKNOWN'} · available ${e.cal_valid??'unknown'} · version ${e.cal_version??'—'} · ${e.cal_points??0} points · CRC32 ${e.cal_crc??'—'} · date ${verified?verified.timestamp:'unverified'}`;
    el('fan-progress').textContent=`${s.state} · point ${s.index}/${s.total} · target ${number(s.target_hz)} Hz · applied ${number(e.applied_hz)} Hz · V20 ${number(e.actual_hz)} Hz · Pitot ${number(e.pv)} m/s · std ${number(s.stability?.std)} m/s · trend ${number(s.stability?.trend)} m/s² · ${s.message}`;
    el('fan-percent').value=s.progress||0;
    el('fan-prepare').disabled=s.active;el('fan-cancel').disabled=!s.active;
    el('fan-deploy').disabled=s.active||s.state!=='COMPLETED';el('fan-reset').disabled=s.active;
    el('fan-configuration').querySelectorAll('input').forEach(i=>i.disabled=s.active);
    el('fan-results').replaceChildren();
    for(const p of s.points) {
      const tr=document.createElement('tr');
      for(const v of [number(p.target_hz),number(p.mean_pv),number(p.std_pv),p.samples]) {const td=document.createElement('td');td.textContent=v;tr.append(td);}el('fan-results').append(tr);
    }
    const svg=el('fan-curve');svg.replaceChildren();const ns='http://www.w3.org/2000/svg';
    const max=Math.max(40,...s.points.map(p=>p.mean_pv));
    function text(x,y,label){const t=document.createElementNS(ns,'text');t.setAttribute('x',x);t.setAttribute('y',y);t.setAttribute('fill','currentColor');t.textContent=label;svg.append(t);}
    text(10,20,'Airspeed (m/s)');text(430,235,'Frequency (Hz)');text(10,210,'0');text(10,45,String(max));text(550,210,'50');
    const line=document.createElementNS(ns,'polyline');line.setAttribute('points',s.points.map(p=>`${40+p.target_hz*10.4},${205-p.mean_pv/max*165}`).join(' '));line.setAttribute('fill','none');line.setAttribute('stroke','#d7193f');line.setAttribute('stroke-width','3');svg.append(line);
  }
  async function refresh() {
    runs=await request('runs');el('fan-runs').replaceChildren();
    for(const r of runs) {
      const row=document.createElement('p');row.append(document.createTextNode(`${r.timestamp} · ${r.completion_status} · ${r.deployment_status} `));
      for(const file of ['calibration.json','raw_samples.csv','characteristic.csv','inverse_lookup.csv']) {const a=document.createElement('a');a.href=`/api/calibration/fan/runs/${encodeURIComponent(r.id)}/files/${file}`;a.textContent=file+' ';row.append(a);}
      if(r.completion_status==='COMPLETED') {const b=document.createElement('button');b.textContent='Activate';b.onclick=()=>action(()=>request(`runs/${encodeURIComponent(r.id)}/deploy`,{}));row.append(b);}el('fan-runs').append(row);
    }
    render(await request('status'));
  }
  async function action(fn){try{await fn();await refresh();}catch(e){el('fan-progress').textContent=e.message;}}
  el('fan-configuration').onsubmit=e=>{e.preventDefault();const data=Object.fromEntries([...new FormData(e.target)].map(([k,v])=>[k,Number(v)]));action(()=>request('prepare',data));};
  el('fan-cancel').onclick=()=>action(()=>request('cancel',{}));
  el('fan-deploy').onclick=()=>action(()=>request(`runs/${encodeURIComponent(current.run_id)}/deploy`,{}));
  el('fan-reset').onclick=()=>{if(confirm('Remove the active fan calibration and restore the default calculated motor model? Historical measurement files will be preserved.'))action(()=>request('reset',{}));};
  el('fan-refresh').onclick=()=>action(refresh);
  setInterval(()=>{if(!el('calibration-settings').hidden)request('status').then(render).catch(e=>{el('fan-active-status').textContent=e.message;});},500);
})();
