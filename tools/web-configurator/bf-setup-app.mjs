import {SerialTransport} from './serial.mjs';
import {FlightSetup, unpackPosition, packPosition} from './bf-setup.mjs';
const $ = id => document.getElementById(id);
let transport, setup, snapshot, reviewed, busy = false;
for (let i = 0; i < 4; i++) {
  const row = document.createElement('div'); row.className = 'settings-grid';
  row.innerHTML = `<label class="check"><input id="enable-${i}" type="checkbox">Custom Message ${i + 1}</label><label>X<input id="x-${i}" type="number" min="0" max="47"></label><label>Y<input id="y-${i}" type="number" min="0" max="30"></label><span id="old-${i}" class="hint"></span>`;
  $('rows').append(row);
}
const rows = () => Array.from({length: 4}, (_, i) => ({x: Number($(`x-${i}`).value), y: Number($(`y-${i}`).value), enabled: $(`enable-${i}`).checked}));
function controls() {
  $('read').disabled = busy || !('serial' in navigator) || !window.isSecureContext;
  $('close').disabled = busy || !transport; $('placement').disabled = busy || !snapshot || setup?.uncertain;
  $('apply').disabled = busy || !reviewed || !$('ground').checked; $('export').disabled = busy || !snapshot;
}
function invalidate() { reviewed = null; $('ground').checked = false; $('proposal').textContent = '變更後請更新預覽。'; controls(); }
function show(value) {
  snapshot = value; reviewed = null; $('ground').checked = false;
  $('status').textContent = `Betaflight API ${value.api} · 畫布 ${value.canvas.cols} × ${value.canvas.rows} · OSD 設定檔 ${value.profile}\n目前未解鎖，未進入 FAILSAFE。`;
  $('modes').textContent = value.modes.filter(m => m.id >= 40 && m.id <= 43).map(m => `USER${m.id - 39}: AUX${m.aux} ${m.low}–${m.high}`).join('；') || '沒有讀到 USER1–4 的有效 AUX 區間；ARM 模式可直接使用飛控 ARM，其他模式需先指派 USER。';
  value.positions.forEach((raw, i) => {
    const p = unpackPosition(raw, value.profile); $(`x-${i}`).value = p.x; $(`y-${i}`).value = p.y; $(`enable-${i}`).checked = p.enabled;
    $(`old-${i}`).textContent = `目前：${p.enabled ? '顯示' : '隱藏'} (${p.x}, ${p.y})`;
  });
  $('proposal').textContent = '尚未提出變更。'; controls();
}
async function run(action) {
  if (busy) return; busy = true; controls();
  try { await action(); } catch (error) {
    reviewed = null; $('status').textContent = error.message;
    if (!setup || setup.uncertain) {
      try { await transport?.close(); } catch {}
      transport = setup = snapshot = null;
      $('status').textContent += '\n請確認飛控重新上電後再連接；未確認的變更不可視為已保存。';
    }
  }
  finally { busy = false; controls(); }
}
$('read').onclick = () => run(async () => {
  if (!transport) { transport = new SerialTransport(await navigator.serial.requestPort()); await transport.open(); setup = new FlightSetup(transport); }
  show(await setup.read());
});
$('close').onclick = () => run(async () => { await transport.close(); transport = setup = snapshot = reviewed = null; $('status').textContent = '已斷線；設定完成後請重新上電並確認眼鏡畫面與錄影起停。'; });
$('preset').onclick = () => { for (let i = 0; i < 4; i++) { $(`x-${i}`).value = 1; $(`y-${i}`).value = i; $(`enable-${i}`).checked = true; } invalidate(); };
$('preview').onclick = () => run(async () => {
  const value = rows(); value.forEach((row, i) => packPosition(snapshot.positions[i], row, snapshot.profile, snapshot.canvas));
  reviewed = value;
  $('proposal').textContent = value.map((p, i) => `Custom Message ${i + 1}: ${$(`old-${i}`).textContent} → ${p.enabled ? '顯示' : '隱藏'} (${p.x}, ${p.y})`).join('\n');
  $('ground').checked = false;
});
$('rows').addEventListener('input', invalidate); $('ground').onchange = controls;
$('apply').onclick = () => run(async () => { if (!reviewed || !$('ground').checked) return; show(await setup.apply(snapshot, reviewed)); $('status').textContent += '\n四行位置已保存並讀回一致；請重新上電後確認眼鏡顯示。'; });
$('export').onclick = () => { const link = document.createElement('a'); link.href = URL.createObjectURL(new Blob([JSON.stringify({captured_at: new Date().toISOString(), flight_controller: snapshot}, null, 2)], {type: 'application/json'})); link.download = 'rotorrec-betaflight-check.json'; link.click(); setTimeout(() => URL.revokeObjectURL(link.href), 1000); };
window.addEventListener('beforeunload', e => { if (transport) { e.preventDefault(); e.returnValue = ''; } });
controls();
