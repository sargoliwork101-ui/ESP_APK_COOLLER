// ========== Cooler ESP32 — Android WebView App ==========
// منطق دقیقاً مثل نسخه‌ی اصلی، با این تفاوت که درخواست‌ها به آدرس IP قابل‌تنظیم
// برد ارسال می‌شوند (چه از طریق AP خود برد 192.168.4.1 و چه از طریق LAN مودم).
// همچنین از Android Native Bridge (اگر موجود باشد) برای درخواست‌های HTTP استفاده
// می‌شود تا مشکل CORS و Cleartext (http روی اندروید ۹+) رفع شود.

const APP_NAME = 'کولر هوشمند ESP32';
const APP_TAGLINE = 'ESP32 · TIMER HUB';

let ESP_IP = '192.168.4.1'; // پیش‌فرض: IP استاندارد AP در حالت ESP32
const ESP_IP_KEY = 'cooler_esp_ip';

// ========== بومی‌سازی پل اندروید ==========
// اندروید یک شیء به نام AndroidBridge با متد request(method, url, body) تزریق می‌کند
// که رشته‌ی JSON خروجی می‌دهد (یا برای POSTهای بدون نیاز به پاسخ، "OK").
// در وب معمولی/مرورگر از fetch بومی استفاده می‌کنیم.
function hasAndroidBridge(){
  return typeof window.AndroidBridge !== 'undefined' && typeof window.AndroidBridge.request === 'function';
}

// یک fetch ساده‌ساز که روی اندروید از Bridge و در غیر این صورت از fetch استاندارد استفاده می‌کند.
function espFetch(path, options = {}){
  const method = (options.method || 'GET').toUpperCase();
  const url = 'http://' + ESP_IP + path;
  let body = options.body || null;

  if(hasAndroidBridge()){
    return new Promise((resolve, reject)=>{
      try{
        const bodyStr = body ? (typeof body === 'string' ? body : new URLSearchParams(body).toString()) : '';
        const resp = window.AndroidBridge.request(method, url, bodyStr || '');
        if(resp === null || resp === undefined){ reject(new Error('no response')); return; }
        resolve({
          ok: !resp.startsWith('ERR:'),
          text: ()=>Promise.resolve(resp.startsWith('ERR:') ? resp.substring(4) : resp),
          json: ()=>Promise.resolve(JSON.parse(resp.startsWith('ERR:') ? resp.substring(4) : resp))
        });
      }catch(e){ reject(e); }
    });
  } else {
    const opts = { method: method, headers: {} };
    if(body){
      if(typeof body === 'string'){ opts.headers['Content-Type'] = 'application/json'; opts.body = body; }
      else { opts.body = new URLSearchParams(body).toString(); opts.headers['Content-Type'] = 'application/x-www-form-urlencoded'; }
    }
    return fetch(url, opts);
  }
}

function espPostForm(path, formData){
  const params = new URLSearchParams();
  if(formData instanceof FormData){
    for(const [k,v] of formData.entries()) params.append(k, v);
  } else if(typeof formData === 'object'){
    for(const k in formData) params.append(k, formData[k]);
  }
  return espFetch(path, { method:'POST', body: params.toString() });
}

// ========== ذخیره/بارگذاری IP برد ==========
function loadEspIp(){
  try{
    const saved = localStorage.getItem(ESP_IP_KEY);
    if(saved && saved.trim()){ ESP_IP = saved.trim(); }
  }catch(e){}
}
function saveEspIp(ip){
  ESP_IP = ip.trim();
  try{ localStorage.setItem(ESP_IP_KEY, ESP_IP); }catch(e){}
}
function saveEspIpAndReconnect(){
  const inp = document.getElementById('esp-ip');
  if(!inp) return;
  const v = inp.value.trim();
  if(!v){ showModal('لطفاً آدرس IP برد را وارد کنید.'); return; }
  saveEspIp(v);
  updateIpDisplay();
  closeIpModal();
  lastStaState = null;
  resetActivity();
  fetchStatus();
  showToast('آدرس برد ذخیره شد و در حال اتصال...', 'info');
}
function updateIpDisplay(){
  const el = document.getElementById('ip-current');
  if(el) el.innerText = 'آدرس ذخیره‌شده: ' + ESP_IP;
  const inp = document.getElementById('esp-ip');
  if(inp && !inp.value) inp.value = ESP_IP;
}

function openIpModal(){
  updateIpDisplay();
  const m=document.getElementById('ip-modal');
  const o=document.getElementById('custom-alert-overlay');
  if(m) m.style.display='block';
  if(o){ o.style.display='block'; o.onclick=closeIpModal; }
}
function closeIpModal(){
  const m=document.getElementById('ip-modal');
  const o=document.getElementById('custom-alert-overlay');
  if(m) m.style.display='none';
  if(o){ o.style.display='none'; o.onclick=closeAlert; }
}

// ========== Initialize scenario rows (empty slots up to 20) ==========
// دقیقاً همان HTML که در handleRoot() به ازای هر سناریو در حلقه تولید می‌شود،
// اما در حالت پیش‌فرض همه display:none / scenario-disabled هستند (چون active=false).
const MAX_SCENARIOS = 20;
function buildScenarioCards(){
  const list = document.getElementById('scenarios-list');
  if(!list) return;
  list.innerHTML = '';
  const dayNames = ['ش','ی','د','س','چ','پ','ج'];
  for(let i=0;i<MAX_SCENARIOS;i++){
    // در C++ در حالت غیرفعال، shStr/ehStr خالی هستند و displayStyle='display:none;'
    // checked=''  enabledChecked=''  switchClass='switch'  cardClass='scenario-card scenario-disabled'
    const card = document.createElement('div');
    card.className = 'scenario-card scenario-disabled';
    card.id = 'row_'+i;
    card.style.display = 'none';
    card.innerHTML = `
      <div class='scenario-top'>
        <div class='scenario-name'>سناریو ${i+1} </div>
        <div class='scenario-actions'>
          <div class='switch' onclick='toggleScenarioEnabled(${i})'><div class='knob'></div></div>
          <div class='icon-btn danger' onclick='removeScenario(${i})' title='حذف سناریو'>✕</div>
        </div>
      </div>
      <div class='time-row'>
        <div class='time-box'><div class='lbl'>روشن</div><input type='time' name='sh_${i}' value='' step='60'></div>
        <div class='time-box'><div class='lbl'>خاموش</div><input type='time' name='eh_${i}' value='' step='60'></div>
      </div>
      <div class='days-label'>روزهای اجرا</div>
      <div class='days-row'>
        ${dayNames.map((dn,day)=>`<button type='button' class='day-btn' data-day='${day}' onclick='toggleScenarioDay(${i},${day})'>${dn}</button>`).join('')}
      </div>
      <input type='hidden' name='wd_${i}' value='127'>
      <input type='checkbox' name='act_${i}' value='1' style='display:none;'>
      <input type='checkbox' name='en_${i}' value='1' style='display:none;'>
    `;
    // توجه: در حالت اولیه (inactive) روزها selected نیستند — درست مثل خروجی C++ که
    // day-btn ها بدون کلاس selected هستند. وقتی سناریو add می‌شود یا از برد خوانده
    // می‌شود، روزها بر اساس wd مقداردهی می‌شوند.
    list.appendChild(card);
  }
}
function buildTxPowerButtons(){
  const row = document.getElementById('tx-power-row');
  if(!row) return;
  row.innerHTML = '';
  const labels = ['کم','متوسط','زیاد','حداکثر'];
  for(let p=0;p<=3;p++){
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'power-btn' + (p===3?' selected':'');
    btn.dataset.power = p;
    btn.textContent = labels[p];
    btn.onclick = ()=>selectTxPower(p);
    row.appendChild(btn);
  }
}

// ========== UI helpers ==========
function showModal(msg){
  document.getElementById('custom-alert-text').innerText = msg;
  document.getElementById('custom-alert-overlay').style.display = 'block';
  document.getElementById('custom-alert').style.display = 'block';
}
function closeAlert(){
  document.getElementById('custom-alert-overlay').style.display = 'none';
  document.getElementById('custom-alert').style.display = 'none';
}

function showToast(msg, kind){
  kind = kind || 'info';
  const wrap = document.getElementById('toast-wrap');
  if(!wrap) return;
  const el = document.createElement('div');
  el.className = 'toast toast-' + kind;
  el.innerText = msg;
  wrap.appendChild(el);
  requestAnimationFrame(()=>el.classList.add('show'));
  setTimeout(()=>{ el.classList.remove('show'); setTimeout(()=>el.remove(), 300); }, 3800);
}

function togglePasswordVisibility(){
  const passInput = document.getElementById('wifi-pass');
  const eyeIcon = document.getElementById('eye-icon');
  if(!passInput) return;
  if(passInput.type === 'password'){
    passInput.type = 'text';
    if(eyeIcon) eyeIcon.innerHTML = '<path d=\"M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24\"/><line x1=\"1\" y1=\"1\" x2=\"23\" y2=\"23\"/>';
  } else {
    passInput.type = 'password';
    if(eyeIcon) eyeIcon.innerHTML = '<path d=\"M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z\"/><circle cx=\"12\" cy=\"12\" r=\"3\"/>';
  }
}

function switchTab(tabId){
  document.querySelectorAll('.nav-btn').forEach(b=>b.classList.remove('active'));
  document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));
  if(tabId==='home'){ document.getElementById('tab-btn-home').classList.add('active'); document.getElementById('tab-home').classList.add('active'); }
  else if(tabId==='scenarios'){ document.getElementById('tab-btn-scenarios').classList.add('active'); document.getElementById('tab-scenarios').classList.add('active'); updateScenariosManualNote(); }
  else if(tabId==='wifi'){ document.getElementById('tab-btn-wifi').classList.add('active'); document.getElementById('tab-wifi').classList.add('active'); }
}

let swipeStartX=0, swipeStartY=0, swipeEligible=false;
const pageOrder=['home','scenarios','wifi'];
document.addEventListener('touchstart', function(e){
  if(e.touches.length!==1) return;
  const target=e.target;
  swipeEligible=!target.closest('input,button,.switch,.fab,.icon-btn');
  swipeStartX=e.touches[0].clientX; swipeStartY=e.touches[0].clientY;
},{passive:true});
document.addEventListener('touchend', function(e){
  if(!swipeEligible || !e.changedTouches.length) return;
  const dx=e.changedTouches[0].clientX-swipeStartX, dy=e.changedTouches[0].clientY-swipeStartY;
  if(Math.abs(dx)<65 || Math.abs(dx)<Math.abs(dy)*1.4) return;
  const active=document.querySelector('.page.active');
  const current=active ? active.id.replace('tab-','') : 'home';
  const index=pageOrder.indexOf(current);
  const next=dx<0 ? index+1 : index-1;
  if(next>=0 && next<pageOrder.length) switchTab(pageOrder[next]);
},{passive:true});

function updateScenariosManualNote(){
  const note=document.getElementById('manual-disabled-note');
  if(!note) return;
  const mb=document.getElementById('manual-btn');
  const manualOn = mb && !mb.classList.contains('isoff');
  note.style.display = manualOn ? 'block' : 'none';
}

function syncTime(){
  let now = new Date();
  espPostForm('/sync', {
    h: now.getHours(), m: now.getMinutes(), s: now.getSeconds(),
    y: now.getFullYear(), mon: now.getMonth()+1, d: now.getDate(),
    wd: (now.getDay()+1)%7
  }).then(r=>r.text()).then(()=>showModal('ساعت داخلی دستگاه با موفقیت با گوشی شما همگام‌سازی شد.'))
    .catch(()=>showModal('خطا در همگام‌سازی ساعت. از اتصال گوشی به برد مطمئن شوید.'));
}

function toggleManual(){
  let btn = document.getElementById('manual-btn');
  if(btn){
    btn.classList.toggle('isoff');
    btn.innerText = btn.classList.contains('isoff') ? 'روشن کردن دستی کولر' : 'خاموش کردن دستی کولر';
  }
  updateScenariosManualNote();
  espFetch('/toggle-manual', {method:'POST'}).catch(()=>{
    showToast('خطا در ارسال فرمان به برد', 'bad');
  });
}

function toggleScenarioDay(index, day){
  const row=document.getElementById('row_'+index); if(!row) return;
  const input=row.querySelector('input[name="wd_'+index+'"]');
  const btn=row.querySelector('.day-btn[data-day="'+day+'"]'); if(!input || !btn) return;
  let mask=parseInt(input.value,10); if(isNaN(mask)) mask=127;
  const bit=1<<day;
  if(mask & bit){ if((mask & 127)===bit){ showModal('حداقل یک روز برای اجرای سناریو انتخاب کنید.'); return; } mask &= ~bit; }
  else mask |= bit;
  input.value=mask; btn.classList.toggle('selected',(mask&bit)!==0);
}

function toggleScenarioEnabled(index){
  const row = document.getElementById('row_'+index);
  if(!row) return;
  const en = row.querySelector('input[name="en_'+index+'"]');
  const sw = row.querySelector('.switch');
  if(!en || !sw) return;
  en.checked = !en.checked;
  if(en.checked){
    row.classList.remove('scenario-disabled');
    sw.classList.add('on');
  } else {
    row.classList.add('scenario-disabled');
    row.classList.remove('running','next');
    sw.classList.remove('on');
  }
  updateScenarioOrder();
}

function toggleNet(sw){
  const cb = document.getElementById('internet-enabled');
  if(!cb) return;
  cb.checked = !cb.checked;
  sw.classList.toggle('on', cb.checked);
}

function toggleApCycleSwitch(sw){
  const cb = document.getElementById('ap-cycle-enabled');
  if(!cb) return;
  cb.checked = !cb.checked;
  sw.classList.toggle('on', cb.checked);
}

function selectTxPower(level){
  const row=document.getElementById('tx-power-row');
  const input=document.getElementById('tx-power-value');
  if(!row || !input) return;
  input.value=level;
  row.querySelectorAll('.power-btn').forEach(btn=>btn.classList.toggle('selected', parseInt(btn.dataset.power,10)===level));
}

function updateScenarioOrder(){
  const rows = Array.from(document.querySelectorAll('.scenario-card'));
  rows.forEach(row=>{
    const visible = row.style.display !== 'none';
    if(!visible){ row.style.order = 10000; return; }
    const sh = row.querySelector('input[name^="sh_"]');
    const eh = row.querySelector('input[name^="eh_"]');
    let w = -1;
    if(sh && sh.value && eh && eh.value){
      const p = sh.value.split(':');
      if(p.length===2) w = parseInt(p[0])*60 + parseInt(p[1]);
    }
    row.style.order = w;
  });
  const vis = rows.filter(r=>r.style.display!=='none');
  vis.sort((a,b)=>parseInt(a.style.order)-parseInt(b.style.order));
  vis.forEach((row,idx)=>{
    const name = row.querySelector('.scenario-name');
    if(name && name.childNodes.length) name.childNodes[0].nodeValue = 'سناریو '+(idx+1)+' ';
  });
}

function addScenario(){
  const rows = document.querySelectorAll('.scenario-card');
  for(let i=0;i<rows.length;i++){
    if(rows[i].style.display==='none'){
      rows[i].style.display='block';
      const cb=rows[i].querySelector('input[name^="act_"]'); if(cb) cb.checked=true;
      const en=rows[i].querySelector('input[name^="en_"]'); if(en) en.checked=true;
      rows[i].classList.remove('scenario-disabled');
      const sw=rows[i].querySelector('.switch'); if(sw) sw.classList.add('on');
      const sh=rows[i].querySelector('input[name^="sh_"]'); if(sh) sh.value="";
      const eh=rows[i].querySelector('input[name^="eh_"]'); if(eh) eh.value="";
      const wd=rows[i].querySelector('input[name^="wd_"]'); if(wd) wd.value=127;
        rows[i].querySelectorAll('.day-btn').forEach(b=>b.classList.add('selected'));
      updateScenarioOrder();
      return;
    }
  }
  showModal('حداکثر ظرفیت مجاز (۲۰ سناریو) را اضافه کرده‌اید.');
}

function removeScenario(index){
  const row = document.getElementById('row_'+index);
  if(row){
    row.style.display='none';
    const cb=row.querySelector('input[name^="act_"]'); if(cb) cb.checked=false;
    const en=row.querySelector('input[name^="en_"]'); if(en) en.checked=false;
    row.classList.remove('scenario-disabled','running','next');
    const sw=row.querySelector('.switch'); if(sw) sw.classList.remove('on');
    const sh=row.querySelector('input[name^="sh_"]'); if(sh) sh.value="";
    const eh=row.querySelector('input[name^="eh_"]'); if(eh) eh.value="";
    updateScenarioOrder();
  }
}

function scenarioRowsToData(){
  const list=[];
  for(let i=0;i<MAX_SCENARIOS;i++){
    const row=document.getElementById('row_'+i);
    if(!row || row.style.display==='none') continue;
    const sh=row.querySelector('input[name^="sh_"]');
    const eh=row.querySelector('input[name^="eh_"]');
    if(!sh || !eh || !sh.value || !eh.value) continue;
    const a=sh.value.split(':'), b=eh.value.split(':');
    if(a.length!==2||b.length!==2) continue;
    const en=row.querySelector('input[name^="en_"]'); const wd=row.querySelector('input[name^="wd_"]');
    list.push({sh:+a[0],sm:+a[1],eh:+b[0],em:+b[1],en:!en||en.checked,wd:wd?parseInt(wd.value,10):127});
  }
  return list;
}
function exportScenarios(){
  const backup={version:1,type:'esp32-cooler-scenarios',exportedAt:new Date().toISOString(),scenarios:scenarioRowsToData()};
  const blob=new Blob([JSON.stringify(backup,null,2)],{type:'application/json'});
  const url=URL.createObjectURL(blob), a=document.createElement('a');
  const now=new Date(); const stamp=now.getFullYear()+'-'+String(now.getMonth()+1).padStart(2,'0')+'-'+String(now.getDate()).padStart(2,'0');
  a.href=url; a.download='cooler-scenarios-'+stamp+'.json'; document.body.appendChild(a); a.click(); a.remove(); URL.revokeObjectURL(url);
  showModal('فایل پشتیبان سناریوها در گوشی شما دانلود شد.');
}
function importScenarios(event){
  const file=event.target.files && event.target.files[0]; if(!file) return;
  const reader=new FileReader();
  reader.onload=function(){
    try{
      const parsed=JSON.parse(reader.result); const data=Array.isArray(parsed)?parsed:parsed.scenarios;
      if(!Array.isArray(data) || data.length>MAX_SCENARIOS) throw new Error('format');
      for(const x of data){
        if(!x || !Number.isInteger(x.sh)||!Number.isInteger(x.sm)||!Number.isInteger(x.eh)||!Number.isInteger(x.em)||x.sh<0||x.sh>23||x.eh<0||x.eh>23||x.sm<0||x.sm>59||x.em<0||x.em>59||x.sh===x.eh&&x.sm===x.em) throw new Error('data');
      }
      for(let i=0;i<MAX_SCENARIOS;i++) removeScenario(i);
      data.forEach((x,i)=>{
        const row=document.getElementById('row_'+i); if(!row) return;
        row.style.display='block'; row.querySelector('input[name^="act_"]').checked=true;
        const en=row.querySelector('input[name^="en_"]'); en.checked=x.en!==false;
        const sw=row.querySelector('.switch'); sw.classList.toggle('on',en.checked); row.classList.toggle('scenario-disabled',!en.checked);
        row.querySelector('input[name^="sh_"]').value=String(x.sh).padStart(2,'0')+':'+String(x.sm).padStart(2,'0');
        row.querySelector('input[name^="eh_"]').value=String(x.eh).padStart(2,'0')+':'+String(x.em).padStart(2,'0');
        const wd=row.querySelector('input[name^="wd_"]'); const mask=Number.isInteger(x.wd)&&x.wd>=1&&x.wd<=127?x.wd:127; wd.value=mask;
        row.querySelectorAll('.day-btn').forEach(btn=>btn.classList.toggle('selected',(mask&(1<<parseInt(btn.dataset.day,10)))!==0));
      });
      updateScenarioOrder(); showModal('سناریوها از فایل خوانده شدند. برای اعمال روی برد، دکمه «ذخیره و پیاده‌سازی برنامه‌ها» را بزنید.');
    }catch(err){ showModal('فایل پشتیبان معتبر نیست یا با این برنامه سازگار نیست.'); }
    event.target.value='';
  }; reader.readAsText(file,'UTF-8');
}

function validateForm(/*event*/){
  document.querySelectorAll('.scenario-card').forEach(row=>row.classList.remove('error-conflict'));
  let activeScenarios=[];
  for(let i=0;i<MAX_SCENARIOS;i++){
    const row=document.getElementById('row_'+i);
    if(row && row.style.display!=='none'){
      const enInput=row.querySelector('input[name^="en_"]');
      const scenarioEnabled = !enInput || enInput.checked;
      const sh=row.querySelector('input[name^="sh_"]').value;
      const eh=row.querySelector('input[name^="eh_"]').value;
      const hasStart=(sh!=="");
      const hasEnd=(eh!=="");
      if((hasStart&&!hasEnd)||(!hasStart&&hasEnd)||(!hasStart&&!hasEnd)){
        row.classList.add('error-conflict');
        showModal('لطفاً هر دو کادر زمان روشن شدن و خاموش شدن را در تمام سناریوها به دقت پر کنید.');
        return false;
      }
      if(hasStart&&hasEnd){
        let sp=sh.split(':'), ep=eh.split(':');
        if(sp.length===2&&ep.length===2){
          let startMin=parseInt(sp[0])*60+parseInt(sp[1]);
          let endMin=parseInt(ep[0])*60+parseInt(ep[1]);
          if(startMin===endMin){
            row.classList.add('error-conflict');
            showModal('در یکی از سناریوها زمان روشن و خاموش شدن یکسان است. این کار مجاز نیست.');
            return false;
          }
          if(scenarioEnabled) activeScenarios.push({start:startMin,end:endMin,id:i});
        }
      }
    }
  }
  let timeMap=new Array(1440).fill(null);
  let conflictedIds=new Set();
  for(let s of activeScenarios){
    let st=s.start, en=s.end, mins=[];
    if(st<en){ for(let m=st;m<en;m++) mins.push(m); }
    else { for(let m=st;m<1440;m++) mins.push(m); for(let m=0;m<en;m++) mins.push(m); }
    for(let m of mins){
      if(timeMap[m]!==null){ conflictedIds.add(s.id); conflictedIds.add(timeMap[m]); }
      else timeMap[m]=s.id;
    }
  }
  if(conflictedIds.size>0){
    conflictedIds.forEach(id=>{ const r=document.getElementById('row_'+id); if(r) r.classList.add('error-conflict'); });
    showModal('تداخل زمانی! بازه‌های سناریوها روی هم افتاده‌اند. لطفا زمان سناریوهای مشخص شده با کادر قرمز رنگ را اصلاح کنید.');
    return false;
  }
  return true;
}

function saveApForm(event){
  event.preventDefault();
  const form=event.target;
  const passInput=form.querySelector('input[name="pass"]');
  if(passInput && passInput.value.length<8){ showModal('رمز شبکه (AP) خود برد باید حداقل ۸ کاراکتر باشد.'); return false; }
  const modal=document.getElementById('progress-modal');
  const overlay=document.getElementById('custom-alert-overlay');
  const fill=document.getElementById('progress-bar-fill');
  const pct=document.getElementById('progress-percent');
  const title=document.getElementById('progress-title');
  const text=document.getElementById('progress-text');
  title.innerText='در حال ذخیره تنظیمات فرستنده...';
  text.innerText='تنظیمات ذخیره شد و برد در حال راه‌اندازی مجدد است. لطفاً منتظر بمانید؛ پس از راه‌اندازی مجدد برد، اگر آدرس IP عوض شد از بخش «تنظیمات اتصال به برد» آدرس جدید را وارد کنید.';
  fill.style.width='100%'; pct.innerText='100%';
  overlay.style.display='block'; modal.style.display='block';
  waitingWifiReboot=true; wifiWasDisconnected=false;
  switchTab('home');
  const fd=new FormData(form);
  espPostForm('/save-ap', fd)
    .then(r=>r.text()).then(()=>{
      // برد ری‌استارت می‌شود؛ چند ثانیه بعد دوباره تلاش می‌کنیم.
    })
    .catch(()=>{
      waitingWifiReboot=false; wifiWasDisconnected=false;
      if(modal) modal.style.display='none'; if(overlay) overlay.style.display='none';
      showModal('خطا در ذخیره تنظیمات فرستنده! لطفاً دوباره تلاش کنید.');
    });
  return false;
}

function saveStaForm(event){
  event.preventDefault();
  const form=event.target;
  const passInput=form.querySelector('input[name="sta_pass"]');
  if(passInput && passInput.value.length>0 && passInput.value.length<8){ showModal('رمز وای‌فای مودم اگر وارد شود باید حداقل ۸ کاراکتر باشد.'); return false; }
  const onInput=form.querySelector('input[name="sta_on_minutes"]');
  const offInput=form.querySelector('input[name="sta_off_minutes"]');
  const onVal=onInput?Number(onInput.value):NaN;
  const offVal=offInput?Number(offInput.value):NaN;
  if(!Number.isInteger(onVal) || onVal<1 || onVal>1440 || !Number.isInteger(offVal) || offVal<0 || offVal>1440){ showModal('زمان روشن بودن STA باید عدد صحیح بین ۱ تا ۱۴۴۰ و زمان خاموش بودن عدد صحیح بین ۰ تا ۱۴۴۰ دقیقه باشد.'); return false; }
  const statusEl=document.getElementById('sta-status-inline');
  const cycleEl=document.getElementById('sta-cycle-status-inline');
  const internetModeEl=document.getElementById('internet-mode-inline');
  if(statusEl) statusEl.innerText='در حال ذخیره و اعمال اتصال مودم...';
  if(cycleEl) cycleEl.innerText='در حال ذخیره...';
  if(internetModeEl) internetModeEl.innerText='در حال ذخیره...';
  const fd=new FormData(form);
  const internetEnabled=document.getElementById('internet-enabled');
  fd.set('internet', (internetEnabled && internetEnabled.checked) ? '1' : '0');
  espPostForm('/save-sta', fd)
    .then(r=>r.text()).then(()=>{ showModal('تنظیمات مودم / اینترنت ذخیره شد.'); resetActivity(); fetchStatus(); })
    .catch(()=>{ if(statusEl) statusEl.innerText='خطا'; if(cycleEl) cycleEl.innerText='خطا'; if(internetModeEl) internetModeEl.innerText='خطا'; showModal('خطا در ذخیره تنظیمات مودم / اینترنت!'); });
  return false;
}

function saveProtectionForm(event){
  event.preventDefault(); const form=event.target; const input=form.querySelector('input[name="min_off"]');
  const value=input ? Number(input.value) : NaN;
  if(!Number.isInteger(value) || value<0 || value>1440){ showModal('زمان محافظت باید یک عدد صحیح بین ۰ تا ۱۴۴۰ دقیقه باشد.'); return false; }
  espPostForm('/save-protection', new FormData(form))
    .then(r=>r.text()).then(()=>{showModal('تنظیمات محافظت کمپرسور ذخیره شد.'); fetchStatus();})
    .catch(()=>showModal('خطا در ذخیره تنظیمات محافظت!'));
  return false;
}

function saveApCycleForm(event){
  event.preventDefault();
  const form=event.target;
  const onInput=form.querySelector('input[name="on_minutes"]');
  const offInput=form.querySelector('input[name="off_minutes"]');
  const onVal=onInput?Number(onInput.value):NaN;
  const offVal=offInput?Number(offInput.value):NaN;
  if(!Number.isInteger(onVal) || onVal<1 || onVal>1440 || !Number.isInteger(offVal) || offVal<1 || offVal>1440){ showModal('مدت روشن و خاموش بودن هرکدام باید عددی صحیح بین ۱ تا ۱۴۴۰ دقیقه باشد.'); return false; }
  const cycleEnabled=document.getElementById('ap-cycle-enabled');
  const fd=new FormData(form);
  fd.set('cycle_enabled', (cycleEnabled && cycleEnabled.checked) ? '1' : '0');
  espPostForm('/save-ap-cycle', fd)
    .then(r=>r.text()).then(()=>{ showModal('تنظیمات چرخه AP ذخیره شد.'); fetchStatus(); })
    .catch(()=>showModal('خطا در ذخیره تنظیمات چرخه AP!'));
  return false;
}

function saveScenariosForm(event){
  event.preventDefault();
  if(!validateForm(event)) return false;
  const modal=document.getElementById('progress-modal');
  const overlay=document.getElementById('custom-alert-overlay');
  const fill=document.getElementById('progress-bar-fill');
  const pct=document.getElementById('progress-percent');
  const title=document.getElementById('progress-title');
  const text=document.getElementById('progress-text');
  title.innerText="در حال ذخیره سناریوها...";
  text.innerText="لطفاً منتظر بمانید، اطلاعات به طور امن روی حافظه بورد نوشته می‌شود.";
  fill.style.width='0%'; pct.innerText='0%';
  overlay.style.display='block'; modal.style.display='block';
  let val=0;
  let intId=setInterval(()=>{ if(val<90){ val+=Math.floor(Math.random()*8)+4; if(val>90) val=90; fill.style.width=val+'%'; pct.innerText=val+'%'; } },120);
  let valid=[];
  for(let i=0;i<MAX_SCENARIOS;i++){
    const row=document.getElementById('row_'+i);
    if(row && row.style.display!=='none'){
      const enInput=row.querySelector('input[name^="en_"]');
      const enabled=!enInput||enInput.checked;
      const shVal=row.querySelector('input[name^="sh_"]').value;
      const ehVal=row.querySelector('input[name^="eh_"]').value;
      if(shVal&&ehVal){
        let sp=shVal.split(':'), ep=ehVal.split(':');
        if(sp.length===2&&ep.length===2){
          let sh=parseInt(sp[0],10), sm=parseInt(sp[1],10), eh=parseInt(ep[0],10), em=parseInt(ep[1],10);
          if(sh!==eh||sm!==em){ const wdInput=row.querySelector('input[name^="wd_"]'); let wd=wdInput ? parseInt(wdInput.value,10) : 127; if(isNaN(wd)||wd<1||wd>127) wd=127; valid.push({sh:sh,sm:sm,eh:eh,em:em,en:enabled,wd:wd}); }
        }
      }
    }
  }
  valid.sort((a,b)=>(a.sh*60+a.sm)-(b.sh*60+b.sm));
  espFetch('/save',{method:'POST',body:JSON.stringify(valid)})
    .then(r=>{ clearInterval(intId); fill.style.width='100%'; pct.innerText='100%'; title.innerText="ذخیره‌سازی موفقیت‌آمیز بود ✅"; text.innerText="برنامه‌ریزی جدید سناریوها بدون قطع ارتباط روی بورد ثبت شد."; setTimeout(()=>{ modal.style.display='none'; overlay.style.display='none'; resetActivity(); fetchStatus(); updateScenarioOrder(); },1600); })
    .catch(err=>{ clearInterval(intId); modal.style.display='none'; overlay.style.display='none'; showModal('خطا در برقراری ارتباط با برد!'); resetActivity(); });
  return false;
}

function highlightScenarios(boardTimeStr, currentWeekday){
  if(!boardTimeStr || boardTimeStr==='--:--:--') return;
  let parts=boardTimeStr.split(':');
  if(parts.length<2) return;
  let cur=parseInt(parts[0])*60+parseInt(parts[1]);
  let cards=document.querySelectorAll('.scenario-card');
  cards.forEach(c=>{
    c.classList.remove('running','next');
    const nm=c.querySelector('.scenario-name');
    if(nm){ const old=nm.querySelector('.badge-live,.badge-next'); if(old) old.remove(); }
  });
  let data=[];
  cards.forEach(c=>{
    if(c.style.display!=='none' && !c.classList.contains('scenario-disabled')){
      let sh=c.querySelector('input[name^="sh_"]'), eh=c.querySelector('input[name^="eh_"]');
      if(sh&&eh&&sh.value&&eh.value&&!c.classList.contains('error-conflict')){
        let sp=sh.value.split(':'), ep=eh.value.split(':');
        if(sp.length===2&&ep.length===2){ let s=parseInt(sp[0])*60+parseInt(sp[1]); let e=parseInt(ep[0])*60+parseInt(ep[1]); if(s!==e){ const wdInput=c.querySelector('input[name^="wd_"]'); let wd=wdInput ? parseInt(wdInput.value,10) : 127; if(isNaN(wd)) wd=127; data.push({card:c,s:s,e:e,wd:wd}); } }
      }
    }
  });
  let anyRunning=false, next=null, minDiff=Infinity;
  data.forEach(d=>{
    let running = d.s<d.e ? (cur>=d.s&&cur<d.e) : (cur>=d.s||cur<d.e);
    let scenarioDay=(d.s>d.e && cur<d.e) ? ((currentWeekday+6)%7) : currentWeekday;
    running = running && ((d.wd & (1<<scenarioDay))!==0);
    if(running){ d.card.classList.add('running'); anyRunning=true; const nm=d.card.querySelector('.scenario-name'); const b=document.createElement('span'); b.className='badge-live'; b.textContent='در حال اجرا'; nm.appendChild(b); }
    else { let diff=d.s-cur; if(diff<0) diff+=1440; if(diff<minDiff){ minDiff=diff; next=d; } }
  });
  if(!anyRunning && next){ next.card.classList.add('next'); const nm=next.card.querySelector('.scenario-name'); const b=document.createElement('span'); b.className='badge-next'; b.textContent='بعدی'; nm.appendChild(b); }
}

function gregorianToJalali(gy, gm, gd){
  const gdm=[0,31,59,90,120,151,181,212,243,273,304,334];
  let gy2=(gm>2)?gy+1:gy;
  let days=355666+365*gy+Math.floor((gy2+3)/4)-Math.floor((gy2+99)/100)+Math.floor((gy2+399)/400)+gd+gdm[gm-1];
  let jy=-1595+33*Math.floor(days/12053); days%=12053;
  jy+=4*Math.floor(days/1461); days%=1461;
  if(days>365){ jy+=Math.floor((days-1)/365); days=(days-1)%365; }
  let jm, jd;
  if(days<186){ jm=1+Math.floor(days/31); jd=1+(days%31); }
  else { jm=7+Math.floor((days-186)/30); jd=1+((days-186)%30); }
  return [jy,jm,jd];
}
function toFaDigits(n){
  const map = ['۰','۱','۲','۳','۴','۵','۶','۷','۸','۹'];
  return String(n).replace(/\d/g, d=>map[+d]);
}
function formatJalaliDate(gy,gm,gd){
  const j=gregorianToJalali(gy,gm,gd);
  return toFaDigits(j[0])+'/'+toFaDigits(String(j[1]).padStart(2,'0'))+'/'+toFaDigits(String(j[2]).padStart(2,'0'));
}
function formatDuration(totalSeconds){
  if(totalSeconds==null || totalSeconds<0) return '--';
  const days = Math.floor(totalSeconds / 86400);
  const hours = Math.floor((totalSeconds % 86400) / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  if(days > 0) return toFaDigits(days) + ' روز و ' + toFaDigits(hours) + ' ساعت';
  if(hours > 0) return toFaDigits(hours) + ' ساعت و ' + toFaDigits(minutes) + ' دقیقه';
  if(minutes > 0) return toFaDigits(minutes) + ' دقیقه';
  return toFaDigits(totalSeconds) + ' ثانیه';
}
function pad2(n){ return String(n).padStart(2,'0'); }

function formatNtpSuccessStamp(data){
  if(!data || data.ntpLastValid!==1) return 'هنوز دریافت نشده';
  const j=gregorianToJalali(data.ntpYear, data.ntpMonth, data.ntpDay);
  return toFaDigits(j[0])+'/'+toFaDigits(pad2(j[1]))+'/'+toFaDigits(pad2(j[2]))+' - '+toFaDigits(pad2(data.ntpHour))+':'+toFaDigits(pad2(data.ntpMinute))+':'+toFaDigits(pad2(data.ntpSecond));
}

function applyWifiFormDefaults(data){
  // این مقادیر فقط در اولین اتصال از روی /status می‌آیند.
  // اگر کاربر در حال تایپ در فیلدها باشد، پر نکنند (روی مقدار کاربر نوشته نشود).
  const skipFill = isAnyInputFocused() && _configFormFilled;
  const setVal = (id, val)=>{
    if(skipFill) return;
    const el = document.getElementById(id);
    if(el && !el.dataset.userTouched){
      if(el.type === 'checkbox'){ el.checked = !!val; }
      else { el.value = val; }
    }
  };
  setVal('ap-on-min', data.apOnMinutes);
  setVal('ap-off-min', data.apOffMinutes);
  setVal('sta-on-min', data.staOnMinutes);
  setVal('sta-off-min', data.staOffMinutes);
  setVal('prot-min-off', data.protectionMinutes);
  const inetCb = document.getElementById('internet-enabled');
  const inetSw = document.getElementById('internet-switch');
  if(inetCb && !skipFill){ inetCb.checked = (data.internetEnabled===1); if(inetSw) inetSw.classList.toggle('on', inetCb.checked); }
  const apCb = document.getElementById('ap-cycle-enabled');
  const apSw = document.getElementById('ap-cycle-switch');
  if(apCb && !skipFill){ apCb.checked = (data.apCycleEnabled===1); if(apSw) apSw.classList.toggle('on', apCb.checked); }
  selectTxPower(data.apTxPowerLevel||3);
  // ردیابی لمس کاربر روی فیلدهای متنی/عددی که نباید overwrite شوند
  ['ap-ssid','sta-ssid','wifi-pass','sta-pass','esp-ip','ap-on-min','ap-off-min','sta-on-min','sta-off-min','prot-min-off']
    .forEach(id=>{
      const el = document.getElementById(id);
      if(el && !el._tracked){ el._tracked=true; el.addEventListener('input',()=>{el.dataset.userTouched='1';}); }
    });
}

function loadConfigFromDevice(){
  // در فیرم‌ویر جدید /config تنظیمات AP/SSID (بدون پسورد) و SSID مودم را می‌دهد تا فرم‌ها از قبل پر شوند.
  espFetch('/config').then(r=>r.json()).then(cfg=>{
    const fillIfUntouched = (id,val)=>{
      const el = document.getElementById(id);
      if(el && val && !el.dataset.userTouched) el.value = val;
    };
    fillIfUntouched('ap-ssid', cfg.apSsid);
    fillIfUntouched('sta-ssid', cfg.staSsid);
    if(cfg.deviceName){
      const t = document.getElementById('app-title'); if(t && !t.dataset.custom) t.innerText = cfg.deviceName;
    }
    _configFormFilled = true;
  }).catch(()=>{ /* نادیده بگیر — قدیمی بودن فیرم‌ویر یا قطع بودن */ });
}

// سناریوها را هم از روی برد می‌خوانیم تا فرم تب سناریوها از قبل پر باشد.
function loadScenariosFromDevice(){
  espFetch('/scenarios').then(r=>r.json()).then(list=>{
    if(!Array.isArray(list)) return;
    for(let i=0;i<MAX_SCENARIOS;i++) removeScenario(i);
    list.forEach((x,i)=>{
      const row=document.getElementById('row_'+i); if(!row) return;
      row.style.display='block';
      const act=row.querySelector('input[name^="act_"]'); if(act) act.checked=true;
      const en=row.querySelector('input[name^="en_"]');
      const enabled = x.en!==false;
      if(en) en.checked=enabled;
      const sw=row.querySelector('.switch');
      if(sw) sw.classList.toggle('on', enabled);
      row.classList.toggle('scenario-disabled', !enabled);
      const sh=row.querySelector('input[name^="sh_"]');
      const eh=row.querySelector('input[name^="eh_"]');
      if(sh) sh.value=pad2(x.sh||0)+':'+pad2(x.sm||0);
      if(eh) eh.value=pad2(x.eh||0)+':'+pad2(x.em||0);
      const wd=row.querySelector('input[name^="wd_"]');
      const mask = (typeof x.wd === 'number') ? x.wd : 127;
      if(wd) wd.value = mask;
      row.querySelectorAll('.day-btn').forEach(btn=>btn.classList.toggle('selected',(mask&(1<<parseInt(btn.dataset.day,10)))!==0));
    });
    updateScenarioOrder();
  }).catch(()=>{});
}

let lastActivity=Date.now();
let currentInterval=1000;
let pingTimeoutId=null;
let waitingWifiReboot=false, wifiWasDisconnected=false;
let lastStaState=null;
let configLoadedOnce = false;
let _configFormFilled = false; // فقط بار اول از روی /config فیلدها پر شوند
function isAnyInputFocused(){
  return document.activeElement && document.activeElement.tagName === 'INPUT';
}

function resetActivity(){
  lastActivity=Date.now();
  if(currentInterval!==1000){ currentInterval=1000; if(pingTimeoutId) clearTimeout(pingTimeoutId); fetchStatus(); }
}
['click','touchstart','input','change','scroll'].forEach(evt=>document.addEventListener(evt,resetActivity,{passive:true}));

function fetchStatus(){
  if(document.hidden){ if(pingTimeoutId) clearTimeout(pingTimeoutId); pingTimeoutId=setTimeout(fetchStatus,currentInterval); return; }
  let reqStart=Date.now();
  espFetch('/status?t='+reqStart)
    .then(r=>{ if(!r.ok) throw new Error('bad'); return r.json(); })
    .then(data=>{
      let ping=Date.now()-reqStart;
      let badge=document.getElementById('connection-status');
      badge.className='conn-badge connected';
      const ct = document.getElementById('conn-text'); if(ct) ct.textContent='متصل · '+toFaDigits(ping)+'ms';
      document.getElementById('board-time').innerText=data.time;
      const dayNames=['شنبه','یکشنبه','دوشنبه','سه‌شنبه','چهارشنبه','پنجشنبه','جمعه'];
      const dateEl=document.getElementById('board-date');
      if(dateEl && data.year!=null) dateEl.innerText='امروز: '+dayNames[data.weekday]+' — '+formatJalaliDate(data.year,data.month,data.day);

      if(waitingWifiReboot && wifiWasDisconnected){
        waitingWifiReboot=false; wifiWasDisconnected=false;
        const _m=document.getElementById('progress-modal'); const _o=document.getElementById('custom-alert-overlay');
        if(_m) _m.style.display='none'; if(_o) _o.style.display='none';
        showToast('اتصال مجدد برقرار شد ✅', 'good');
      }

      let hero=document.getElementById('cooler-display-status');
      let statusText=document.getElementById('cooler-status-text');
      let statusSub=document.getElementById('cooler-status-sub');
      let syncWarning=document.getElementById('sync-warning');
      let manualBtn=document.getElementById('manual-btn');
      let internetWifiStatus=document.getElementById('internet-wifi-status');

      const staState = data.staState;
      let staText, staClass;
      if(data.internetEnabled!==1){ staText='غیرفعال چون اینترنت خاموش است'; staClass='status-connecting'; }
      else if(data.staConfigured!==1){ staText='تنظیم نشده'; staClass='status-connecting'; }
      else if(staState===4){ staText='طبق زمان‌بندی موقتاً قطع است'; staClass='status-connecting'; }
      else if(staState===2){ staText='متصل - '+data.staIp; staClass='status-connected'; }
      else if(staState===3){ staText='⚠️ قطع شده - در حال تلاش مجدد...'; staClass='status-disconnected'; }
      else if(staState===1){ staText='در حال اتصال...'; staClass='status-connecting'; }
      else { staText='در حال بررسی...'; staClass='status-connecting'; }

      let staCycleText, staCycleClass;
      if(data.internetEnabled!==1){ staCycleText='غیرفعال'; staCycleClass='status-connecting'; }
      else if(data.staConfigured!==1){ staCycleText='تا قبل از وارد کردن SSID مودم فعال نمی‌شود'; staCycleClass='status-connecting'; }
      else if(data.staOffMinutes===0){ staCycleText='دائم روشن (زمان خاموشی = ۰)'; staCycleClass='status-connected'; }
      else if(data.staPhaseOn===1){ staCycleText='روشن — '+toFaDigits(Math.ceil(data.staRemaining/60))+' دقیقه تا قطع دوره‌ای'; staCycleClass='status-connected'; }
      else { staCycleText='خاموش — '+toFaDigits(Math.ceil(data.staRemaining/60))+' دقیقه تا وصل مجدد'; staCycleClass='status-connecting'; }

      let internetModeText, internetModeClass;
      if(data.internetEnabled===1){
        if(data.staConfigured!==1){ internetModeText='فعال است، اما مودم تنظیم نشده'; internetModeClass='status-connecting'; }
        else if(data.staOffMinutes===0){ internetModeText='فعال — اتصال مودم دائماً روشن است'; internetModeClass='status-connected'; }
        else { internetModeText='فعال — طبق زمان‌بندی STA'; internetModeClass='status-connected'; }
      } else { internetModeText='غیرفعال'; internetModeClass='status-connecting'; }

      if(internetWifiStatus){ internetWifiStatus.textContent=staText; internetWifiStatus.className=staClass; }
      const staCycleHome=document.getElementById('sta-cycle-status');
      if(staCycleHome){ staCycleHome.textContent=staCycleText; staCycleHome.className=staCycleClass; }
      const internetModeHome=document.getElementById('internet-mode-status');
      if(internetModeHome){ internetModeHome.textContent=internetModeText; internetModeHome.className=internetModeClass; }
      const staInline=document.getElementById('sta-status-inline');
      if(staInline){ staInline.textContent=staText; staInline.className=staClass; }
      const staCycleInline=document.getElementById('sta-cycle-status-inline');
      if(staCycleInline){ staCycleInline.textContent=staCycleText; staCycleInline.className=staCycleClass; }
      const internetModeInline=document.getElementById('internet-mode-inline');
      if(internetModeInline){ internetModeInline.textContent=internetModeText; internetModeInline.className=internetModeClass; }

      if(lastStaState!==null && lastStaState!==staState && data.internetEnabled===1 && data.staConfigured===1){
        if(staState===3){ showToast('⚠️ اتصال STA به مودم اینترنت قطع شد', 'bad'); }
        else if(staState===2 && lastStaState===3){ showToast('✅ اتصال STA به مودم دوباره برقرار شد', 'good'); }
        else if(staState===2 && lastStaState===1){ showToast('✅ اتصال STA به مودم برقرار شد', 'good'); }
      }
      lastStaState=staState;

      const apCycleEl=document.getElementById('ap-cycle-status');
      if(apCycleEl){
        if(!data.apCycleEnabled){ apCycleEl.textContent='غیرفعال — فرستنده همیشه روشن است'; apCycleEl.style.color='var(--text-dim)'; }
        else if(data.apOn && data.apClientConnected){ apCycleEl.textContent='روشن — دستگاهی متصل است، چرخه متوقف مانده'; apCycleEl.style.color='var(--scenario-live)'; }
        else if(data.apOn){ apCycleEl.textContent='روشن — '+toFaDigits(Math.ceil(data.apRemaining/60))+' دقیقه تا خاموش‌شدن'; apCycleEl.style.color='var(--scenario-live)'; }
        else { apCycleEl.textContent='در حال خاموش بودن طبق چرخه'; apCycleEl.style.color='var(--warn)'; }
      }

      const protectionEl=document.getElementById('protection-status');
      if(protectionEl){
        if(data.protectionRemaining>0){ protectionEl.textContent='فعال — '+toFaDigits(Math.ceil(data.protectionRemaining/60))+' دقیقه تا اجازه روشن‌شدن'; protectionEl.style.color='var(--warn)'; }
        else if(data.protectionMinutes>0){ protectionEl.textContent='فعال — فاصله تنظیم‌شده: '+toFaDigits(data.protectionMinutes)+' دقیقه'; protectionEl.style.color='var(--scenario-live)'; }
        else { protectionEl.textContent='غیرفعال'; protectionEl.style.color='var(--text-dim)'; }
      }

      const ntpStampText=formatNtpSuccessStamp(data);
      const ntpColor = data.ntpLastValid===1 ? 'var(--scenario-live)' : 'var(--warn)';
      const ntpHome=document.getElementById('ntp-last-update');
      if(ntpHome){ ntpHome.innerText=ntpStampText; ntpHome.style.color=ntpColor; }
      const ntpInline=document.getElementById('ntp-last-update-inline');
      if(ntpInline){ ntpInline.innerText=ntpStampText; ntpInline.style.color=ntpColor; }

      const switchCountEl=document.getElementById('relay-switch-count');
      if(switchCountEl && data.switchCount!=null) switchCountEl.innerText=toFaDigits(data.switchCount)+' بار';
      const onDurationEl=document.getElementById('relay-on-duration');
      if(onDurationEl && data.onSeconds!=null) onDurationEl.innerText=formatDuration(data.onSeconds);

      if(data.sync===1){ syncWarning.innerText='ساعت همگام‌سازی شده و سناریوها فعال هستند.'; syncWarning.style.color='var(--on)'; }
      else { syncWarning.innerText='⚠️ ساعت برد همگام نیست! سناریوها تا زمان همگام‌سازی اجرا نخواهند شد.'; syncWarning.style.color='var(--off)'; }

      if(data.relay===1){ hero.className='panel hero on'; statusText.innerText='کولر روشن است'; }
      else { hero.className='panel hero off'; statusText.innerText='کولر خاموش است'; }

      if(statusSub) statusSub.innerText = data.protectionRemaining>0 ? 'محافظت کمپرسور: '+toFaDigits(Math.ceil(data.protectionRemaining/60))+' دقیقه تا روشن‌شدن' : (data.override===1 ? 'حالت: دستی (روشن)' : 'حالت: خودکار (سناریو)');

      if(manualBtn){
        if(data.override===1 && manualBtn.classList.contains('isoff')){ manualBtn.classList.remove('isoff'); manualBtn.innerText='خاموش کردن دستی کولر'; }
        else if(data.override===0 && !manualBtn.classList.contains('isoff')){ manualBtn.classList.add('isoff'); manualBtn.innerText='روشن کردن دستی کولر'; }
      }

      const manualNote=document.getElementById('manual-disabled-note');
      if(manualNote) manualNote.style.display = (data.override===1) ? 'block' : 'none';

      if(!configLoadedOnce){
        applyWifiFormDefaults(data);
        loadConfigFromDevice();
        loadScenariosFromDevice();
        configLoadedOnce = true;
      }

      if(data.sync===1 && data.override===0) highlightScenarios(data.time, data.weekday);
      else document.querySelectorAll('.scenario-card').forEach(r=>{ r.classList.remove('running','next'); const nm=r.querySelector('.scenario-name'); if(nm){ const old=nm.querySelector('.badge-live,.badge-next'); if(old) old.remove(); } });

      queueNextPing();
    })
    .catch(err=>{
      let badge=document.getElementById('connection-status');
      badge.className='conn-badge disconnected';
      if(waitingWifiReboot) wifiWasDisconnected=true;
      const ct = document.getElementById('conn-text'); if(ct) ct.textContent='قطع ارتباط!';
      let hero=document.getElementById('cooler-display-status');
      let statusText=document.getElementById('cooler-status-text');
      if(hero) hero.className='panel hero disconnected';
      if(statusText) statusText.innerText='ارتباط با برد قطع شده است';
      document.querySelectorAll('.scenario-card').forEach(r=>{ r.classList.remove('running','next'); const nm=r.querySelector('.scenario-name'); if(nm){ const old=nm.querySelector('.badge-live,.badge-next'); if(old) old.remove(); } });
      queueNextPing();
    });
}

function queueNextPing(){
  if(pingTimeoutId) clearTimeout(pingTimeoutId);
  let since=Date.now()-lastActivity;
  currentInterval = since>30000 ? 3000 : 1000;
  pingTimeoutId=setTimeout(fetchStatus,currentInterval);
}

// ========== Boot ==========
document.addEventListener('DOMContentLoaded', ()=>{
  document.getElementById('app-title').innerText = APP_NAME;
  document.getElementById('app-subtitle').innerText = APP_TAGLINE;
  buildScenarioCards();
  buildTxPowerButtons();
  loadEspIp();
  updateIpDisplay();
  updateScenarioOrder();
  const list=document.getElementById('scenarios-list');
  if(list){
    list.addEventListener('change', updateScenarioOrder);
    list.addEventListener('input', function(e){ updateScenarioOrder(); let row=e.target.closest('.scenario-card'); if(row) row.classList.remove('error-conflict'); });
  }
  fetchStatus();
  document.addEventListener('visibilitychange',()=>{ if(!document.hidden) resetActivity(); });
});
