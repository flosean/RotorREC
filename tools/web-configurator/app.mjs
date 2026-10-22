import { Client, CMD, decodeSettings, enterPassthrough, loadPackage, sleep } from './protocol.mjs';
import { SerialTransport } from './serial.mjs';

const $ = id => document.getElementById(id);
const phases = ['啟動中', '待機', '搜尋相機', '等待確認', '連接 Action 2', '請在相機確認配對', '等待相機狀態', '已連線', '重新連線中', '找不到相機', '不支援的服務', '配對遭拒', '狀態訂閱失敗', 'Link Pause'];
let transport = null, client = null, current = null, pkg = null, busy = false, flashing = false, cancelRequested = false, poll = null;
let lastPort = null, automaticDirect = false;
const supported = window.isSecureContext && 'serial' in navigator && 'DecompressionStream' in window;

function log(message) {
  const line = `[${new Date().toLocaleTimeString('zh-TW')}] ${message}`;
  $('log').textContent = ($('log').textContent + line + '\n').split('\n').slice(-160).join('\n');
  $('log').scrollTop = $('log').scrollHeight;
}
function result(message, type = '') { $('result').textContent = message; $('result').className = type; log(message); }
function renderControls() {
  $('connect').disabled = !supported || busy || !!client;
  $('disconnect').disabled = busy || !transport;
  $('uart').disabled = busy || !!transport; $('direct').disabled = busy || !!transport;
  $('refresh').disabled = busy || !client;
  $('camera-controls').disabled = busy || !client || !!current?.storage_error;
  $('settings-controls').disabled = busy || !client || current?.settings_raw === undefined || !!current?.storage_error;
  $('package').disabled = busy;
  $('flash').disabled = busy || !client || !pkg;
  $('cancel').disabled = !flashing || cancelRequested;
  $('connection-badge').textContent = busy ? '操作中' : client ? '已連線' : '尚未連線';
  $('connection-badge').classList.toggle('connected', !!client);
}
function showInfo(value, populate = false) {
  current = value;
  $('board').textContent = value.board.replace('rotorrec_', '').toUpperCase(); $('version').textContent = value.version;
  $('phase').textContent = phases[value.phase] ?? '未知狀態'; $('battery').textContent = value.battery_valid ? `${value.battery}%` : '未知';
  $('device-status').textContent = value.command_pending ? '等待相機命令完成' : value.saving ? '相機忙碌 / 存檔中' : !value.recording_valid ? '錄影狀態尚未確認' : value.recording ? '錄影中' : '相機待機';
  const pairing = [3, 5].includes(value.phase) && value.camera !== 3;
  $('pair-code').hidden = !pairing; $('pair-code').textContent = pairing ? `配對碼 ${String(value.pairing_code ?? 0).padStart(4, '0')} · 請在相機螢幕確認` : '';
  const messages = [];
  if (value.pending_verify) messages.push('新韌體正在完成首次開機確認。');
  if (value.storage_error) messages.push('設定儲存異常；相機與設定保存不可用，請先修復儲存或更新韌體。');
  if (value.saved_settings_raw !== undefined && value.saved_settings_raw !== value.settings_raw) messages.push('已保存的設定尚未重啟生效。');
  if (value.settings_raw === undefined) messages.push('目前韌體不支援基本設定，請先更新。');
  $('pending').textContent = messages.join(' ');
  if (populate) {
    $('camera').value = value.camera;
    if (value.settings_raw !== undefined) {
      const settings = decodeSettings(value.saved_settings_raw ?? value.settings_raw);
      $('user').value = settings.user_mode_id; $('osd').value = settings.osd_lines;
      $('led').value = settings.led_percent; $('low-battery').value = settings.low_battery_percent; $('link-pause').checked = settings.link_paused;
    }
  }
  renderControls();
}
function stopPoll() { clearTimeout(poll); poll = null; }
function startPoll() {
  stopPoll(); if (!client || busy) return;
  poll = setTimeout(() => run(async () => showInfo(await client.info())), 8000);
}
async function closeConnection(sendExit = true) {
  stopPoll();
  if (client && sendExit && !client.uncertain) { try { await client.request(CMD.EXIT, undefined, 300, 1); } catch {} }
  client = null; current = null;
  if (transport) { const closing = transport; transport = null; await closing.close(); }
  for (const id of ['board', 'version', 'phase', 'battery']) $(id).textContent = '—';
  $('device-status').textContent = '已斷線；請將飛控重新上電以恢復正常控制。'; $('pair-code').hidden = true; $('pending').textContent = '';
  renderControls();
}
async function run(action, disconnectOnError = true) {
  if (busy) return; busy = true; stopPoll(); renderControls();
  try { await action(); }
  catch (error) {
    result(error.message || String(error), 'error');
    // A local ZIP error cannot invalidate the current device session.
    // Confirmed ABORT / rejected commands still have a synchronized session.
    // closeConnection skips EXIT automatically when the outcome is uncertain.
    if (disconnectOnError) await closeConnection();
  } finally { busy = false; flashing = false; renderControls(); startPoll(); }
}
$('connect').addEventListener('click', () => run(async () => {
  const port = await navigator.serial.requestPort(); // Must remain in this user gesture.
  if (automaticDirect && lastPort && port !== lastPort) { $('direct').checked = false; automaticDirect = false; }
  transport = new SerialTransport(port); await transport.open();
  if (!$('direct').checked) await enterPassthrough(transport, $('uart').value, log);
  $('direct').checked = true; automaticDirect = true; lastPort = port;
  client = new Client(transport); showInfo(await client.info(true), true);
  result('已連接 RotorREC。管理期間遙控錄影與 OSD 暫停；完成後請將飛控重新上電。', 'success');
}));
$('uart').addEventListener('input', () => { $('direct').checked = false; automaticDirect = false; });
$('direct').addEventListener('change', () => { automaticDirect = false; });
$('disconnect').addEventListener('click', () => run(async () => { await closeConnection(); result('已斷線。請將飛控重新上電以結束 Passthrough。'); }));
$('refresh').addEventListener('click', () => run(async () => { showInfo(await client.info(), true); result('已讀取目前裝置狀態與設定。', 'success'); }));
$('save-camera').addEventListener('click', () => run(async () => {
  const camera = Number($('camera').value); await client.request(CMD.CAMERA, Uint8Array.of(camera));
  showInfo(await client.reboot({ camera }), true); result('相機選擇已保存，並已在重啟後讀回確認。', 'success');
}));
$('save-settings').addEventListener('click', () => run(async () => {
  if (!$('led').reportValidity() || !$('low-battery').reportValidity()) return;
  showInfo(await client.setSettings({ user_mode_id: Number($('user').value), osd_lines: Number($('osd').value),
    led_percent: Number($('led').value), low_battery_percent: Number($('low-battery').value), link_paused: $('link-pause').checked }), true);
  result('設定已保存，並已在重啟後讀回確認。', 'success');
}));
$('pair').addEventListener('click', () => run(async () => {
  await client.request(CMD.PAIR); result('配對中，請留意相機的配對確認畫面（最多等待 120 秒）。');
  const deadline = performance.now() + 120000;
  while (performance.now() < deadline) {
    await sleep(1000); const value = await client.info(); showInfo(value);
    if (value.phase === 7 && !value.command_pending) { result('相機配對與連線已確認。', 'success'); return; }
  }
  throw new Error('配對尚未確認。請查看相機確認畫面並重新讀取狀態。');
}));
$('package').addEventListener('change', () => run(async () => {
  pkg = null; $('package-info').textContent = '正在驗證更新包…';
  const file = $('package').files[0]; if (!file) { $('package-info').textContent = '尚未選擇更新包。'; return; }
  try {
    if (file.size > 0x410000) throw new Error('更新包超出大小限制');
    pkg = await loadPackage(new Uint8Array(await file.arrayBuffer()));
  }
  catch (error) { $('package-info').textContent = '更新包驗證失敗，請選擇正確的 RotorREC ZIP。'; throw error; }
  const m = pkg.metadata; $('package-info').textContent = `${m.board} · v${m.version} · ${(m.size / 1024).toFixed(1)} KiB · SHA-256 已驗證`;
  result('更新包已通過內容、描述與雜湊檢查。連線後仍會檢查目標板型及容量。', 'success');
}, false));
$('flash').addEventListener('click', () => run(async () => {
  flashing = true; cancelRequested = false; $('progress').value = 0; renderControls();
  const started = performance.now(); result('開始更新。請保持供電、分頁在前景，避免電腦睡眠。');
  const value = await client.update(pkg, progress => {
    $('progress').value = progress.sent * 100 / progress.total;
    if (progress.phase === 'verify') {
      flashing = false; renderControls(); $('progress-text').textContent = '傳送完成 · 校驗、重啟與指紋確認中…';
    } else {
      const rate = progress.sent / ((performance.now() - started) / 1000);
      $('progress-text').textContent = `${Math.floor(progress.sent * 100 / progress.total)}% · ${(progress.sent / 1024).toFixed(0)} / ${(progress.total / 1024).toFixed(0)} KiB · ${(rate / 1024).toFixed(1)} KiB/s`;
    }
  }, () => cancelRequested);
  showInfo(value, true); $('progress-text').textContent = '更新完成 · 版本與 ELF 指紋確認成功';
  result('韌體刷寫完成；已確認新韌體啟動、版本與 ELF 指紋。請將飛控重新上電。', 'success');
}));
$('cancel').addEventListener('click', () => { cancelRequested = true; renderControls(); $('progress-text').textContent = '等待目前封包完成後取消…'; });
$('clear-log').addEventListener('click', () => { $('log').textContent = ''; });
window.addEventListener('beforeunload', event => { if (busy || client) { event.preventDefault(); event.returnValue = ''; } });
if ('serial' in navigator) navigator.serial.addEventListener('disconnect', event => {
  if (transport && (event.target === transport.port || event.port === transport.port)) {
    result('USB 已中斷。操作結果可能尚未確認；重連後請讀取裝置狀態。', 'error');
    if (!busy) run(() => closeConnection(false));
  }
});
document.addEventListener('visibilitychange', () => { if (document.hidden && busy) log('分頁進入背景；瀏覽器可能延遲通訊，請返回此分頁。'); });
$('support').textContent = supported ? 'Web Serial 已就緒 · 使用飛控 USB 連線，所有處理都在本機完成。' : '無法使用：請以桌面 Chrome / Edge 從 http://localhost 或 HTTPS 開啟，並確認瀏覽器支援 Web Serial 與 DecompressionStream。';
renderControls();
