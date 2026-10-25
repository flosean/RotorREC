import { Client, CMD, decodeSettings, decodeControl, enterPassthrough, loadPackage, sleep } from './protocol.mjs';
import { SerialTransport } from './serial.mjs';
import { HttpTransport } from './http.mjs';

const $ = id => document.getElementById(id);
const phases = ['啟動中', '待機', '搜尋相機', '等待確認', '連接 Action 2', '請在相機確認配對', '等待相機狀態', '已連線', '重新連線中', '找不到相機', '不支援的服務', '配對遭拒', '狀態訂閱失敗', 'Link Pause'];
let transport = null, client = null, current = null, pkg = null, busy = false, flashing = false, cancelRequested = false, poll = null;
let lastPort = null, automaticDirect = false;
const wifiPage = location.hostname === '192.168.4.1' || new URLSearchParams(location.search).get('transport') === 'wifi';
const supported = (wifiPage || (window.isSecureContext && 'serial' in navigator)) && 'DecompressionStream' in window;
let wifiCredentials = null;

function log(message) {
  const line = `[${new Date().toLocaleTimeString('zh-TW')}] ${message}`;
  $('log').textContent = ($('log').textContent + line + '\n').split('\n').slice(-160).join('\n');
  $('log').scrollTop = $('log').scrollHeight;
}
function result(message, type = '') { $('result').textContent = message; $('result').className = type; log(message); }
function renderControls() {
  $('connect').disabled = !supported || busy || !!client;
  $('disconnect').disabled = busy || !transport;
  $('uart').disabled = wifiPage || busy || !!transport; $('direct').disabled = wifiPage || busy || !!transport;
  $('refresh').disabled = busy || !client;
  $('camera-controls').disabled = busy || !client || !!current?.storage_error;
  $('settings-controls').disabled = busy || !client || current?.settings_raw === undefined || !!current?.storage_error;
  $('record-controls').disabled = busy || !client || current?.control_raw === undefined || !!current?.storage_error;
  $('layout-controls').disabled = busy || !client || current?.osd_layout_version !== 1 || !!current?.storage_error;
  $('export-diagnostics').disabled = busy || !current;
  $('wifi-controls').disabled = busy || !client || !current?.wifi_capable;
  $('wifi-enter').disabled = busy || !wifiCredentials || !!current?.wifi_active;
  $('wifi-exit').hidden = !current?.wifi_active;
  $('reset-controls').disabled = busy || !client || current?.reset_capable !== 1;
  $('reset-device').disabled = busy || !client || !$('reset-confirm').checked;
  $('pair').disabled = busy || !client || !!current?.wifi_active || !!current?.reset_pending;
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
  const recordResults = ['', '等待相機確認開始錄影', '等待相機確認停止錄影', '開始錄影失敗，請檢查相機', '相機尚未停止錄影，請勿切斷電源', '錄影狀態未知，請檢查相機', '錄影已意外停止，請檢查相機'];
  if (recordResults[value.record_result]) $('device-status').textContent = recordResults[value.record_result];
  else if (value.stop_delay_remaining) $('device-status').textContent = `預計 ${value.stop_delay_remaining} 秒後要求停錄，請等待相機確認`;
  const pairing = [3, 5].includes(value.phase) && value.camera !== 3;
  $('pair-code').hidden = !pairing; $('pair-code').textContent = pairing ? `配對碼 ${String(value.pairing_code ?? 0).padStart(4, '0')} · 請在相機螢幕確認` : '';
  const messages = [];
  if (value.pending_verify) messages.push('新韌體正在完成首次開機確認。');
  if (value.storage_error) messages.push('設定儲存異常；相機與設定保存不可用，請先修復儲存或更新韌體。');
  if (value.reset_pending) messages.push('重設工作尚未完成。請重啟；若持續失敗，可選恢復出廠重設並檢查儲存狀態。');
  if (value.stop_storage_error) messages.push('待停錄要求保存失敗；模組重啟後可能無法恢復停錄，請直接確認相機。');
  if (value.saved_settings_raw !== undefined && value.saved_settings_raw !== value.settings_raw) messages.push('已保存的設定尚未重啟生效。');
  if (value.settings_raw === undefined) messages.push('目前韌體不支援基本設定，請先更新。');
  if (value.control_raw === undefined) messages.push('更新韌體後可設定 ARM、按鍵模式與停錄延遲。');
  else if (value.saved_control_raw !== value.control_raw) messages.push('錄影設定已保存，等待重啟生效。');
  $('pending').textContent = messages.join(' ');
  const telemetry = value.telemetry;
  $('preflight-status').textContent = value.wifi_active ? 'Wi-Fi 地面維護中：相機與飛控控制已停用。完成後按「退出 Wi-Fi，恢復運行」，再實際確認錄影與 OSD。' :
    `模組已讀取；相機${value.phase === 7 ? '已連接' : '尚未就緒'}，錄影狀態${value.recording_valid ? '已確認' : '未知'}。目前為維護模式，無法在此判定飛控遙控與 OSD 已恢復；完成後須實際檢查。`;
  const duration = seconds => seconds == null ? '未知' : `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
  $('camera-telemetry').textContent = telemetry ?
    `錄影 ${telemetry.record_time_estimated ? '約 ' : ''}${duration(telemetry.record_time)} · 剩餘 ${duration(telemetry.remain_time)} · SD ${telemetry.remain_capacity == null ? '未知' : telemetry.remain_capacity + ' MB'} · ${telemetry.parameters || '格式未知'}` :
    '目前韌體未提供詳細遙測。相機未支援或資料過期的欄位會顯示未知。';
  const warnings = [[1, '沒有 SD 卡'], [2, 'SD 卡已滿'], [4, 'SD 卡格式錯誤'], [8, 'SD 卡寫入過慢'], [16, '請切換影片模式'], [32, '相機過熱'], [64, '相機過冷'], [128, '剩餘錄影時間偏低'], [256, '相機電量偏低']];
  $('camera-warnings').textContent = warnings.filter(([bit]) => telemetry?.warnings & bit).map(([, text]) => text).join('；');
  if (populate) {
    (value.saved_osd_layout ?? ['', '', '', '']).forEach((row, i) => { $(`layout-${i}`).value = row; });
    $('camera').value = value.camera;
    if (value.settings_raw !== undefined) {
      const settings = decodeSettings(value.saved_settings_raw ?? value.settings_raw);
      $('user').value = settings.user_mode_id; $('osd').value = settings.osd_lines;
      $('led').value = settings.led_percent; $('low-battery').value = settings.low_battery_percent; $('link-pause').checked = settings.link_paused;
    }
    if (value.control_raw !== undefined) {
      const control = decodeControl(value.saved_control_raw ?? value.control_raw);
      $('record-mode').value = control.record_mode; $('stop-delay').value = control.stop_delay_seconds;
      $('auto-record').checked = control.auto_record; $('auto-stop').checked = control.auto_stop;
      $('low-remaining').value = control.low_remaining_seconds; $('heads-up').checked = control.heads_up_warnings;
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
  client = null; current = null; wifiCredentials = null;
  $('reset-confirm').checked = false;
  if (transport) { const closing = transport; transport = null; await closing.close(); }
  for (const id of ['board', 'version', 'phase', 'battery']) $(id).textContent = '—';
  $('device-status').textContent = wifiPage ? '網頁已斷線；模組仍可能在 Wi-Fi 維護模式。重新連線後退出維護，或將模組重新上電。' : '已斷線；請將飛控重新上電以恢復正常控制。'; $('pair-code').hidden = true; $('pending').textContent = '';
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
  wifiCredentials = null; $('wifi-credentials').textContent = '';
  if (wifiPage) { transport = new HttpTransport(); await transport.open(); }
  else {
    const port = await navigator.serial.requestPort(); // Must remain in this user gesture.
    if (automaticDirect && lastPort && port !== lastPort) { $('direct').checked = false; automaticDirect = false; }
    transport = new SerialTransport(port); await transport.open();
    if (!$('direct').checked) await enterPassthrough(transport, $('uart').value, log);
    $('direct').checked = true; automaticDirect = true; lastPort = port;
  }
  client = new Client(transport); showInfo(await client.info(true), true);
  result(wifiPage ? '已連接模組的本機 Wi-Fi 網頁。相機與飛控控制已停用；完成後請退出 Wi-Fi。' : '已連接 RotorREC。管理期間遙控錄影與 OSD 暫停；完成後請將飛控重新上電。', 'success');
}));
$('uart').addEventListener('input', () => { $('direct').checked = false; automaticDirect = false; });
$('direct').addEventListener('change', () => { automaticDirect = false; });
$('disconnect').addEventListener('click', () => run(async () => { await closeConnection(); result(wifiPage ? '已中斷網頁連線；模組仍在 Wi-Fi 模式，可長按 BOOT 放開退出。' : '已斷線。請將飛控重新上電以結束 Passthrough。'); }));
$('refresh').addEventListener('click', () => run(async () => { showInfo(await client.info(), true); result('已讀取目前裝置狀態與設定。', 'success'); }));
$('save-camera').addEventListener('click', () => run(async () => {
  const camera = Number($('camera').value); await client.request(CMD.CAMERA, Uint8Array.of(camera));
  showInfo(await client.reboot({ camera }), true); result('相機選擇已保存，並已在重啟後讀回確認。', 'success');
}));
$('save-settings').addEventListener('click', () => run(async () => {
  if (!$('led').reportValidity() || !$('low-battery').reportValidity()) return;
  const changes = { user_mode_id: Number($('user').value), osd_lines: Number($('osd').value),
    led_percent: Number($('led').value), low_battery_percent: Number($('low-battery').value), link_paused: $('link-pause').checked };
  if (current.control_raw !== undefined) {
    if (!$('stop-delay').reportValidity() || !$('low-remaining').reportValidity()) return;
    Object.assign(changes, { record_mode: Number($('record-mode').value), stop_delay_seconds: Number($('stop-delay').value),
      auto_record: $('auto-record').checked, auto_stop: $('auto-stop').checked,
      low_remaining_seconds: Number($('low-remaining').value), heads_up_warnings: $('heads-up').checked });
  }
  showInfo(await client.setSettings(changes), true);
  result('設定已保存，並已在重啟後讀回確認。', 'success');
}));
$('pair').addEventListener('click', () => run(async () => {
  if (current?.wifi_active) throw new Error('Wi-Fi 維護模式未啟動 BLE；請保存相機選擇後退出 Wi-Fi，再長按 BOOT 配對。');
  await client.request(CMD.PAIR); result('配對中，請留意相機的配對確認畫面（最多等待 120 秒）。');
  const deadline = performance.now() + 120000;
  while (performance.now() < deadline) {
    await sleep(1000); const value = await client.info(); showInfo(value);
    if (value.phase === 7 && !value.command_pending) { result('相機配對與連線已確認。', 'success'); return; }
  }
  throw new Error('配對尚未確認。請查看相機確認畫面並重新讀取狀態。');
}));
$('save-layout').addEventListener('click', () => run(async () => {
  showInfo(await client.setLayout(Array.from({length: 4}, (_, i) => $(`layout-${i}`).value)), true);
  result('OSD 版面已保存，並在重啟後讀回確認。', 'success');
}));
$('reset-layout').addEventListener('click', () => { for (let i = 0; i < 4; i++) $(`layout-${i}`).value = ''; });
$('reset-confirm').addEventListener('change', renderControls);
$('reset-scope').addEventListener('change', () => { $('reset-confirm').checked = false; renderControls(); });
$('reset-device').addEventListener('click', () => run(async () => {
  if (!$('reset-confirm').checked) throw new Error('請先確認重設範圍');
  const scope = Number($('reset-scope').value);
  $('reset-confirm').checked = false;
  const value = await client.resetDevice(scope);
  if (value) {
    showInfo(value, true);
    result(scope === 1 ? '清除配對與重啟已確認。設定已保留，Link Pause 已開啟；需要相機時請恢復連線並重新配對。' : '恢復出廠與重啟已確認，請重新設定相機及錄影控制。', 'success');
  } else {
    await closeConnection(false);
    result('出廠重設要求已保存，已送出重啟要求。Wi-Fi 連線無法確認完成；請以 USB 重新連線讀取狀態。');
  }
}));
$('wifi-info').addEventListener('click', () => run(async () => {
  wifiCredentials = JSON.parse(new TextDecoder().decode(await client.request(CMD.WIFI_INFO)));
  $('wifi-credentials').textContent = `熱點：${wifiCredentials.ssid}\n密碼：${wifiCredentials.password}\n網址：${wifiCredentials.url}\n請先保存以上資訊，再進入 Wi-Fi。`;
}));
$('wifi-enter').addEventListener('click', () => run(async () => {
  if (!wifiCredentials) throw new Error('請先讀取並保存 Wi-Fi 連線資訊');
  try { await client.request(CMD.WIFI_ENTER, undefined, 1000, 1); }
  finally { await closeConnection(false); }
  result('已送出進入 Wi-Fi 的要求。請在手機連接上方熱點並開啟網址；若熱點未出現，重新讀取模組狀態。FC 仍在透傳，最後需將飛控重新上電。');
}));
$('wifi-exit').addEventListener('click', () => run(async () => {
  await client.request(CMD.RUN); await closeConnection(false);
  result('退出 Wi-Fi 要求已接受，模組即將重新啟動。熱點消失後確認相機與 OSD 已恢復；若曾由 FC USB 透傳進入，仍須將飛控重新上電。');
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
  result(wifiPage ? '韌體更新與重啟指紋已確認。完成設定後請退出 Wi-Fi。' : '韌體刷寫完成；已確認新韌體啟動、版本與 ELF 指紋。請將飛控重新上電。', 'success');
}));
$('cancel').addEventListener('click', () => { cancelRequested = true; renderControls(); $('progress-text').textContent = '等待目前封包完成後取消…'; });
$('clear-log').addEventListener('click', () => { $('log').textContent = ''; });
$('export-diagnostics').addEventListener('click', () => {
  const file = new Blob([JSON.stringify({ captured_at: new Date().toISOString(), device: current,
    log: $('log').textContent, hardware_verified: false }, null, 2)], {type: 'application/json'});
  const link = document.createElement('a'); link.href = URL.createObjectURL(file); link.download = 'rotorrec-diagnostics.json';
  link.click(); setTimeout(() => URL.revokeObjectURL(link.href), 1000);
});
window.addEventListener('beforeunload', event => { if (busy || client) { event.preventDefault(); event.returnValue = ''; } });
if ('serial' in navigator) navigator.serial.addEventListener('disconnect', event => {
  if (transport && (event.target === transport.port || event.port === transport.port)) {
    result('USB 已中斷。操作結果可能尚未確認；重連後請讀取裝置狀態。', 'error');
    if (!busy) run(() => closeConnection(false));
  }
});
document.addEventListener('visibilitychange', () => { if (document.hidden && busy) log('分頁進入背景；瀏覽器可能延遲通訊，請返回此分頁。'); });
$('support').textContent = wifiPage ? (supported ? '模組本機 Wi-Fi 網頁 · 不需要網際網路。更新時保持頁面在前景。' : '此瀏覽器缺少 ZIP 解壓縮能力；請使用支援 DecompressionStream 的新版瀏覽器。') : supported ? 'Web Serial 已就緒 · 使用飛控 USB 連線，所有處理都在本機完成。' : '無法使用：請以桌面 Chrome / Edge 從 http://localhost 或 HTTPS 開啟，並確認瀏覽器支援 Web Serial 與 DecompressionStream。';
if (wifiPage) {
  $('connect').textContent = '連接 Wi-Fi 模組';
  $('connection-title').textContent = '連接 RotorREC';
  $('connection-description').textContent = '透過模組 Wi-Fi 調整設定與更新韌體。';
  $('transport-label').textContent = '本機 Wi-Fi';
  $('uart').closest('label').hidden = true; $('direct').closest('label').hidden = true;
  $('connection-hint').textContent = '請保持連接這台模組的 Wi-Fi；此頁不使用手機 USB 或網際網路。';
  document.querySelector('.sidebar-foot').textContent = 'LOCAL / WI-FI · ESP32-C3 · ESP32-C6';
  $('maintenance-notice').textContent = 'Wi-Fi 地面維護：相機與飛控控制停止。完成後使用「退出 Wi-Fi，恢復運行」。如果從 FC USB 透傳進入，最後仍須將飛控重新上電；以 BOOT 進入可直接退出回到正常模式。';
  document.querySelector('a[href="bf-setup.html"]').hidden = true;
  $('help-link').href = '#first-use';
}
renderControls();
