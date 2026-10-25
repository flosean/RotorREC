/* Betaflight MSP API 1.47/1.48: explicit, user-triggered OSD installation only.
 * No RC, ARM, motor, receiver, port or general OSD configuration writes. */
export const MSP = Object.freeze({API: 1, VARIANT: 2, MODES: 34, OSD: 84, SET_OSD: 85,
  STATUS: 101, BOXIDS: 119, CANVAS: 189, SAVE: 250});
const allowed = new Set(Object.values(MSP));
const decoder = new TextDecoder();
function crc8(bytes) {
  let crc = 0;
  for (const b of bytes) { crc ^= b; for (let i = 0; i < 8; i++) crc = ((crc << 1) ^ ((crc & 128) ? 0xd5 : 0)) & 255; }
  return crc;
}
export function mspFrame(command, payload = new Uint8Array(), direction = '<') {
  if (!allowed.has(command) || payload.length > 4096) throw new Error('不允許的飛控命令');
  const frame = new Uint8Array(9 + payload.length), view = new DataView(frame.buffer);
  frame.set([36, 88, direction.charCodeAt(0), 0]); view.setUint16(4, command, true); view.setUint16(6, payload.length, true);
  frame.set(payload, 8); frame[frame.length - 1] = crc8(frame.subarray(3, -1)); return frame;
}
export class MspParser {
  buffer = new Uint8Array();
  feed(bytes) {
    const next = new Uint8Array(this.buffer.length + bytes.length); next.set(this.buffer); next.set(bytes, this.buffer.length);
    this.buffer = next; const result = [];
    while (this.buffer.length >= 3) {
      const b = this.buffer;
      if (b[0] !== 36 || b[1] !== 88 || ![62, 33].includes(b[2])) { this.buffer = b.slice(1); continue; }
      if (b.length < 9) break;
      const length = b[6] | (b[7] << 8);
      if (length > 4096 || b[3] !== 0) { this.buffer = b.slice(1); continue; }
      if (b.length < length + 9) break;
      if (crc8(b.subarray(3, length + 8)) !== b[length + 8]) { this.buffer = b.slice(1); continue; }
      result.push({command: b[4] | (b[5] << 8), error: b[2] === 33, payload: b.slice(8, length + 8)});
      this.buffer = b.slice(length + 9);
    }
    return result;
  }
}
export function parseOsd(payload) {
  const p = payload, v = new DataView(p.buffer, p.byteOffset, p.byteLength);
  if (p.length < 10 || !(p[0] & 1) || p[7] < 85 || p.length < 10 + p[7] * 2 + 2) throw new Error('此飛控未提供四個 Custom Message 元件');
  const allPositions = Array.from({length: p[7]}, (_, i) => v.getUint16(10 + i * 2, true));
  let offset = 10 + p[7] * 2;
  offset += 1 + p[offset];
  if (offset >= p.length) throw new Error('OSD 統計欄位不完整');
  offset += 1 + p[offset] * 2 + 7; // timers, legacy warnings, warning count, 32-bit warnings
  if (offset + 2 > p.length) throw new Error('OSD 設定檔資訊不完整');
  const profiles = p[offset], profile = p[offset + 1];
  if (profiles < 1 || profiles > 3 || profile < 1 || profile > profiles) throw new Error('OSD 設定檔編號無效');
  return {positions: allPositions.slice(81, 85), allPositions, profile, profiles, flags: p[0]};
}
export function unpackPosition(raw, profile) {
  return {x: (raw & 31) | ((raw & 1024) >> 5), y: (raw >> 5) & 31, enabled: !!(raw & (1 << (10 + profile)))};
}
export function packPosition(previous, row, profile, canvas) {
  if (!Number.isInteger(row.x) || !Number.isInteger(row.y) || row.x < 0 || row.x + 16 > canvas.cols ||
      row.y < 0 || row.y >= canvas.rows || typeof row.enabled !== 'boolean') throw new Error('每行需保留 16 格且位於飛控畫布內');
  const flag = 1 << (10 + profile);
  return (previous & ~(2047 | flag)) | (row.x & 31) | ((row.x & 32) << 5) | (row.y << 5) | (row.enabled ? flag : 0);
}
export class FlightSetup {
  constructor(transport) { this.transport = transport; this.parser = new MspParser(); this.uncertain = false; }
  async request(command, payload) {
    if (this.uncertain) throw new Error('上一個飛控操作結果未知；請將飛控重新上電後重讀');
    const wire = mspFrame(command, payload);
    try { await this.transport.write(wire); }
    catch (error) { this.uncertain = true; throw error; }
    const deadline = performance.now() + 2000;
    while (performance.now() < deadline) {
      let bytes;
      try { bytes = await this.transport.read(100); }
      catch (error) { this.uncertain = true; throw error; }
      for (const reply of this.parser.feed(bytes)) {
      if (reply.command !== command) continue;
      if (reply.error) throw new Error(`飛控拒絕 MSP ${command}`);
      return reply.payload;
      }
    }
    this.uncertain = true;
    throw new Error('飛控未回應；操作結果未知，請重新上電後重讀');
  }
  async identity() {
    const api = await this.request(MSP.API), variant = decoder.decode(await this.request(MSP.VARIANT));
    if (api.length !== 3 || api[0] !== 0 || api[1] !== 1 || ![47, 48].includes(api[2]) || variant !== 'BTFL')
      throw new Error('OSD 設定工具僅支援已核對的 Betaflight MSP API 1.47／1.48');
    return {variant, api: `1.${api[2]}`};
  }
  async groundState() {
    const ids = [];
    for (let page = 0; page <= 4; page++) {
      const part = await this.request(MSP.BOXIDS, Uint8Array.of(page));
      if (part.length > 32 || ids.length + part.length > 128) throw new Error('飛控模式對照資料無效');
      ids.push(...part); if (part.length < 32) break;
    }
    if (new Set(ids).size !== ids.length || !ids.includes(0) || !ids.includes(27)) throw new Error('無法確認 ARM／FAILSAFE 對照');
    const p = await this.request(MSP.STATUS);
    const extra = p[15] & 15;
    if (p.length < 21 + extra || p[16 + extra] < 5) throw new Error('飛控狀態不完整');
    const active = id => {
      const bit = ids.indexOf(id), byte = bit < 32 ? 6 + (bit >> 3) : 16 + ((bit - 32) >> 3);
      if (bit >= 32 && ((bit - 32) >> 3) >= (p[15] & 15)) throw new Error('飛控模式狀態缺失');
      return !!(p[byte] & (1 << (bit & 7)));
    };
    const disabled = new DataView(p.buffer, p.byteOffset, p.byteLength).getUint32(17 + extra, true);
    if (active(0) || active(27) || (disabled & ((1 << 1) | (1 << 2) | (1 << 4))))
      throw new Error('飛控已解鎖或處於 FAILSAFE；停止設定操作');
    return {armed: false, failsafe: false};
  }
  async read() {
    const identity = await this.identity(), ground = await this.groundState();
    const osd = parseOsd(await this.request(MSP.OSD)), canvas = await this.request(MSP.CANVAS);
    if (canvas.length !== 2 || canvas[0] < 16 || canvas[0] > 63 || canvas[1] < 4 || canvas[1] > 31)
      throw new Error('飛控畫布大小無效');
    const rawModes = await this.request(MSP.MODES);
    if (rawModes.length % 4) throw new Error('飛控 Modes 資料不完整');
    const modes = [];
    for (let i = 0; i < rawModes.length; i += 4) if (rawModes[i + 2] < rawModes[i + 3])
      modes.push({id: rawModes[i], aux: rawModes[i + 1] + 1, low: 900 + rawModes[i + 2] * 25, high: 900 + rawModes[i + 3] * 25});
    return {...identity, ...ground, ...osd, canvas: {cols: canvas[0], rows: canvas[1]}, modes};
  }
  async apply(snapshot, rows) {
    if (!Array.isArray(rows) || rows.length !== 4) throw new Error('需要四行位置');
    const before = await this.read();
    if (before.api !== snapshot.api || before.profile !== snapshot.profile ||
        JSON.stringify(before.positions) !== JSON.stringify(snapshot.positions) ||
        JSON.stringify(before.canvas) !== JSON.stringify(snapshot.canvas)) throw new Error('飛控配置已變更；請重讀並重新檢查提案');
    const proposed = rows.map((row, i) => packPosition(before.positions[i], row, before.profile, before.canvas));
    const enabled = rows.filter(row => row.enabled);
    if (enabled.some((a, i) => enabled.slice(i + 1).some(b => a.y === b.y && Math.abs(a.x - b.x) < 16))) throw new Error('Custom Message 行彼此重疊');
    for (let i = 0; i < 4; i++) {
      if (proposed[i] === before.positions[i]) continue;
      await this.groundState();
      await this.request(MSP.SET_OSD, Uint8Array.of(81 + i, proposed[i] & 255, proposed[i] >> 8, 1));
    }
    const verify = parseOsd(await this.request(MSP.OSD));
    if (JSON.stringify(verify.positions) !== JSON.stringify(proposed)) throw new Error('OSD 寫入讀回不符；尚未保存，請重新上電還原');
    await this.groundState(); await this.request(MSP.SAVE);
    const saved = await this.read();
    if (JSON.stringify(saved.positions) !== JSON.stringify(proposed)) throw new Error('OSD 保存後讀回不符；請檢查飛控');
    return saved;
  }
}
