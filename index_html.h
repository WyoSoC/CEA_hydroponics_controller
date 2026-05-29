#pragma once
#include <Arduino.h>

// Self-contained dashboard (no external resources -> works offline / behind Funnel).
// Live readings via WebSocket; control/calibration/settings via HTTP POST (so the proxy's
// auth headers apply). The server enforces the lock on every request; the UI only
// hides/disables controls as a convenience. EC is shown in mS/cm (probe reads uS/1000).
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Hydroponics</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { margin:0; font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif; background:#0f1418; color:#e7edf2; }
  header { padding:14px 20px; display:flex; align-items:center; gap:12px; border-bottom:1px solid #223; }
  h1 { font-size:18px; margin:0; font-weight:600; }
  h2 { font-size:13px; letter-spacing:.06em; text-transform:uppercase; color:#8fb; opacity:.7; margin:0 0 12px; }
  #dot { width:10px; height:10px; border-radius:50%; background:#c0392b; }
  #dot.live { background:#27ae60; }
  #conn { font-size:13px; color:#8aa; }
  #lockbar { padding:12px 20px; font-weight:600; text-align:center; }
  #lockbar.locked   { background:#3a1c1c; color:#ff8a80; }
  #lockbar.unlocked { background:#15321f; color:#86e6a8; }
  #alarmbar { display:none; padding:10px 20px; font-weight:700; text-align:center; background:#5a1d1d; color:#ffb3a7; }
  main { padding:20px; display:grid; gap:16px; grid-template-columns:repeat(auto-fit,minmax(190px,1fr)); max-width:980px; }
  .card { background:#172026; border:1px solid #243; border-radius:12px; padding:18px 20px; }
  .card.alarm { border-color:#c0392b; box-shadow:0 0 0 1px #c0392b inset; }
  .label { font-size:13px; letter-spacing:.06em; text-transform:uppercase; color:#7f9; opacity:.7; }
  .val { font-size:42px; font-weight:700; margin-top:6px; }
  .unit { font-size:15px; font-weight:400; color:#9ab; margin-left:4px; }
  .tgt { font-size:12px; color:#7f9aa6; margin-top:6px; }
  .stale .val { color:#7a8a96; }
  .ph .val{color:#f39c12}.ec .val{color:#3498db}.temp .val{color:#1ab6c8}
  section { max-width:980px; margin:0 20px 16px; background:#141b21; border:1px solid #223; border-radius:12px; padding:16px 20px; }
  section[hidden]{display:none}
  .row { display:flex; flex-wrap:wrap; gap:10px; align-items:center; margin:8px 0; }
  button { background:#1f6feb; color:#fff; border:0; border-radius:8px; padding:8px 14px; font-size:14px; cursor:pointer; }
  button.sec { background:#33414d; } button.warn{background:#b4471f;}
  button:disabled { opacity:.4; cursor:not-allowed; }
  input,select { background:#0e141a; color:#e7edf2; border:1px solid #2a3a46; border-radius:7px; padding:7px 9px; font-size:14px; }
  input[type=number]{width:84px}
  #log { font-family:ui-monospace,Menlo,monospace; font-size:12px; background:#0c1116; border:1px solid #223; border-radius:8px; padding:10px; height:150px; overflow:auto; white-space:pre-wrap; color:#a8c0cf; }
  footer { padding:14px 20px; color:#7f9aa6; font-size:13px; border-top:1px solid #223; display:flex; flex-wrap:wrap; gap:18px; }
  .muted{color:#7f9aa6;font-size:12px}
  .calhead{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
  #calresult{font-family:ui-monospace,Menlo,monospace;font-size:12px;color:#9fe6b6;margin-top:6px;min-height:16px}
  hr{border:0;border-top:1px solid #223;margin:16px 0}
  details.help{margin:6px 0 12px}
  details.help summary{cursor:pointer;color:#8ad;font-size:13px}
  .eq{font-size:19px;text-align:center;background:#0e141a;border:1px solid #2a3a46;border-radius:8px;padding:12px;margin:10px 0;color:#cfe1ea}
  .eq sub{font-size:12px;color:#9ab}
  .help ul{margin:8px 0;padding-left:20px;font-size:13px;color:#c7d3da;line-height:1.5}
  .help li{margin:4px 0}
</style>
</head>
<body>
<header><span id="dot"></span><h1 id="name">Hydroponics</h1><span id="conn">connecting…</span></header>
<div id="lockbar" class="locked">…</div>
<div id="alarmbar"></div>

<main>
  <div class="card ph" id="cardph"><div class="label">pH</div><div class="val" id="ph">--</div><div class="tgt" id="phtgt"></div></div>
  <div class="card ec" id="cardec"><div class="label">EC</div><div class="val" id="ec">--<span class="unit">mS/cm</span></div><div class="tgt" id="ectgt"></div></div>
  <div class="card temp"><div class="label">Temperature</div><div class="val" id="temp">--<span class="unit">°C</span></div></div>
</main>

<section id="controls" hidden>
  <h2>Pumps</h2>
  <div class="row"><span>1 acid (pH↓)</span><input type="number" id="ml1" value="1.0" step="0.5"><button onclick="dispense(1)">Dispense mL</button><button class="sec" onclick="stop(1)">Stop</button></div>
  <div class="row"><span>2 nutrient A</span><input type="number" id="ml2" value="1.0" step="0.5"><button onclick="dispense(2)">Dispense mL</button><button class="sec" onclick="stop(2)">Stop</button></div>
  <div class="row"><span>3 nutrient B</span><input type="number" id="ml3" value="1.0" step="0.5"><button onclick="dispense(3)">Dispense mL</button><button class="sec" onclick="stop(3)">Stop</button></div>
  <div class="row"><button class="warn" onclick="stop(0)">STOP ALL</button><span class="muted">max single dose 25 mL; negative = reverse</span></div>

  <hr>
  <h2>Control settings (PI)</h2>
  <details class="help"><summary>What do Kp and Ki do? (tap for intuition)</summary>
    <div class="eq">dose = K<sub>p</sub> &middot; e &nbsp;+&nbsp; K<sub>i</sub> &middot; &Sigma;(e &middot; &Delta;t)</div>
    <ul>
      <li><b>e</b> = how far the reading is from the setpoint (in the direction the pump can fix).</li>
      <li><b>K<sub>p</sub> &middot; e</b> &mdash; the <b>proportional</b> term: dose in proportion to the error <i>right now</i>.
          Bigger K<sub>p</sub> = stronger, faster correction. Too big &rarr; overshoot
          (and for pH that's permanent &mdash; there's no base pump to undo it).</li>
      <li><b>K<sub>i</sub> &middot; &Sigma;(e &middot; &Delta;t)</b> &mdash; the <b>integral</b> term: it adds up the error <i>over time</i>,
          pushing harder the longer you stay off-target. Use it to erase a small, stubborn offset.
          Start at <b>0</b> (proportional-only); raise it slowly.</li>
      <li><b>&Delta;t</b> = the channel's dose interval &mdash; each cycle doses once, then waits to mix and re-measure.</li>
    </ul>
    <div class="muted">The dose is capped and never negative &mdash; the pump only ever <i>adds</i> (acid, or nutrient).</div>
  </details>
  <div class="row"><b>pH — acid</b></div>
  <div class="row">setpoint <input type="number" id="s_phSp" step="0.1"> Kp <input type="number" id="s_phKp" step="0.1"> Ki <input type="number" id="s_phKi" step="0.01"> interval <input type="number" id="s_phMin" step="1"> min</div>
  <div class="row">alarms — low <input type="number" id="s_phAlo" step="0.1"> high <input type="number" id="s_phAhi" step="0.1"></div>
  <div class="row"><b>EC — nutrients (pumps 2 &amp; 3, equal)</b></div>
  <div class="row">setpoint <input type="number" id="s_ecSp" step="0.1"> mS/cm Kp <input type="number" id="s_ecKp" step="0.1"> Ki <input type="number" id="s_ecKi" step="0.01"> interval <input type="number" id="s_ecMin" step="1"> min</div>
  <div class="row">alarms — low <input type="number" id="s_ecAlo" step="0.1"> high <input type="number" id="s_ecAhi" step="0.1"> mS/cm</div>
  <div class="row"><button onclick="saveSettings()">Save settings</button> <span id="setmsg" class="muted"></span> <span class="muted" id="iterm"></span></div>
  <div class="muted">Ki = 0 → proportional-only (no integral). Each channel doses once per its interval, then waits to mix (dose-and-wait). pH only doses acid; EC only adds nutrient.</div>

  <hr>
  <h2>Calibration wizard</h2>
  <div class="row calhead">
    <select id="cdev" onchange="renderCal()">
      <option value="ph">pH</option><option value="ec">EC</option><option value="rtd">RTD / Temp</option><option value="pump">Pumps</option>
    </select>
    <span id="calreading" class="muted"></span><span id="calstab"></span>
    <button class="sec" onclick="calStatus()">Check status</button>
    <button class="sec" onclick="calClear()">Clear cal</button>
  </div>
  <div id="calsteps"></div>
  <div id="calresult"></div>

  <hr>
  <details><summary class="muted">Advanced: raw EZO command</summary>
    <div class="row"><select id="rawdev"><option>ph</option><option>ec</option><option>rtd</option><option>p1</option><option>p2</option><option>p3</option></select>
      <input id="rawcmd" placeholder="e.g. Status" style="flex:1;min-width:140px"><button onclick="rawSend()">Send</button></div>
  </details>
</section>

<section id="admin" hidden>
  <h2>Instructor (tailnet)</h2>
  <div class="row">unlock for <input type="number" id="umin" value="60" step="5"> min
    <button onclick="unlock()">Unlock public control</button>
    <button class="warn" onclick="lock()">Lock</button>
    <button class="sec" onclick="loadLog()">Refresh audit log</button></div>
  <div id="log"></div>
  <hr>
  <div class="row">Firmware update (.bin) <input type="file" id="otafile" accept=".bin">
    <button onclick="otaUpload()">Upload &amp; flash</button>
    <span id="otaprog" class="muted"></span></div>
  <div class="muted">Tailnet only. Device reboots into the new firmware on success.</div>
  <hr>
  <div class="row">Unit name <input id="idname" style="width:150px"> hostname <input id="idhost" style="width:150px">
    <button onclick="setIdentity()">Rename &amp; reboot</button> <span id="idmsg" class="muted"></span></div>
  <div class="muted">Sets this unit's display name + mDNS hostname (&lt;host&gt;.local). Reboots to apply.</div>
</section>

<footer><span>updated <b id="age">–</b></span><span>auto-dosing: <b id="auto">–</b></span>
  <span>queue: <b id="queue">–</b></span><span>uptime: <b id="uptime">–</b></span></footer>

<script>
const $=id=>document.getElementById(id);
let ws, st={}, lastMsg=0, isTailnet=false, remain=0, remainSync=0;
let hist={ph:[],ec:[],temp:[]};

function fmt(v,ok,dp){ return ok ? Number(v).toFixed(dp) : "--"; }
function setConn(live){ $("dot").classList.toggle("live",live); $("conn").textContent=live?"live":"disconnected — retrying"; }
function canControl(){ return isTailnet || !st.locked; }
function applyCaps(){ $("admin").hidden=!isTailnet; $("controls").hidden=!canControl(); }

function renderLock(){
  const bar=$("lockbar");
  if(st.locked){ bar.className="locked"; bar.textContent="🔒 LOCKED — public control disabled"; }
  else { const m=Math.floor(remain/60), s=remain%60; bar.className="unlocked";
    bar.textContent=`🔓 UNLOCKED — public control enabled · ${m}:${String(s).padStart(2,'0')} remaining`; }
}
function renderAlarms(){
  const a=[]; if(st.phHi)a.push('pH HIGH'); if(st.phLo)a.push('pH LOW'); if(st.ecHi)a.push('EC HIGH'); if(st.ecLo)a.push('EC LOW');
  const el=$("alarmbar"); el.style.display=a.length?'block':'none'; if(a.length)el.textContent='⚠ ALARM: '+a.join(' · ');
  $("cardph").classList.toggle('alarm', !!(st.phHi||st.phLo));
  $("cardec").classList.toggle('alarm', !!(st.ecHi||st.ecLo));
}
const SKEYS=[['phSp','s_phSp'],['phAlo','s_phAlo'],['phAhi','s_phAhi'],['phKp','s_phKp'],['phKi','s_phKi'],['phMin','s_phMin'],['ecSp','s_ecSp'],['ecAlo','s_ecAlo'],['ecAhi','s_ecAhi'],['ecKp','s_ecKp'],['ecKi','s_ecKi'],['ecMin','s_ecMin']];
function fillSettings(){ SKEYS.forEach(([k,id])=>{ const el=$(id); if(el && document.activeElement!==el && st[k]!=null) el.value=st[k]; }); }

function render(){
  if(st.name){ $("name").textContent=st.name; document.title=st.name; }
  $("ph").textContent=fmt(st.ph,st.phOk,2);
  $("ec").innerHTML=(st.ecOk?(st.ec/1000).toFixed(2):'--')+'<span class="unit">mS/cm</span>';
  $("temp").innerHTML=fmt(st.temp,st.tempOk,1)+'<span class="unit">°C</span>';
  if(st.phSp!=null) $("phtgt").textContent=`target ${st.phSp} · alarms ${st.phAlo}–${st.phAhi}`;
  if(st.ecSp!=null) $("ectgt").textContent=`target ${st.ecSp} · alarms ${st.ecAlo}–${st.ecAhi}`;
  $("auto").textContent=st.auto?"ON":"off"; $("queue").textContent=st.queue??"–";
  $("uptime").textContent=st.uptime!=null?st.uptime+" s":"–";
  remain=st.lockRemain||0; remainSync=Date.now();
  if(st.iPh!=null) $("iterm").textContent='integrator: pH '+Number(st.iPh).toFixed(2)+' mL · EC '+Number(st.iEc).toFixed(2)+' mL';
  if(st.phOk)pp('ph',st.ph); if(st.ecOk)pp('ec',st.ec); if(st.tempOk)pp('temp',st.temp);
  applyCaps(); renderLock(); renderAlarms(); fillSettings(); updateCalReading();
}
function pp(k,v){ hist[k].push(v); if(hist[k].length>5)hist[k].shift(); }
function tick(){
  if(lastMsg){ const secs=Math.round(((st.ageMs||0)+(Date.now()-lastMsg))/1000);
    $("age").textContent=secs+" s ago"; const stale=secs>8;
    document.querySelectorAll(".card").forEach(c=>c.classList.toggle("stale",stale)); }
  if(!st.locked && remain>0){ const left=remain-Math.floor((Date.now()-remainSync)/1000);
    if(left>=0){ const old=remain; remain=left; if(old!==remain) renderLock(); } }
}
function logLine(s){ const el=$("log"); el.textContent+=(el.textContent?"\n":"")+new Date().toLocaleTimeString()+"  "+s; el.scrollTop=el.scrollHeight; }

// ---- calibration wizard (EC shown in uS, matching cal-solution labels) ----
function metricFor(d){
  if(d==='ph')  return ['ph', st.phOk, st.ph, 0.05, 2, 'pH'];
  if(d==='ec')  return ['ec', st.ecOk, st.ec, Math.max(30,0.01*(st.ec||0)), 0, 'µS/cm'];
  if(d==='rtd') return ['temp', st.tempOk, st.temp, 0.3, 1, '°C'];
  return [null];
}
function updateCalReading(){
  const d=$("cdev").value, m=metricFor(d), el=$("calreading"), sb=$("calstab");
  if(!m[0]){ el.textContent=''; sb.textContent=''; return; }
  el.textContent = m[1] ? ('now: '+Number(m[2]).toFixed(m[4])+' '+m[5]) : 'no reading';
  const arr=hist[m[0]]; let stable = arr.length>=4 && (Math.max(...arr)-Math.min(...arr))<=m[3];
  sb.textContent = m[1] ? (stable?'● stable':'… stabilizing') : ''; sb.style.color = stable?'#27ae60':'#f39c12';
}
function calDev(){ const d=$("cdev").value; return d==='pump' ? ('p'+($('psel')?$('psel').value:'1')) : d; }
function calSend(dev,cmd){ post(`/api/cal?dev=${dev}&cmd=${encodeURIComponent(cmd)}`); }
const calStatus=()=>calSend(calDev(),'Cal,?');
const calClear =()=>{ if(confirm('Clear calibration for '+calDev()+'?')) calSend(calDev(),'Cal,clear'); };
function rawSend(){ const c=$("rawcmd").value.trim(); if(c) calSend($("rawdev").value,c); }

function renderCal(){
  const d=$("cdev").value; let h='';
  if(d==='ph'){
    h=`<div class="muted">Rinse the probe between buffers and wait for a STABLE reading before each point. Order: mid → low → high.</div>
      <div class="row">1. Midpoint buffer <input type="number" id="phmid" value="7.00" step="0.01"> <button onclick="calSend('ph','Cal,mid,'+$('phmid').value)">Calibrate mid</button></div>
      <div class="row">2. Low buffer <input type="number" id="phlow" value="4.00" step="0.01"> <button onclick="calSend('ph','Cal,low,'+$('phlow').value)">Calibrate low</button></div>
      <div class="row">3. High buffer <input type="number" id="phhigh" value="10.00" step="0.01"> <button onclick="calSend('ph','Cal,high,'+$('phhigh').value)">Calibrate high</button></div>
      <div class="row"><button class="sec" onclick="calSend('ph','Slope,?')">Check slope</button></div>`;
  } else if(d==='ec'){
    h=`<div class="muted">Set K to match your probe, dry-calibrate with a DRY probe, then calibrate in solution. Values in µS/cm.</div>
      <div class="row">K value <select id="eck"><option>0.1</option><option selected>1.0</option><option>10</option></select>
        <button onclick="calSend('ec','K,'+$('eck').value)">Set K</button><button class="sec" onclick="calSend('ec','K,?')">K?</button></div>
      <div class="row">1. <button onclick="calSend('ec','Cal,dry')">Dry calibration (probe dry!)</button></div>
      <div class="row">2. Single point <input type="number" id="ecn" value="12880"> µS <button onclick="calSend('ec','Cal,'+$('ecn').value)">Calibrate</button></div>
      <div class="row">or two-point: low <input type="number" id="eclow" value="12880"> <button onclick="calSend('ec','Cal,low,'+$('eclow').value)">low</button>
        high <input type="number" id="echigh" value="80000"> <button onclick="calSend('ec','Cal,high,'+$('echigh').value)">high</button></div>`;
  } else if(d==='rtd'){
    h=`<div class="muted">Single-point at a known temperature (e.g. 100 °C boiling, 0 °C ice water). PT-1000 usually doesn't need this.</div>
      <div class="row">Reference temp <input type="number" id="rtdt" value="100.00" step="0.01"> °C <button onclick="calSend('rtd','Cal,'+$('rtdt').value)">Calibrate</button></div>`;
  } else {
    h=`<div class="muted">Prime the pump, dispense a known amount into the graduated cylinder, then enter the ACTUAL measured volume.</div>
      <div class="row">Pump <select id="psel" onchange="updateCalReading()"><option value="1">1 acid</option><option value="2">2 nutrient A</option><option value="3">3 nutrient B</option></select></div>
      <div class="row">Dispense test <input type="number" id="pml" value="10" step="1"> mL <button onclick="post('/api/pump?id='+$('psel').value+'&ml='+$('pml').value)">Dispense</button></div>
      <div class="row">Measured <input type="number" id="pmeas" value="10" step="0.1"> mL <button onclick="calSend('p'+$('psel').value,'Cal,'+$('pmeas').value)">Calibrate to measured</button></div>`;
  }
  $("calsteps").innerHTML=h; updateCalReading();
}

function connect(){
  const proto = location.protocol==='https:' ? 'wss' : 'ws';   // wss when behind Funnel/TLS
  ws=new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen=()=>setConn(true);
  ws.onclose=()=>{setConn(false); setTimeout(connect,2000);};
  ws.onerror=()=>ws.close();
  ws.onmessage=e=>{ try{ const d=JSON.parse(e.data);
    if(d.event!==undefined){ logLine(d.event); $("calresult").textContent='⤷ '+d.event; }
    else { st=d; lastMsg=Date.now(); render(); }
  }catch(_){}};
}

async function post(path){
  try{ const r=await fetch(path,{method:'POST'}); const j=await r.json().catch(()=>({}));
    if(!r.ok) logLine("✗ "+(j.err||r.status)+"  ("+path+")"); return r.ok;
  }catch(e){ logLine("✗ network error"); return false; }
}
const dispense=i=>post(`/api/pump?id=${i}&ml=${encodeURIComponent($("ml"+i).value)}`);
const stop=i=>post(`/api/pump/stop${i?`?id=${i}`:""}`);
const unlock=()=>post(`/api/unlock?min=${encodeURIComponent($("umin").value)}`);
const lock=()=>post(`/api/lock`);
function saveSettings(){ const q=SKEYS.map(([k,id])=>k+'='+encodeURIComponent($(id).value)).join('&');
  post('/api/settings?'+q).then(ok=>{ $("setmsg").textContent=ok?'saved':'failed'; setTimeout(()=>$("setmsg").textContent='',2500); }); }
async function loadLog(){ try{ const r=await fetch('/api/log'); const a=await r.json();
  $("log").textContent=a.map(e=>`${(e.t/1000)|0}s  ${e.who}  ${e.action}  ${e.detail}`).join("\n"); }catch(_){} }
async function whoami(){ try{ const r=await fetch('/api/whoami'); const j=await r.json();
  isTailnet=(j.origin==='tailnet'); applyCaps(); }catch(_){} }

async function loadIdentity(){ try{ const r=await fetch('/api/identity'); const j=await r.json();
  if(document.activeElement!==$("idname")) $("idname").value=j.name||'';
  if(document.activeElement!==$("idhost")) $("idhost").value=j.host||''; }catch(_){} }
async function setIdentity(){
  const n=$("idname").value.trim(), h=$("idhost").value.trim();
  if(!n && !h){ alert('Enter a name and/or hostname'); return; }
  if(!confirm('Rename to "'+n+'" / '+h+'.local and reboot?')) return;
  try{
    const r=await fetch('/api/identity?name='+encodeURIComponent(n)+'&host='+encodeURIComponent(h),{method:'POST'});
    const j=await r.json().catch(()=>({}));
    if(!r.ok){ $("idmsg").textContent='✗ '+(j.err||r.status); return; }
    logLine('identity set — rebooting…'); identityCountdown(j.host);
  }catch(e){ $("idmsg").textContent='request failed'; }
}
// After a rename: the device reboots; the OLD <host>.local stops resolving, so when
// reached by mDNS we navigate to the NEW hostname. By IP / via proxy the URL is
// unchanged, so we just reload. ~12s allows reboot + Wi-Fi + mDNS re-announce.
function identityCountdown(newHost){
  let s=12;
  const onLocal = location.hostname.endsWith('.local');
  const target = (onLocal && newHost) ? (location.protocol+'//'+newHost+'.local'+location.pathname+location.search) : null;
  (function step(){
    $("idmsg").textContent = 'rebooting — '+(target?('opening '+newHost+'.local'):'reloading')+' in '+s+'s…';
    if(s-- <= 0){ target ? location.assign(target) : location.reload(); }
    else setTimeout(step,1000);
  })();
}

function otaReloadCountdown(){          // page reload after a flash (pulls the new firmware's page)
  let s=10;
  (function step(){
    $("otaprog").textContent = 'rebooting — reloading in '+s+'s…';
    if(s-- <= 0) location.reload();
    else setTimeout(step,1000);
  })();
}
function otaUpload(){
  const f=$("otafile").files[0]; if(!f){ alert('Choose a .bin file'); return; }
  if(!confirm('Flash '+f.name+' ('+Math.round(f.size/1024)+' KB)? The device will reboot.')) return;
  const fd=new FormData(); fd.append('firmware',f,f.name);
  const x=new XMLHttpRequest(); x.open('POST','/api/ota');
  $("otaprog").textContent='starting…';
  // network upload progress (this finishes quickly; the device then writes flash)
  x.upload.onprogress=e=>{ $("otaprog").textContent = e.lengthComputable
      ? ('uploading '+Math.round(100*e.loaded/e.total)+'%')
      : ('uploading '+Math.round(e.loaded/1024)+' KB'); };
  x.upload.onload =()=>{ $("otaprog").textContent='upload complete — writing flash…'; };  // the silent slow part
  x.onload =()=>{ if(x.status==200) otaReloadCountdown();
                  else $("otaprog").textContent='failed: '+x.responseText; };
  // a successful flash may reboot before the response is read -> treat as success + reload
  x.onerror=()=>{ $("otaprog").textContent='connection closed (likely flashed)…'; otaReloadCountdown(); };
  x.send(fd);
}

renderCal(); whoami(); loadIdentity(); connect(); setInterval(tick,500);
</script>
</body>
</html>
)rawliteral";
