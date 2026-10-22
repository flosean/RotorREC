const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });
export const CMD = Object.freeze({ HELLO: 1, INFO: 2, CAMERA: 3, PAIR: 4, BEGIN: 5, DATA: 6, END: 7, ABORT: 8, REBOOT: 9, EXIT: 10, SETTINGS: 11 });
export const BOARDS = Object.freeze({ rotorrec_esp32c3: 5, rotorrec_esp32c6: 13 });
export const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const view = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
export const u32 = number => { const bytes = new Uint8Array(4); view(bytes).setUint32(0, number, true); return bytes; };
const join = (a, b) => { const bytes = new Uint8Array(a.length + b.length); bytes.set(a); bytes.set(b, a.length); return bytes; };
export function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) { crc ^= byte; for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0); }
  return (crc ^ 0xffffffff) >>> 0;
}
export function encode(command, session, sequence, payload = new Uint8Array()) {
  if (payload.length > 1040) throw new Error('封包過大');
  const bytes = new Uint8Array(20 + payload.length), data = view(bytes);
  bytes.set(encoder.encode('RREC')); bytes[4] = 1; bytes[5] = command;
  data.setUint32(6, session, true); data.setUint32(10, sequence, true); data.setUint16(14, payload.length, true);
  bytes.set(payload, 16); data.setUint32(bytes.length - 4, crc32(bytes.subarray(0, -4)), true);
  return bytes;
}
export class Parser {
  buffer = new Uint8Array();
  feed(bytes) {
    this.buffer = join(this.buffer, bytes);
    const packets = [];
    while (this.buffer.length) {
      const b = this.buffer;
      if ([82, 82, 69, 67].slice(0, Math.min(4, b.length)).some((v, i) => b[i] !== v)) { this.buffer = b.subarray(1); continue; }
      if (b.length < 16) break;
      const data = view(b), length = data.getUint16(14, true), size = length + 20;
      if (b[4] !== 1 || length > 1040) { this.buffer = b.subarray(1); continue; }
      if (b.length < size) break;
      if (crc32(b.subarray(0, size - 4)) !== data.getUint32(size - 4, true)) { this.buffer = b.subarray(1); continue; }
      packets.push({ command: b[5], session: data.getUint32(6, true), sequence: data.getUint32(10, true), payload: b.slice(16, size - 4) });
      this.buffer = b.slice(size);
    }
    return packets;
  }
}
export function encodeSettings(s) {
  if (!['user_mode_id', 'osd_lines', 'led_percent', 'low_battery_percent'].every(k => Number.isInteger(s[k])) ||
      typeof s.link_paused !== 'boolean' || typeof s.owns_extra_lines !== 'boolean' ||
      s.user_mode_id < 40 || s.user_mode_id > 43 || ![2, 4].includes(s.osd_lines) ||
      s.led_percent < 1 || s.led_percent > 100 || s.low_battery_percent < 0 || s.low_battery_percent > 100 ||
      (s.osd_lines === 4 && !s.owns_extra_lines)) throw new Error('設定值無效');
  return (0x01000000 | (s.user_mode_id - 40) | (s.osd_lines === 4 ? 4 : 0) | (s.link_paused ? 8 : 0) |
    (s.led_percent << 4) | (s.low_battery_percent << 11) | (s.owns_extra_lines ? 1 << 18 : 0)) >>> 0;
}
export function decodeSettings(raw) {
  if (!Number.isInteger(raw) || raw < 0 || raw > 0xffffffff || (raw & 0xfff80000) !== 0x01000000) throw new Error('不支援的設定格式');
  const s = { user_mode_id: 40 + (raw & 3), osd_lines: raw & 4 ? 4 : 2, link_paused: !!(raw & 8),
    led_percent: (raw >>> 4) & 127, low_battery_percent: (raw >>> 11) & 127, owns_extra_lines: !!(raw & (1 << 18)) };
  encodeSettings(s); return s;
}
const hex = bytes => [...bytes].map(v => v.toString(16).padStart(2, '0')).join('');
export async function imageMetadata(image) {
  if (image.length < 288 || image.length > 0x3f0000 || image[0] !== 0xe9 || view(image).getUint32(32, true) !== 0xabcd5432) throw new Error('需要 RotorREC app 映像，不能使用整合 factory 映像');
  const field = offset => { const bytes = image.subarray(offset, offset + 32), end = bytes.indexOf(0); if (end < 0 || bytes.subarray(0, end).some(b => b > 127)) throw new Error('映像描述無效'); return decoder.decode(bytes.subarray(0, end)); };
  const board = field(80), version = field(48);
  if (!Object.hasOwn(BOARDS, board) || !/^[A-Za-z0-9_.+\-]{1,31}$/.test(version) || view(image).getUint16(12, true) !== BOARDS[board]) throw new Error('不支援的板型或晶片');
  return { product: 'RotorREC', board, version, protocol: 1, layout: 1, size: image.length,
    sha256: hex(new Uint8Array(await crypto.subtle.digest('SHA-256', image))), elf_sha256: hex(image.subarray(176, 208)) };
}
async function unzipEntry(compressed, method, length) {
  if (method === 0) { if (compressed.length !== length) throw new Error('ZIP 長度錯誤'); return compressed; }
  if (method !== 8) throw new Error('ZIP 壓縮方式不支援');
  const reader = new Blob([compressed]).stream().pipeThrough(new DecompressionStream('deflate-raw')).getReader();
  const result = new Uint8Array(length); let offset = 0;
  try {
    while (true) { const { value, done } = await reader.read(); if (done) break;
      if (offset + value.length > length) throw new Error('ZIP 展開超出宣告長度'); result.set(value, offset); offset += value.length; }
  } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
  if (offset !== length) throw new Error('ZIP 內容不完整'); return result;
}
export async function loadPackage(bytes) {
  if (bytes.length < 22 || bytes.length > 0x410000) throw new Error('更新包大小無效');
  const data = view(bytes); let end = bytes.length - 22;
  while (end >= Math.max(0, bytes.length - 65557) && !(data.getUint32(end, true) === 0x06054b50 && end + 22 + data.getUint16(end + 20, true) === bytes.length)) end--;
  if (end < Math.max(0, bytes.length - 65557) || data.getUint16(end + 4, true) || data.getUint16(end + 6, true) ||
      data.getUint16(end + 8, true) !== 2 || data.getUint16(end + 10, true) !== 2) throw new Error('更新包必須只含 manifest.json 與 app.bin');
  const start = data.getUint32(end + 16, true), size = data.getUint32(end + 12, true);
  if (start + size !== end) throw new Error('ZIP 目錄無效');
  const files = {}; let cursor = start;
  for (let i = 0; i < 2; i++) {
    if (cursor + 46 > end || data.getUint32(cursor, true) !== 0x02014b50) throw new Error('ZIP 目錄損壞');
    const flags = data.getUint16(cursor + 8, true), method = data.getUint16(cursor + 10, true), crc = data.getUint32(cursor + 16, true);
    const compressedSize = data.getUint32(cursor + 20, true), length = data.getUint32(cursor + 24, true), nameLength = data.getUint16(cursor + 28, true);
    const next = cursor + 46 + nameLength + data.getUint16(cursor + 30, true) + data.getUint16(cursor + 32, true);
    if (next > end || data.getUint16(cursor + 34, true) || (flags & ~0x808)) throw new Error('不支援的 ZIP 格式');
    const name = decoder.decode(bytes.subarray(cursor + 46, cursor + 46 + nameLength)), local = data.getUint32(cursor + 42, true);
    if (!['app.bin', 'manifest.json'].includes(name) || Object.hasOwn(files, name) || length > (name === 'app.bin' ? 0x3f0000 : 4096)) throw new Error('ZIP 檔案或大小無效');
    if (local + 30 > start || data.getUint32(local, true) !== 0x04034b50 || data.getUint16(local + 6, true) !== flags || data.getUint16(local + 8, true) !== method) throw new Error('ZIP 本機標頭無效');
    const localNameLength = data.getUint16(local + 26, true), payloadStart = local + 30 + localNameLength + data.getUint16(local + 28, true);
    if (payloadStart + compressedSize > start || decoder.decode(bytes.subarray(local + 30, local + 30 + localNameLength)) !== name) throw new Error('ZIP 檔名或資料範圍錯誤');
    const content = await unzipEntry(bytes.subarray(payloadStart, payloadStart + compressedSize), method, length);
    if (crc32(content) !== crc) throw new Error('ZIP CRC 驗證失敗'); files[name] = content; cursor = next;
  }
  if (cursor !== end) throw new Error('ZIP 目錄長度錯誤');
  const metadata = JSON.parse(decoder.decode(files['manifest.json'])), actual = await imageMetadata(files['app.bin']);
  if (!metadata || Array.isArray(metadata) || Object.keys(metadata).length !== Object.keys(actual).length || Object.entries(actual).some(([k, v]) => metadata[k] !== v)) throw new Error('Manifest 與韌體內容不一致');
  return { metadata: actual, image: files['app.bin'] };
}
export class TimeoutError extends Error {}
export class DeviceError extends Error {}
export class Client {
  constructor(port) { this.port = port; this.reset(); }
  reset() { this.session = crypto.getRandomValues(new Uint32Array(1))[0] || 1; this.sequence = 0; this.parser = new Parser(); this.uncertain = false; }
  async request(command, payload = new Uint8Array(), timeout = 5000, retries = 3) {
    if (this.uncertain) throw new Error('上一個命令結果不明，請斷線並等待 30 秒後重新連線');
    const wire = encode(command, this.session, this.sequence, payload);
    for (let attempt = 0; attempt < retries; attempt++) {
      await this.port.write(wire); const deadline = performance.now() + timeout;
      while (performance.now() < deadline) {
        for (const packet of this.parser.feed(await this.port.read(Math.max(1, deadline - performance.now())))) {
          if (packet.command !== (command | 0x80) || packet.session !== this.session || packet.sequence !== this.sequence) continue;
          if (packet.payload.length < 4) throw new Error('裝置回應不完整'); this.sequence++;
          const error = view(packet.payload).getUint32(0, true);
          if (error) throw new DeviceError(`裝置拒絕命令 ${command}：0x${error.toString(16)}。請確認相機待機與裝置狀態。`);
          return packet.payload.slice(4);
        }
      }
      this.parser = new Parser();
    }
    this.uncertain = true;
    throw new TimeoutError('裝置沒有回應；請檢查供電、UART。上一個操作結果不明，等待 30 秒後重新讀取狀態。');
  }
  async info(hello = false, timeout = 5000, retries = 3) {
    const value = JSON.parse(decoder.decode(await this.request(hello ? CMD.HELLO : CMD.INFO, undefined, timeout, retries)));
    if (value.product !== 'RotorREC' || value.protocol !== 1 || !Object.hasOwn(BOARDS, value.board)) throw new Error('不支援的裝置或協定');
    return value;
  }
  async reboot(expected = {}) {
    try { await this.request(CMD.REBOOT, undefined, 1000, 1); } catch (error) { if (!(error instanceof TimeoutError)) throw error; }
    return this.waitForBoot(expected);
  }
  async waitForBoot(expected = {}) {
    this.reset(); const deadline = performance.now() + 40000;
    while (performance.now() < deadline) {
      try {
        const value = await this.info(this.sequence === 0, Math.min(5000, Math.max(1, deadline - performance.now())), 1);
        if (value.pending_verify) { await sleep(500); continue; }
        for (const [key, expectedValue] of Object.entries(expected)) if (value[key] !== expectedValue) throw new Error(`重啟驗證失敗：${key} 不符，裝置可能已回滾。`);
        return value;
      } catch (error) {
        if (!(error instanceof TimeoutError)) throw error;
        // An acknowledged pending image can crash and reboot before the next INFO.
        // Re-handshake in that case; otherwise retry the same unacknowledged HELLO.
        if (this.sequence > 0) this.reset(); else this.uncertain = false;
        await sleep(200);
      }
    }
    throw new TimeoutError('無法確認重啟結果。請重新連線檢查版本、指紋與設定。');
  }
  async setSettings(changes) {
    const current = await this.info();
    const value = { ...decodeSettings(current.saved_settings_raw ?? current.settings_raw), ...changes };
    value.owns_extra_lines ||= value.osd_lines === 4;
    const raw = encodeSettings(value); await this.request(CMD.SETTINGS, u32(raw)); return this.reboot({ settings_raw: raw });
  }
  async update(pkg, progress = () => {}, cancelled = () => false) {
    const { metadata, image } = pkg, current = await this.info();
    if (metadata.board !== current.board || metadata.layout !== current.layout || metadata.protocol !== current.protocol) throw new Error('更新包板型或分區配置不符');
    if (!Number.isInteger(current.capacity) || image.length > current.capacity) throw new Error('韌體超過更新分區容量');
    const payload = new Uint8Array(104); payload.set(u32(image.length)); payload.set(u32(metadata.layout), 4);
    payload.set(encoder.encode(metadata.board), 8); payload.set(encoder.encode(metadata.version), 40);
    payload.set(Uint8Array.from(metadata.sha256.match(/../g), b => parseInt(b, 16)), 72);
    let endStarted = false;
    try {
      if (cancelled()) throw new Error('已取消刷寫'); await this.request(CMD.BEGIN, payload);
      for (let offset = 0; offset < image.length; offset += 1024) {
        if (cancelled()) throw new Error('已取消刷寫；原韌體保持選定');
        const chunk = image.subarray(offset, offset + 1024); await this.request(CMD.DATA, join(u32(offset), chunk));
        progress({ phase: 'transfer', sent: offset + chunk.length, total: image.length });
      }
      if (cancelled()) throw new Error('已取消刷寫；原韌體保持選定');
      endStarted = true; progress({ phase: 'verify', sent: image.length, total: image.length });
      try { await this.request(CMD.END); }
      catch (error) {
        if (!(error instanceof TimeoutError)) throw error;
        // The committed receiver reboots after its 30-second session timeout. A fresh
        // session cannot extend that timeout or accidentally abort the committed image.
        return await this.waitForBoot({ version: metadata.version, elf_sha256: metadata.elf_sha256 });
      }
      return await this.reboot({ version: metadata.version, elf_sha256: metadata.elf_sha256 });
    } catch (error) {
      if (!endStarted && !this.uncertain) { try { await this.request(CMD.ABORT, undefined, 1000, 1); } catch {} }
      if (endStarted) throw new Error(`更新提交後尚未確認啟動結果：${error.message} 請等待至少 30 秒再重新讀取裝置資訊，勿以傳送完成判定刷寫成功。`);
      throw error;
    }
  }
}
export function selectUart(text, requested = '') {
  const candidates = [...text.matchAll(/^serial (\d+) (\d+) (\d+) /gm)]
    .map(m => ({ uart: Number(m[1]) + 1, functions: Number(m[2]), baud: Number(m[3]) }))
    .filter(p => p.uart >= 1 && p.uart <= 20 && (p.functions & 1));
  if (requested === '' && candidates.length !== 1) throw new Error(`請指定已接 RotorREC 的 UART。MSP 候選：${candidates.map(p => `UART${p.uart} / ${p.baud}`).join('、') || '無'}`);
  const uart = requested === '' ? candidates[0].uart : Number(requested);
  if (!Number.isInteger(uart) || !candidates.some(p => p.uart === uart && p.baud === 115200)) throw new Error('指定 UART 必須已在 Betaflight 啟用 MSP、115200 baud');
  return uart;
}
async function cliResponse(port, command) {
  await port.write(encoder.encode(`${command}\r\n`)); let text = ''; const deadline = performance.now() + 3000;
  while (performance.now() < deadline) {
    const bytes = await port.read(100); text += new TextDecoder().decode(bytes);
    if (text.length > 65536) throw new Error('CLI 回應過長');
    if (!bytes.length && /(?:^|\r?\n)# $/.test(text)) return text;
  }
  throw new TimeoutError('Betaflight CLI 沒有回應。請關閉 Configurator；已進入透傳時勾選「已在 Passthrough」。');
}
export async function enterPassthrough(port, requested = '', log = () => {}) {
  await port.write(encoder.encode('#')); await sleep(300); port.clear();
  const version = await cliResponse(port, 'version'); if (!version.includes('Betaflight')) throw new Error('這個序列埠不是 Betaflight');
  const uart = selectUart(await cliResponse(port, 'serial'), requested); log(`已確認 Betaflight，開啟 UART${uart} 透傳。`);
  await port.write(encoder.encode(`serialpassthrough ${uart - 1} 115200 rxtx\r\n`));
  let text = ''; const deadline = performance.now() + 3000;
  while (performance.now() < deadline) { text += new TextDecoder().decode(await port.read(100)); if (text.includes('Forwarding')) return uart; if (text.length > 65536) break; }
  throw new TimeoutError('Betaflight 未確認透傳。若已進入透傳，勾選「已在 Passthrough」後重連。');
}
