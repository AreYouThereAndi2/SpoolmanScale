#pragma once

// The one page the bridge serves, at "/" and as the setup page of the access
// point. Everything inline: in setup mode there is no internet to load a
// stylesheet or script from, and the board has no file system for it.
// The page asks /api/status every few seconds and does the rest by fetch().
// German, like everything the user reads.

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Druckbrücke</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#6b7280;--line:#e2e5ea;--acc:#2563eb;--ok:#15803d;--bad:#b91c1c}
@media(prefers-color-scheme:dark){:root{--bg:#14171c;--card:#1d2128;--fg:#e8eaee;--mut:#9aa1ad;--line:#2d333c;--acc:#60a5fa;--ok:#4ade80;--bad:#f87171}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:680px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:4px 0 12px}
h2{font-size:16px;margin:0 0 10px}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin-bottom:14px}
dl{display:grid;grid-template-columns:max-content 1fr;gap:4px 14px;margin:0}
dt{color:var(--mut)}dd{margin:0;overflow-wrap:anywhere}
label{display:block;margin:8px 0 3px;color:var(--mut);font-size:13px}
input,select{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:7px;background:var(--bg);color:var(--fg);font:inherit}
button{padding:9px 14px;border:0;border-radius:7px;background:var(--acc);color:#fff;font:inherit;cursor:pointer;margin:10px 8px 0 0}
button.sec{background:transparent;color:var(--acc);border:1px solid var(--acc)}
button:disabled{opacity:.5;cursor:default}
.ok{color:var(--ok)}.bad{color:var(--bad)}.mut{color:var(--mut);font-size:13px}
.msg{margin-top:8px;min-height:1.2em}
ul.list{list-style:none;margin:8px 0 0;padding:0}
ul.list li{padding:8px 10px;border:1px solid var(--line);border-radius:7px;margin-bottom:6px;cursor:pointer}
ul.list li.sel{border-color:var(--acc);outline:1px solid var(--acc)}
progress{width:100%;margin-top:8px}
</style></head><body><main>
<h1>Druckbrücke</h1>

<section><h2>Status</h2><dl>
<dt>Drucker</dt><dd id="s_pr">-</dd>
<dt>Verbindung</dt><dd id="s_conn">-</dd>
<dt>Akku</dt><dd id="s_bat">-</dd>
<dt>Letzter Auftrag</dt><dd id="s_last">-</dd>
<dt>Aufträge</dt><dd id="s_jobs">-</dd>
<dt>WLAN</dt><dd id="s_wifi">-</dd>
<dt>Firmware</dt><dd id="s_fw">-</dd>
</dl>
<button id="b_test">Testetikett drucken</button>
<div class="msg" id="m_test"></div></section>

<section><h2>Drucker wählen</h2>
<p class="mut">Drucker einschalten und in die Nähe stellen, dann suchen. Die Suche dauert etwa 10 Sekunden.</p>
<button id="b_scan" class="sec">Drucker suchen</button>
<ul class="list" id="l_bt"></ul>
<label for="i_addr">Adresse</label><input id="i_addr" placeholder="80:52:D3:F8:B9:BC" autocomplete="off">
<label for="i_name">Name</label><input id="i_name" placeholder="PM240" autocomplete="off">
<label for="i_pin">PIN</label><input id="i_pin" value="0000" autocomplete="off">
<button id="b_save">Speichern</button><button id="b_forget" class="sec">Drucker vergessen</button>
<div class="msg" id="m_pr"></div></section>

<section><h2>WLAN</h2>
<button id="b_wscan" class="sec">Netzwerke suchen</button>
<ul class="list" id="l_wifi"></ul>
<label for="i_ssid">Netzwerk (SSID)</label><input id="i_ssid" autocomplete="off">
<label for="i_pass">Passwort</label><input id="i_pass" type="password" autocomplete="off">
<button id="b_wsave">Verbinden</button>
<div class="msg" id="m_wifi"></div></section>

<section><h2>Einstellungen</h2>
<label for="i_host">Hostname (ohne .local)</label><input id="i_host" autocomplete="off">
<label for="i_key">Neuer API-Schlüssel (leer lassen = unverändert)</label><input id="i_key" autocomplete="off">
<p class="mut">API-Schlüssel: <span id="s_auth">-</span>. Ist einer gesetzt, brauchen alle Aufrufe außer dem Status den Header <code>X-Api-Key</code>. Dieser Browser merkt sich den Schlüssel.</p>
<button id="b_set">Speichern</button><button id="b_nokey" class="sec">Schlüssel entfernen</button>
<div class="msg" id="m_set"></div></section>

<section><h2>Firmware aktualisieren</h2>
<input type="file" id="i_fw" accept=".bin">
<button id="b_fw">Hochladen</button>
<progress id="p_fw" max="100" value="0" hidden></progress>
<div class="msg" id="m_fw"></div></section>
</main>
<script>
const $=id=>document.getElementById(id);
let key='';try{key=localStorage.getItem('bridgeKey')||''}catch(e){}
let filled=false;
function setKey(k){key=k;try{localStorage.setItem('bridgeKey',k)}catch(e){}}
function msg(id,t,ok){const m=$(id);m.textContent=t;m.className='msg '+(ok===true?'ok':ok===false?'bad':'')}
async function api(path,opt,retry){
 opt=opt||{};opt.headers=opt.headers||{};if(key)opt.headers['X-Api-Key']=key;
 const r=await fetch(path,opt);
 if(r.status===401&&!retry){const k=prompt('API-Schlüssel der Druckbrücke:');if(k!==null){setKey(k);return api(path,opt,true)}}
 let j={};try{j=await r.json()}catch(e){j={ok:false,error:'HTTP '+r.status}}
 j.status=r.status;return j;
}
const ERR={'busy':'Gerade belegt (anderer Auftrag oder Suche).','no printer':'Kein Drucker gewählt.','connect failed':'Drucker nicht erreichbar. Eingeschaltet? In Reichweite? PIN richtig?','write failed':'Übertragung abgebrochen.','api key':'API-Schlüssel falsch.'};
function err(j){return ERR[j.error]||j.error||'Fehler'}
function ago(s,up){const d=up-s;return d<60?'vor '+d+' s':d<3600?'vor '+Math.round(d/60)+' min':'vor '+Math.round(d/3600)+' h'}
async function status(){
 try{const j=await api('/api/status');if(!j.ok)return;
 const p=j.printer;
 $('s_pr').textContent=p.address?((p.name||'Drucker')+' ('+p.address+')'+(p.paired?', gekoppelt':'')):'keiner gewählt';
 $('s_conn').textContent=j.busy?'druckt / sucht ...':p.connected?'verbunden':'getrennt (verbindet beim nächsten Auftrag)';
 $('s_bat').textContent=p.battery===null?'unbekannt':p.battery+' %';
 const l=j.last;
 $('s_last').textContent=l?((l.ok?'OK':'Fehler: '+err(l))+', '+l.bytes+' Bytes, '+l.ms+' ms, '+ago(l.at,j.uptime)):'noch keiner';
 $('s_jobs').textContent=j.jobs;
 const w=j.wifi;
 $('s_wifi').textContent=(w.ssid?w.ssid+', '+w.ip+' ('+w.rssi+' dBm), '+j.hostname+'.local':'nicht verbunden')+(w.ap?' | Einrichtungs-WLAN '+w.ap_name+' aktiv':'');
 $('s_fw').textContent=j.name+' '+j.version;
 $('s_auth').textContent=j.auth?'gesetzt':'keiner';
 if(!filled){filled=true;$('i_host').value=j.hostname;
  if(p.address){$('i_addr').value=p.address;$('i_name').value=p.name}
  if(!w.ssid)$('b_wscan').click()}
 }catch(e){}
}
$('b_test').onclick=async()=>{const b=$('b_test');b.disabled=true;msg('m_test','Drucke ...');
 try{const j=await api('/api/test',{method:'POST'});msg('m_test',j.ok?'Gedruckt: '+j.bytes+' Bytes in '+j.ms+' ms':err(j),j.ok)}catch(e){msg('m_test','Keine Antwort',false)}
 b.disabled=false;status()};
$('b_scan').onclick=async()=>{const b=$('b_scan');b.disabled=true;msg('m_pr','Suche läuft (etwa 10 s) ...');$('l_bt').innerHTML='';
 try{const j=await api('/api/scan');
  if(!j.ok){msg('m_pr',err(j),false)}else{
  j.devices.sort((a,b)=>(b.rssi||-999)-(a.rssi||-999));
  for(const d of j.devices){const li=document.createElement('li');li.textContent=(d.name||'(ohne Namen)')+'  '+d.address+(d.rssi!==null?'  '+d.rssi+' dBm':'');
   li.onclick=()=>{for(const x of $('l_bt').children)x.className='';li.className='sel';$('i_addr').value=d.address;$('i_name').value=d.name||''};
   $('l_bt').appendChild(li)}
  msg('m_pr',j.devices.length?'Drucker antippen, dann Speichern.':'Nichts gefunden. Drucker an?',j.devices.length>0)}
 }catch(e){msg('m_pr','Keine Antwort',false)}
 b.disabled=false};
async function savePrinter(body){
 try{const j=await api('/api/printer',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 msg('m_pr',j.ok?'Gespeichert.':err(j),j.ok)}catch(e){msg('m_pr','Keine Antwort',false)}
 status()}
$('b_save').onclick=()=>savePrinter({address:$('i_addr').value.trim().toUpperCase(),name:$('i_name').value.trim(),pin:$('i_pin').value.trim()||'0000'});
$('b_forget').onclick=()=>{if(confirm('Drucker vergessen und Kopplung löschen?')){$('i_addr').value='';$('i_name').value='';savePrinter({address:''})}};
$('b_wscan').onclick=async()=>{const b=$('b_wscan');b.disabled=true;msg('m_wifi','Suche ...');$('l_wifi').innerHTML='';
 try{const j=await api('/api/wifi/scan');
  if(!j.ok){msg('m_wifi',err(j),false)}else{
  for(const n of j.networks){const li=document.createElement('li');li.textContent=n.ssid+'  '+n.rssi+' dBm'+(n.open?'  offen':'');
   li.onclick=()=>{for(const x of $('l_wifi').children)x.className='';li.className='sel';$('i_ssid').value=n.ssid;$('i_pass').focus()};
   $('l_wifi').appendChild(li)}
  msg('m_wifi',j.networks.length?'':'Keine Netzwerke gefunden.')}
 }catch(e){msg('m_wifi','Keine Antwort',false)}
 b.disabled=false};
$('b_wsave').onclick=async()=>{const s=$('i_ssid').value;if(!s){msg('m_wifi','Bitte ein Netzwerk wählen.',false);return}
 try{const j=await api('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s,pass:$('i_pass').value})});
 msg('m_wifi',j.ok?'Gespeichert. Die Druckbrücke verbindet sich jetzt mit "'+s+'". Die neue Adresse steht gleich oben unter Status, solange dieses Gerät noch verbunden ist.':err(j),j.ok)}catch(e){msg('m_wifi','Keine Antwort',false)}};
async function saveSettings(body,newKey){
 try{const j=await api('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 if(j.ok){if(newKey!==null)setKey(newKey);$('i_key').value='';msg('m_set','Gespeichert. Erreichbar unter http://'+body.hostname+'.local/',true)}
 else msg('m_set',j.error==='hostname'?'Hostname: nur a-z, 0-9 und -, höchstens 32 Zeichen.':err(j),false)}catch(e){msg('m_set','Keine Antwort',false)}
 status()}
$('b_set').onclick=()=>{const k=$('i_key').value.trim(),b={hostname:$('i_host').value.trim().toLowerCase()};
 if(k)b.api_key=k;saveSettings(b,k?k:null)};
$('b_nokey').onclick=()=>{if(confirm('API-Schlüssel entfernen? Dann darf jeder im WLAN drucken und einstellen.'))saveSettings({hostname:$('i_host').value.trim().toLowerCase(),api_key:''},'')};
$('b_fw').onclick=()=>{const f=$('i_fw').files[0];if(!f){msg('m_fw','Bitte eine .bin-Datei wählen.',false);return}
 const fd=new FormData();fd.append('firmware',f,f.name);const x=new XMLHttpRequest();const p=$('p_fw');p.hidden=false;p.value=0;
 x.open('POST','/update');if(key)x.setRequestHeader('X-Api-Key',key);
 x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded*100/e.total};
 x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
  if(x.status===200&&j.ok){msg('m_fw','Fertig. Die Druckbrücke startet neu ...',true);setTimeout(()=>location.reload(),8000)}
  else msg('m_fw','Fehlgeschlagen: '+(err(j)||x.status),false)};
 x.onerror=()=>msg('m_fw','Verbindung abgebrochen',false);
 msg('m_fw','Lade hoch ...');x.send(fd)};
status();setInterval(status,4000);
</script></body></html>)HTML";
