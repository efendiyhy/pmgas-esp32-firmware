#pragma once
#include <Arduino.h>
// Dashboard disimpan di header terpisah untuk menghindari Arduino IDE
// salah mendeteksi fungsi JavaScript sebagai fungsi C++ ketika
// melakukan pra-pemrosesan (automatic prototype generation) sketch .ino.
// File ini WAJIB berada satu folder dengan sketch .ino.
const char WEB_PAGE[] PROGMEM = R"ESPHTML(
<!DOCTYPE html><html lang="id"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>EK Instrumentation - Live Monitor</title>
<style>
:root{--bg:#170c2e;--surface:#29164c;--surface2:#392364;--stroke:#9671ec;--text:#faf5ff;--sub:#c4b2df;--accent:#d3a1ff;--green:#78f3bc;--red:#ff778e;--gold:#ffe073}*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 75% -12%,#502885,#170c2e 65%);color:var(--text);font:14px/1.5 system-ui,-apple-system,'Segoe UI',sans-serif}button,input{font:inherit}button{cursor:pointer}.wrap{max-width:1250px;margin:auto;padding:25px}.header{display:flex;align-items:center;justify-content:space-between;gap:15px;flex-wrap:wrap}.brand{font-weight:850;letter-spacing:1px;font-size:13px;color:var(--accent)}h1{font-size:clamp(25px,4vw,35px);letter-spacing:-.7px;margin:2px 0}.muted{color:var(--sub)}.pills{display:flex;gap:8px;flex-wrap:wrap}.pill{border:1px solid var(--stroke);border-radius:5px;padding:6px 12px;background:var(--surface)}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--red);margin-right:6px}.on .dot{background:var(--green)}nav{display:flex;gap:7px;flex-wrap:wrap;margin:25px 0}nav button,.btn{border:1px solid var(--stroke);color:var(--text);background:var(--surface2);border-radius:2px;padding:10px 16px}nav button.active,.btn.primary{background:var(--accent);border-color:var(--accent);color:#160d22;font-weight:800}.view{display:none}.view.active{display:block}.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}.card,.pane{background:#29164c;border:2px solid var(--accent);border-radius:2px;padding:20px;box-shadow:5px 5px 0 #05010c}.kpi-label{color:var(--sub);font-size:12px}.kpi-number{font-size:30px;font-weight:800;letter-spacing:-1px;margin-top:7px}.kpi-unit{font-size:13px;font-weight:500;color:var(--sub);margin-left:5px}.section{margin-top:18px}.grid2{display:grid;grid-template-columns:1.5fr 1fr;gap:16px}.toolbar{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:10px}.graph{width:100%;height:300px;display:block}.chartbtn{background:transparent;color:var(--sub);border:1px solid var(--stroke);border-radius:2px;padding:6px 10px}.chartbtn.active{color:#160d22;background:var(--accent)}h2{margin:0 0 13px;font-size:18px}.pane p{margin-top:0}label{font-size:12px;color:var(--sub);display:block;margin-top:14px;margin-bottom:5px}input[type=text],input[type=password],input[type=url]{background:#1b1034;border:1px solid var(--stroke);border-radius:2px;color:var(--text);padding:11px 12px;width:100%;outline:none}input:focus{border-color:var(--accent)}input[type=checkbox]{accent-color:var(--accent);transform:scale(1.15);margin-right:8px}.formrow{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:17px}.stats{display:grid;grid-template-columns:repeat(2,1fr);gap:12px}.stat{background:#231343;border:1px solid var(--stroke);padding:12px;border-radius:2px}.stat span{display:block;color:var(--sub);font-size:12px}.stat b{font-size:20px}.notice{margin-top:13px;color:var(--gold);font-size:12px}.msg{margin-top:13px;min-height:20px;color:var(--green)}footer{margin:35px 0;color:var(--sub);font-size:12px}small{font-size:12px}code{color:var(--accent);word-break:break-word}@media(max-width:830px){.grid2{grid-template-columns:1fr}.wrap{padding:16px}.graph{height:240px}}@media(max-width:460px){.kpis{grid-template-columns:repeat(2,minmax(0,1fr))}.kpi-number{font-size:23px}.card{padding:14px}}

/* Patch REV4: legenda di luar kanvas; pilihan Wi-Fi dari hasil scan. */
.chartlegend{display:flex;flex-wrap:wrap;align-items:center;gap:9px 16px;min-height:32px;padding:11px 0 7px;border-top:1px solid var(--stroke)}
.legenditem{display:inline-flex;gap:7px;align-items:center;color:var(--text);font-size:12px;min-width:0;max-width:100%;overflow-wrap:anywhere}
.legendswatch{display:inline-block;flex:0 0 20px;height:4px;border-radius:1px}
select{background:#1b1034;border:1px solid var(--stroke);border-radius:2px;color:var(--text);padding:11px 12px;width:100%;outline:none;min-width:0}
select:focus{border-color:var(--accent)}
select option{color:#faf5ff;background:#29164c}
button:disabled{opacity:.5;cursor:wait}
</style></head><body><main class="wrap">
<header class="header"><div><div class="brand">EK INSTRUMENTATION <span class="muted">/ REALTIME MONITOR</span></div><h1 id="deviceTitle">Monitoring</h1><div class="muted" id="deviceID">ESP32 · loading...</div></div>
<div class="pills"><span class="pill" id="wifiPill"><i class="dot"></i><span>WiFi offline</span></span><span class="pill" id="sdPill"><i class="dot"></i><span>SD tidak ada</span></span><span class="pill on"><i class="dot"></i>Monitoring aktif</span></div></header>
<nav><button class="active" data-view="dashboard">Dashboard</button><button data-view="wifi">WiFi</button><button data-view="sd">SD Card</button><button data-view="cloud">Supabase</button><button data-view="laptop">Server Laptop</button></nav>
<section class="view active" id="dashboard"><div class="kpis" id="sensorCards"></div>
<div class="grid2 section"><div class="pane"><div class="toolbar"><h2 style="margin:0;margin-right:auto">Grafik realtime</h2><div id="chartButtons" class="toolbar" style="margin:0"></div></div><canvas class="graph" id="plot"></canvas><div class="chartlegend" id="chartLegend" aria-label="Legenda grafik"></div><small class="muted">90 pembacaan terakhir · pembaruan 1 detik · tetap aktif tanpa internet</small></div>
<div class="pane"><h2>Ringkasan alat</h2><div class="stats"><div class="stat"><span>Data sejak boot</span><b id="recCount">-</b></div><div class="stat"><span>Waktu NTP</span><b id="clockStatus">-</b></div><div class="stat"><span>Supabase sukses</span><b id="cloudCount">-</b></div><div class="stat"><span>Laptop sukses</span><b id="lanCount">-</b></div></div><label>Label sampel (opsional)</label><input id="sampleTag" type="text" maxlength="24" placeholder="AUTO"><div class="formrow"><button class="btn primary" id="saveSample">Simpan label</button><span id="lastTimestamp" class="muted"></span></div></div></div></section>
<section class="view" id="wifi"><div class="grid2"><div class="pane"><h2>Hubungkan WiFi</h2><p class="muted">Opsional. Tanpa WiFi, sensor, dashboard hotspot, dan SD tetap bekerja.</p><label>Pilih jaringan Wi-Fi dari hasil scan</label><div class="formrow"><button class="btn" id="scanWifi" type="button">Cari / Scan Wi-Fi</button><span class="muted" id="scanStatus">Klik scan untuk menampilkan jaringan</span></div><select id="ssid"><option value="">— Pilih jaringan Wi-Fi —</option></select><label>Password WiFi</label><input id="wifiPass" type="password" placeholder="Password jaringan"><div class="formrow"><button class="btn primary" id="saveWifi">Simpan & hubungkan Wi-Fi</button></div><p class="notice">Untuk pengiriman ke laptop: simpan IP di menu Server Laptop terlebih dahulu, kemudian pilih Wi-Fi yang sama dengan laptop di sini.</p></div><div class="pane"><h2>Akses dashboard</h2><p>Hotspot alat selalu aktif:</p><p><strong id="apName">-</strong><br><code>http://192.168.4.1</code></p><p class="muted">Jaringan lokal: <code id="wifiIP">Belum terhubung</code></p><p class="notice">Hotspot dan WiFi router berjalan bersamaan. Password hotspot bawaan 12345678 sebaiknya diganti di firmware sebelum digunakan di tempat umum.</p></div></div></section>
<section class="view" id="sd"><div class="grid2"><div class="pane"><h2>Penyimpanan SD</h2><p class="muted">Jika kartu tersedia, setiap pembacaan otomatis dicatat ke CSV dan tertahan hingga berhasil dikirim.</p><div class="stats"><div class="stat"><span>Status</span><b id="sdStatus">-</b></div><div class="stat"><span>Ukuran CSV</span><b id="sdSize">-</b></div><div class="stat"><span>Error tulis</span><b id="sdErr">-</b></div><div class="stat"><span>Record sejak boot</span><b id="sdRecords">-</b></div></div><div class="formrow"><button class="btn primary" id="checkSD">Cek / pasang ulang SD</button><a class="btn" href="/api/export">Download CSV SD</a></div></div><div class="pane"><h2>Koneksi fisik SPI</h2><p><code>CS GPIO5 · SCK GPIO18 · MISO GPIO19 · MOSI GPIO23</code></p><p class="muted">Gunakan microSD 3.3V yang sesuai modul. Format FAT32. Jangan cabut saat data sedang ditulis.</p><p class="notice">Tanpa SD, tersedia antrean RAM maksimal 120 data per tujuan. Antrean hilang saat listrik mati; koneksi internet bukan syarat untuk memonitor.</p></div></div></section>
<section class="view" id="cloud"><div class="grid2"><div class="pane"><h2>Sinkronisasi Supabase</h2><p class="muted">Menggunakan proyek dan tabel asli alat ini. Data yang belum terkirim pada SD dikirim otomatis setelah WiFi/internet tersedia.</p><label><input type="checkbox" id="cloudToggle"> Aktifkan sinkronisasi cloud</label><div class="formrow"><button class="btn primary" id="saveCloud">Simpan pilihan</button></div><p class="notice">Ketika WiFi hanya terhubung ke router tanpa internet, data tetap menunggu di SD.</p></div><div class="pane"><h2>Status cloud</h2><div class="stats"><div class="stat"><span>Terkirim sejak boot</span><b id="cloudSent">-</b></div><div class="stat"><span>Gagal sejak boot</span><b id="cloudFailed">-</b></div></div><p class="muted">Posisi sinkronisasi SD (byte): <code id="cloudCursor">-</code></p></div></div></section>
<section class="view" id="laptop"><div class="grid2"><div class="pane"><h2>Integrasi webserver laptop</h2><p class="muted">ESP mengirim POST JSON yang seragam untuk alat PM dan gas. Server laptop dapat menggabungkan kedua sumber.</p><p class="notice">LANGKAH 1 — Isi IP laptop terlebih dahulu, simpan. LANGKAH 2 — Hubungkan ESP ke Wi-Fi yang sama dengan laptop melalui menu Wi-Fi.</p><label>Alamat IPv4 laptop (tanpa http://, port, atau path)</label><input type="text" inputmode="decimal" maxlength="15" id="lanIP" placeholder="192.168.1.100"><small class="muted">ESP otomatis menggunakan port 8787. Jangan isi localhost atau IP hotspot ESP.</small><label>Token perangkat (opsional, cocokkan di server laptop)</label><input type="password" id="lanToken" placeholder="Biarkan kosong jika tidak digunakan"><label><input id="lanToggle" type="checkbox"> Aktifkan pengiriman ke laptop</label><div class="formrow"><button id="saveLan" class="btn primary">1 · Simpan IP laptop</button><button id="goWifiFromLan" class="btn" type="button">2 · Pilih Wi-Fi</button></div><p class="notice">Simpan IP laptop sebelum menghubungkan ESP ke jaringan. Setelah itu sambungkan ESP dan laptop ke Wi-Fi/router yang sama (jaringan 2.4 GHz untuk ESP32 biasa). Gunakan DHCP reservation agar IP laptop tetap.</p></div><div class="pane"><h2>Status integrasi</h2><div class="stats"><div class="stat"><span>Terkirim sejak boot</span><b id="lanSent">-</b></div><div class="stat"><span>Gagal sejak boot</span><b id="lanFailed">-</b></div></div><p class="muted">Posisi sinkronisasi SD (byte): <code id="lanCursor">-</code></p><p class="muted">Data ke laptop dilengkapi <code>device_type</code>, <code>device_id</code>, <code>event_id</code>, waktu UTC, dan <code>measurements</code>.</p></div></div></section>
<div class="msg" id="msg" aria-live="polite"></div><footer>EK INSTRUMENTATION · ESP32 OFFLINE-FIRST · Monitoring otomatis sejak alat dinyalakan. Tidak memerlukan CDN/internet untuk menampilkan grafik.</footer>
</main><script>
'use strict';
const $=id=>document.getElementById(id);let currentFields=[],history=[],chartGroup=0,previousType='',initial=true;
const colors=['#49cfdf','#73a3ff','#ffcd76','#72dfab','#bd9afa','#ff8d9d'];
function human(n){return Number(n||0).toLocaleString('id-ID')}
function nfmt(v){return v==null||!Number.isFinite(v)?'—':Number(v).toLocaleString('id-ID',{maximumFractionDigits:3})}
function badge(id,ok,yes,no){const e=$(id);e.classList.toggle('on',!!ok);e.querySelector('span').textContent=ok?yes:no}
function message(s,ok=true){const el=$('msg');el.textContent=s;el.style.color=ok?'#58d8a3':'#ff777a'}
function groups(){return previousType==='pm'?[[0,1,2],[3,4,5],[6,7]]:[[0,1,2],[3,4],[5,6,7,8]]}
function groupLabels(){return previousType==='pm'?['PMS #1','PMS #2','Suhu & RH']:['Gas','Suhu & RH','Tegangan & Rasio']}
function makeCards(){const root=$('sensorCards');root.replaceChildren();currentFields.forEach((f,i)=>{const e=document.createElement('div');e.className='card';e.innerHTML='<div class="kpi-label"></div><div class="kpi-number"><span></span><span class="kpi-unit"></span></div>';e.querySelector('.kpi-label').textContent=f.name;e.querySelector('.kpi-unit').textContent=f.unit;e.id='sensor-'+i;root.appendChild(e)})}
function makeChartButtons(){const root=$('chartButtons');root.replaceChildren();groupLabels().forEach((name,i)=>{const b=document.createElement('button');b.className='chartbtn'+(i===chartGroup?' active':'');b.textContent=name;b.onclick=()=>{chartGroup=i;makeChartButtons();draw()};root.appendChild(b)})}
function makeLegend(){const el=$('chartLegend');el.replaceChildren();const indices=groups()[chartGroup]||[];indices.forEach((idx,j)=>{const item=document.createElement('span');item.className='legenditem';const swatch=document.createElement('span');swatch.className='legendswatch';swatch.style.backgroundColor=colors[j%colors.length];const label=document.createElement('span');label.textContent=currentFields[idx]?.name||('Sensor '+(idx+1));item.append(swatch,label);el.appendChild(item)})}
function draw(){
  const c=$('plot'),ctx=c.getContext('2d');if(!ctx)return;
  const rect=c.getBoundingClientRect(),w=Math.max(300,rect.width),h=Math.max(200,rect.height),dpr=window.devicePixelRatio||1;
  c.width=Math.round(w*dpr);c.height=Math.round(h*dpr);ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);
  const pad={l:50,r:18,t:15,b:38},W=w-pad.l-pad.r,H=h-pad.t-pad.b;
  const indices=groups()[chartGroup]||[];let finite=[];
  history.forEach(row=>indices.forEach(i=>{if(Number.isFinite(row[i+1]))finite.push(row[i+1])}));
  let lo=finite.length?Math.min(...finite):0,hi=finite.length?Math.max(...finite):1;
  if(lo===hi){lo-=1;hi+=1}
  const delta=(hi-lo)*.12;lo-=delta;hi+=delta;
  ctx.lineWidth=1;ctx.font='11px system-ui';
  for(let k=0;k<=4;k++){
    let y=pad.t+H*k/4;ctx.strokeStyle='#5b3e83';ctx.beginPath();ctx.moveTo(pad.l,y);ctx.lineTo(w-pad.r,y);ctx.stroke();
    ctx.fillStyle='#c4b2df';ctx.textAlign='right';ctx.fillText(nfmt(hi-(hi-lo)*k/4),pad.l-10,y+4)
  }
  ctx.textAlign='center';ctx.fillStyle='#c4b2df';ctx.fillText('90 pembacaan terbaru (detik)',pad.l+W/2,h-7);
  const length=Math.max(2,history.length);
  indices.forEach((index,coloridx)=>{
    ctx.strokeStyle=colors[coloridx%colors.length];ctx.lineWidth=2;ctx.beginPath();let started=false;
    history.forEach((row,i)=>{
      const val=row[index+1];if(!Number.isFinite(val)){started=false;return}
      const x=pad.l+(i/Math.max(1,length-1))*W,y=pad.t+(hi-val)/(hi-lo)*H;
      if(!started){ctx.moveTo(x,y);started=true}else ctx.lineTo(x,y)
    });ctx.stroke();
  });
  makeLegend();
  if(!finite.length){ctx.fillStyle='#c4b2df';ctx.textAlign='center';ctx.fillText('Menunggu data sensor...',w/2,h/2)}
}

async function post(path,data){try{const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});const j=await r.json();message(j.message||'Tidak ada respons',!!j.ok);if(!r.ok||!j.ok)throw new Error(j.message||'Permintaan gagal');return j}catch(e){message(String(e),false);return null}}
async function refresh(){try{const [r,h]=await Promise.all([fetch('/api/state',{cache:'no-store'}),fetch('/api/history',{cache:'no-store'})]);if(!r.ok||!h.ok)throw new Error('webserver offline');const s=await r.json(),hist=await h.json();if(s.type!==previousType){previousType=s.type;currentFields=s.fields;makeCards();makeChartButtons()}currentFields=s.fields;history=hist.values||[];$('deviceTitle').textContent=s.title;$('deviceID').textContent=s.device_id;badge('wifiPill',s.wifi_connected,'WiFi terhubung','WiFi offline');badge('sdPill',s.sd_ready,'SD terdeteksi','SD tidak ada');s.fields.forEach((f,i)=>{const el=$('sensor-'+i);if(el)el.querySelector('.kpi-number span').textContent=nfmt(f.value)});$('recCount').textContent=human(s.recorded);$('clockStatus').textContent=s.time_synced?'Sinkron':'Offline';$('cloudCount').textContent=human(s.cloud_sent);$('lanCount').textContent=human(s.lan_sent);$('lastTimestamp').textContent=s.timestamp?new Date(s.timestamp).toLocaleString('id-ID'):'Waktu absolut belum sinkron';$('apName').textContent=s.ap_ssid;$('wifiIP').textContent=s.wifi_ip;$('sdStatus').textContent=s.sd_ready?'TERDETEKSI':'TIDAK ADA';$('sdSize').textContent=(s.sd_bytes/1048576).toFixed(2)+' MB';$('sdErr').textContent=human(s.sd_errors);$('sdRecords').textContent=human(s.recorded);$('cloudSent').textContent=human(s.cloud_sent);$('cloudFailed').textContent=human(s.cloud_errors);$('cloudCursor').textContent=human(s.cloud_cursor);$('lanSent').textContent=human(s.lan_sent);$('lanFailed').textContent=human(s.lan_errors);$('lanCursor').textContent=human(s.lan_cursor);if(initial){savedSSID=s.wifi_ssid||'';setSSIDChoice(savedSSID);$('lanIP').value=(s.lan_url||'').replace(/^https?:\/\//,'').split(':')[0]||'';$('cloudToggle').checked=s.cloud_enabled;$('lanToggle').checked=s.lan_enabled;$('sampleTag').value=s.sample_name;initial=false}draw()}catch(e){message('Tidak bisa membaca sensor: '+e.message,false)}}
function openTab(name){const b=document.querySelector('nav button[data-view="'+name+'"]');if(!b)return;document.querySelectorAll('nav button').forEach(x=>x.classList.remove('active'));document.querySelectorAll('.view').forEach(x=>x.classList.remove('active'));b.classList.add('active');$(name).classList.add('active');draw();if(name==='wifi'&&!scannedOnce)scanWifi()}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>openTab(b.dataset.view));

let savedSSID='',scannedOnce=false,scanBusy=false;
function setSSIDChoice(ssid){if(!ssid)return;const box=$('ssid');if(![...box.options].some(o=>o.value===ssid)){const o=document.createElement('option');o.value=ssid;o.textContent=ssid+' (tersimpan)';box.appendChild(o)}box.value=ssid}
async function scanWifi(attempt=0){
  if(attempt===0){if(scanBusy)return;scanBusy=true;$('scanWifi').disabled=true;$('scanStatus').textContent='Memindai jaringan...'}
  try{
    const r=await fetch('/api/wifi/scan',{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);
    const data=await r.json();if(!data.ok)throw new Error(data.message||'Scan gagal');
    if(data.scanning){if(attempt>=20)throw new Error('Scan terlalu lama, coba ulang');setTimeout(()=>scanWifi(attempt+1),900);return}
    const box=$('ssid'),selected=box.value||savedSSID;box.replaceChildren();
    const blank=document.createElement('option');blank.value='';blank.textContent='— Pilih jaringan Wi-Fi —';box.appendChild(blank);
    for(const w of data.networks||[]){const o=document.createElement('option');o.value=w.ssid;o.textContent=w.ssid+'  ('+w.rssi+' dBm) '+(w.secure?'🔒':'Terbuka');box.appendChild(o)}
    setSSIDChoice(selected);if(!selected)box.value='';
    $('scanStatus').textContent=(data.networks||[]).length+' jaringan ditemukan';scannedOnce=true;
  }catch(e){$('scanStatus').textContent='Scan gagal: '+e.message;message('Gagal scan Wi-Fi: '+e.message,false)
  }finally{if(attempt===0 || attempt>=0){if(!$('scanStatus').textContent.includes('Memindai')){scanBusy=false;$('scanWifi').disabled=false}}}
}
$('scanWifi').onclick=()=>scanWifi();
$('saveWifi').onclick=()=>{const ssid=$('ssid').value;if(!ssid){message('Pilih jaringan dari hasil scan Wi-Fi terlebih dahulu',false);return}post('/api/wifi',{ssid,password:$('wifiPass').value})};
$('saveSample').onclick=()=>post('/api/sample',{sample_name:$('sampleTag').value});
$('checkSD').onclick=async()=>{await post('/api/sd/check',{});refresh()};
$('saveCloud').onclick=()=>post('/api/cloud',{enabled:$('cloudToggle').checked?'1':'0'});
$('saveLan').onclick=async()=>{const ip=$('lanIP').value.trim();if($('lanToggle').checked&&!ip){message('Masukkan IP laptop sebelum mengaktifkan pengiriman',false);return}await post('/api/laptop',{enabled:$('lanToggle').checked?'1':'0',ip,token:$('lanToken').value})};
$('goWifiFromLan').onclick=()=>{if(!$('lanIP').value.trim()){message('Masukkan dan simpan IP laptop dahulu',false);return}openTab('wifi')};
window.addEventListener('resize',draw);refresh();setInterval(refresh,1000);

</script></body></html>
)ESPHTML";
