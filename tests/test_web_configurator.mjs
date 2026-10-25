import test from 'node:test';
import assert from 'node:assert/strict';
import { deflateRawSync } from 'node:zlib';
import { Client, CMD, Parser, TimeoutError, crc32, decodeSettings, decodeControl, encodeControl, encodeLayout, decodeLayout, encode, encodeSettings, enterPassthrough, imageMetadata, loadPackage, selectUart, u32 } from '../tools/web-configurator/protocol.mjs';
import { SerialTransport } from '../tools/web-configurator/serial.mjs';
import { HttpTransport } from '../tools/web-configurator/http.mjs';
import { sha256Fallback, sha256 } from '../tools/web-configurator/sha256.mjs';
import { createHash, randomBytes } from 'node:crypto';

const text = new TextEncoder();
test('reset verifies durable intent and restart; Wi-Fi factory reset stays unverified', async () => {
  const client = new Client({}); let pending = 0, wifi = 0, rebooted = false;
  client.info = async () => ({reset_capable: 1, reset_pending: pending, wifi_active: wifi});
  client.request = async (cmd, payload) => {
    if (cmd === CMD.RESET) pending = payload[0];
    else { assert.equal(cmd, CMD.REBOOT); rebooted = true; }
  };
  client.reboot = async expected => ({...expected, settings_raw: 0x01000058});
  assert.equal((await client.resetDevice(1)).reset_pending, 0);
  await assert.rejects(client.resetDevice(0), /無效/);
  wifi = 1; assert.equal(await client.resetDevice(2), null); assert.equal(rebooted, true);
  client.info = async () => ({reset_capable: 0});
  await assert.rejects(client.resetDevice(1), /更新韌體/);
  client.info = async () => ({reset_capable: 1, reset_pending: 0});
  await assert.rejects(client.resetDevice(1), /未保存/);
});
const join = (...parts) => new Uint8Array(Buffer.concat(parts));
const dataView = b => new DataView(b.buffer, b.byteOffset, b.length);
function image() {
  const b = new Uint8Array(1536), d = dataView(b); b[0] = 0xe9; d.setUint16(12, 5, true); d.setUint32(32, 0xabcd5432, true);
  b.set(text.encode('1.2.0'), 48); b.set(text.encode('rotorrec_esp32c3'), 80); b.fill(0x42, 176, 208); return b;
}
function zip(files, method = 8) {
  const locals = [], central = []; let offset = 0;
  for (const [name, content] of files) {
    const n = text.encode(name), packed = method === 8 ? deflateRawSync(content) : content;
    const local = new Uint8Array(30 + n.length), l = dataView(local); l.setUint32(0, 0x04034b50, true); l.setUint16(4, 20, true);
    l.setUint16(8, method, true); l.setUint32(14, crc32(content), true); l.setUint32(18, packed.length, true); l.setUint32(22, content.length, true); l.setUint16(26, n.length, true); local.set(n, 30);
    const c = new Uint8Array(46 + n.length), d = dataView(c); d.setUint32(0, 0x02014b50, true); d.setUint16(6, 20, true); d.setUint16(10, method, true);
    d.setUint32(16, crc32(content), true); d.setUint32(20, packed.length, true); d.setUint32(24, content.length, true); d.setUint16(28, n.length, true); d.setUint32(42, offset, true); c.set(n, 46);
    locals.push(local, packed); central.push(c); offset += local.length + packed.length;
  }
  const end = new Uint8Array(22), e = dataView(end); e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true);
  e.setUint32(12, central.reduce((sum, p) => sum + p.length, 0), true); e.setUint32(16, offset, true); return join(...locals, ...central, end);
}

test('Python wire golden fixtures, fragmented/corrupt frames and noise', () => {
  assert.equal(Buffer.from(encode(CMD.HELLO, 0x12345678, 0)).toString('hex'), '5252454301017856341200000000000057293b0f');
  assert.equal(Buffer.from(encode(CMD.SETTINGS, 0x12345678, 19, u32(0x01000050))).toString('hex'), '52524543010b78563412130000000400500000011e5d0260');
  const valid = encode(0x81, 42, 19, u32(0)); const corrupt = valid.slice(); corrupt[19] ^= 1;
  const parser = new Parser(), packets = []; for (const b of join(text.encode('$X>noise'), corrupt, valid)) packets.push(...parser.feed(Uint8Array.of(b)));
  assert.equal(packets.length, 1); assert.equal(packets[0].sequence, 19); assert.equal(packets[0].session, 42);
  assert.throws(() => encode(1, 2, 3, new Uint8Array(1041)));
});
test('settings roundtrip, old ownership retained, corrupt values rejected', () => {
  const defaults = { user_mode_id: 40, osd_lines: 2, led_percent: 5, low_battery_percent: 0, link_paused: false, owns_extra_lines: false };
  assert.equal(encodeSettings(defaults), 0x01000050); assert.deepEqual(decodeSettings(0x01000050), defaults);
  for (const user of [40, 43]) for (const lines of [2, 4]) for (const percent of [1, 100]) {
    const s = { ...defaults, user_mode_id: user, osd_lines: lines, led_percent: percent, low_battery_percent: 100, link_paused: true, owns_extra_lines: true };
    assert.deepEqual(decodeSettings(encodeSettings(s)), s);
  }
  for (const raw of [0, -1, 2 ** 32, 0x01000000, 0x01000054, 0x01080050, true]) assert.throws(() => decodeSettings(raw));
});

test('OSD templates validate known tokens and require saved plus reboot readback', async () => {
  const rows = ['{state}', '{battery} {remaining}', 'TEST {format}', ''];
  assert.deepEqual(decodeLayout(encodeLayout(rows)), rows);
  for (const row of ['{typo}', '{state', '}', '中文字', 'X'.repeat(64), '{mode}'.repeat(9)])
    assert.throws(() => encodeLayout([row, '', '', '']));
  const client = new Client({}); let saved;
  client.info = async () => ({osd_layout_version: 1, saved_osd_layout: saved});
  client.request = async (command, payload) => { assert.equal(command, CMD.SET_OSD_LAYOUT); saved = decodeLayout(payload); };
  client.reboot = async () => ({osd_layout: saved});
  assert.deepEqual((await client.setLayout(rows)).osd_layout, rows);
  client.reboot = async () => ({osd_layout: ['', '', '', '']});
  await assert.rejects(client.setLayout(rows), /重啟讀回不符/);
});
test('record settings migrate, preserve legacy fields and verify both words after restart', async () => {
  const control = { record_mode: 0, stop_delay_seconds: 5, auto_record: true, auto_stop: true,
    low_remaining_seconds: 0, heads_up_warnings: true };
  assert.equal(encodeControl(control), 0x01400328);
  assert.deepEqual(decodeControl(0x01400328), control);
  for (const raw of [0, -1, true, 0x02000000, 0x01800000, 0x010000f8, 0x01000007]) assert.throws(() => decodeControl(raw));
  const client = new Client({}), writes = [];
  client.info = async () => ({ settings_raw: 0x01000050, control_raw: encodeControl(control) });
  client.request = async (cmd, payload) => writes.push({ cmd, payload });
  client.reboot = async expected => expected;
  const result = await client.setSettings({ record_mode: 3, stop_delay_seconds: 30, auto_stop: false });
  assert.equal(writes[0].cmd, CMD.SETTINGS); assert.equal(writes[0].payload.length, 8);
  assert.equal(dataView(writes[0].payload).getUint32(4, true), result.control_raw);
  assert.deepEqual(decodeControl(result.control_raw), { ...control, record_mode: 3, stop_delay_seconds: 30, auto_stop: false });
  client.info = async () => ({ settings_raw: 0x01000050 });
  await assert.rejects(client.setSettings({ record_mode: 1 }), /不支援/);
  assert.equal(writes.length, 1);
});

test('UART selection never probes other peripherals or guesses among MSP candidates', () => {
  const ports = 'serial 20 1 115200 0 0 0\r\nserial 0 64 115200 0 0 0\r\nserial 2 1 115200 0 0 0\r\n';
  assert.equal(selectUart(ports), 3); assert.equal(selectUart(ports, '3'), 3); assert.throws(() => selectUart(ports, '1'));
  assert.throws(() => selectUart(ports + 'serial 1 1 115200 0 0 0\n')); assert.throws(() => selectUart('serial 0 1 57600 0 0 0\n'));
});
test('CLI handles fragmented # Betaflight prefix and waits for an idle prompt', async () => {
  let queue = [], command = ''; const writes = [];
  const port = {
    clear() { queue = []; },
    async write(bytes) {
      command = new TextDecoder().decode(bytes); writes.push(command);
      if (command === 'version\r\n') queue.push(text.encode('\r\n# '), text.encode('Betaflight 4.5\r\n# '));
      if (command === 'serial\r\n') queue.push(text.encode('\r\nserial 2 1 115200 0 0 0\r\n# '));
      if (command === 'serialpassthrough 2 115200 rxtx\r\n') queue.push(text.encode('Forwarding'));
    }, async read() { return queue.shift() ?? new Uint8Array(); }
  };
  assert.equal(await enterPassthrough(port), 3); assert.equal(writes.at(-1), 'serialpassthrough 2 115200 rxtx\r\n');
});
test('ZIP stored/deflated packages verify descriptor, manifest, CRC and exact entries', async () => {
  const app = image(), metadata = await imageMetadata(app), manifest = text.encode(JSON.stringify(metadata));
  for (const method of [0, 8]) assert.deepEqual((await loadPackage(zip([['manifest.json', manifest], ['app.bin', app]], method))).metadata, metadata);
  await assert.rejects(loadPackage(zip([['manifest.json', manifest], ['app.bin', app], ['extra', new Uint8Array()]])));
  await assert.rejects(loadPackage(zip([['manifest.json', manifest], ['manifest.json', manifest]])));
  await assert.rejects(loadPackage(zip([['manifest.json', text.encode(JSON.stringify({ ...metadata, board: 'rotorrec_esp32c6' }))], ['app.bin', app]])));
  const corrupt = zip([['manifest.json', manifest], ['app.bin', app]], 0); corrupt[45] ^= 1; await assert.rejects(loadPackage(corrupt));
  const wrongChip = app.slice(); wrongChip[12] = 13; await assert.rejects(imageMetadata(wrongChip));
});

class Peripheral {
  constructor() { this.queue = []; this.cache = new Map(); this.requests = []; this.received = new Uint8Array(); this.version = 'old'; this.elf = 'old'; this.dropData = true; this.committed = false; }
  async write(wire) {
    const key = Buffer.from(wire).toString('hex'); this.requests.push(wire);
    if (this.cache.has(key)) { this.queue.push(this.cache.get(key)); return; }
    const packet = new Parser().feed(wire)[0]; let output = new Uint8Array();
    switch (packet.command) {
      case CMD.HELLO: case CMD.INFO:
        output = text.encode(JSON.stringify({ product: 'RotorREC', protocol: 1, board: 'rotorrec_esp32c3', layout: 1, capacity: 4096, pending_verify: 0, version: this.version, elf_sha256: this.elf })); break;
      case CMD.BEGIN: this.expectedSize = dataView(packet.payload).getUint32(0, true); break;
      case CMD.DATA: assert.equal(dataView(packet.payload).getUint32(0, true), this.received.length); this.received = join(this.received, packet.payload.slice(4)); break;
      case CMD.END: assert.equal(this.received.length, this.expectedSize); this.committed = true; break;
      case CMD.ABORT: this.aborted = true; break;
      case CMD.REBOOT: {
        assert.ok(this.committed); const metadata = await imageMetadata(this.received); this.version = metadata.version; this.elf = metadata.elf_sha256; break;
      }
    }
    const reply = encode(packet.command | 0x80, packet.session, packet.sequence, join(u32(0), output)); this.cache.set(key, reply);
    if (packet.command === CMD.DATA && this.dropData) this.dropData = false; else this.queue.push(reply);
  }
  async read() { return this.queue.shift() ?? new Uint8Array(); }
}

test('telemetry uses its own bounded reply and older firmware needs no extra request', async () => {
  const client = new Client({}), calls = [];
  client.request = async command => {
    calls.push(command);
    return text.encode(JSON.stringify(command === CMD.TELEMETRY ? { record_time: 70000, remain_time: null, warnings: 3 } :
      { product: 'RotorREC', protocol: 1, board: 'rotorrec_esp32c3', telemetry_version: 1 }));
  };
  const value = await client.info(true);
  assert.deepEqual(calls, [CMD.HELLO, CMD.TELEMETRY]);
  assert.equal(value.telemetry.record_time, 70000); assert.equal(value.telemetry.remain_time, null);
  const old = new Client(new Peripheral());
  assert.equal((await old.info()).telemetry, undefined);
});
function fastClient(port) {
  const client = new Client(port), request = client.request.bind(client);
  client.request = (cmd, payload, timeout, retries) => request(cmd, payload, 3, retries ?? 3); return client;
}
test('real update flow retransmits exact lost-ACK chunk, writes once, verifies boot fingerprint', async () => {
  const app = image(), metadata = await imageMetadata(app), port = new Peripheral(), client = fastClient(port), progress = [];
  await client.info(true); const status = await client.update({ metadata, image: app }, p => progress.push(p.phase));
  assert.deepEqual(port.received, app); assert.equal(status.elf_sha256, metadata.elf_sha256); assert.ok(progress.includes('verify'));
  const packets = port.requests.map(w => new Parser().feed(w)[0]); const chunks = packets.filter(p => p.command === CMD.DATA);
  assert.equal(chunks.length, 3); assert.equal(chunks[0].sequence, chunks[1].sequence); assert.equal(port.aborted, undefined);
});
test('local HTTP SHA-256 fallback matches independent native hashes at padding and image boundaries', async () => {
  for (const length of [0, 1, 55, 56, 63, 64, 65, 127, 128, 1536, 0x410000]) {
    const bytes = randomBytes(length), expected = createHash('sha256').update(bytes).digest('hex');
    assert.equal(Buffer.from(sha256Fallback(bytes)).toString('hex'), expected);
  }
  const original = Object.getOwnPropertyDescriptor(globalThis, 'crypto');
  try {
    Object.defineProperty(globalThis, 'crypto', {value: {}, configurable: true});
    assert.equal(Buffer.from(await sha256(text.encode('abc'))).toString('hex'), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  } finally { Object.defineProperty(globalThis, 'crypto', original); }
  assert.throws(() => sha256Fallback(new Uint8Array(0x410001)));
});
test('HTTP update recovers lost response, renews reboot token and verifies fingerprint; cancel aborts', async () => {
  for (const cancel of [false, true]) {
    const peripheral = new Peripheral(); let token = 'a'.repeat(32), sessions = 0, denied = 0;
    const transport = new HttpTransport(async (url, options) => {
      if (url === '/session') { sessions++; return Response.json({token}); }
      assert.equal(url, '/api');
      if (options.headers['X-RotorREC'] !== token) { denied++; return new Response('', {status: 403}); }
      const packet = new Parser().feed(options.body)[0];
      await peripheral.write(options.body);
      const response = peripheral.queue.shift();
      if (packet.command === CMD.REBOOT) token = 'b'.repeat(32);
      if (!response) throw new TypeError('lost HTTP response after device applied command');
      return new Response(response);
    });
    await transport.open(); const client = fastClient(transport), app = image();
    const pkg = {image: app, metadata: await imageMetadata(app)}; let requestedCancel = false;
    if (cancel) {
      await assert.rejects(client.update(pkg, () => { requestedCancel = true; }, () => requestedCancel), /取消/);
      assert.equal(peripheral.aborted, true); assert.equal(peripheral.committed, false);
    } else {
      const value = await client.update(pkg);
      assert.deepEqual(peripheral.received, app); assert.equal(value.elf_sha256, pkg.metadata.elf_sha256);
      assert.ok(sessions >= 3 && denied === 1);
      const chunks = peripheral.requests.filter(w => new Parser().feed(w)[0].command === CMD.DATA);
      assert.deepEqual(chunks[0], chunks[1]);
    }
    await transport.close(); await assert.rejects(transport.write(u32(0)), /關閉/);
  }
});
test('cancel aborts before commit; wrong board does not BEGIN', async () => {
  const app = image(), metadata = await imageMetadata(app), port = new Peripheral(), client = fastClient(port); let cancel = false;
  await assert.rejects(client.update({ metadata, image: app }, () => { cancel = true; }, () => cancel), /取消/);
  assert.equal(port.aborted, true); assert.equal(port.committed, false);
  const other = new Peripheral(), c = fastClient(other);
  await assert.rejects(c.update({ metadata: { ...metadata, board: 'rotorrec_esp32c6' }, image: app }), /板型/);
  assert.equal(other.requests.some(w => new Parser().feed(w)[0].command === CMD.BEGIN), false);
});
test('lost END replies recover with fresh HELLO only, never ABORT or old-session REBOOT', async () => {
  const app = image(), metadata = await imageMetadata(app), port = new Peripheral(), client = fastClient(port);
  const originalWrite = port.write.bind(port); let endSession;
  port.write = async wire => {
    const p = new Parser().feed(wire)[0];
    if (endSession && p.command === CMD.HELLO && p.session !== endSession) { port.version = metadata.version; port.elf = metadata.elf_sha256; }
    await originalWrite(wire);
    if (p.command === CMD.END) { endSession = p.session; port.queue = []; }
  };
  const status = await client.update({ metadata, image: app }); assert.equal(status.elf_sha256, metadata.elf_sha256);
  const commands = port.requests.map(w => new Parser().feed(w)[0].command); assert.ok(!commands.includes(CMD.ABORT)); assert.ok(!commands.includes(CMD.REBOOT));
});
test('reboot rejects same-version wrong fingerprint; timeout forbids sequence reuse', async () => {
  const port = new Peripheral(), client = fastClient(port); port.committed = true; port.received = image();
  await assert.rejects(client.reboot({ version: '1.2.0', elf_sha256: 'wrong' }), /elf_sha256/);
  const silent = fastClient({ write: async () => {}, read: async () => new Uint8Array() });
  await assert.rejects(silent.request(CMD.HELLO), TimeoutError); await assert.rejects(silent.request(CMD.ABORT), /結果不明/);
});
test('pending-image crash requires a fresh HELLO and detects rollback', async () => {
  const client = new Client({}), calls = []; let stage = 0, pendingSession;
  client.info = async hello => {
    calls.push(hello);
    if (stage++ === 0) { assert.equal(hello, true); pendingSession = client.session; client.sequence = 1; return { pending_verify: 1 }; }
    if (stage === 2) { assert.equal(hello, false); throw new TimeoutError('reset during pending verify'); }
    assert.equal(hello, true); assert.notEqual(client.session, pendingSession); return { pending_verify: 0, elf_sha256: 'old' };
  };
  await assert.rejects(client.waitForBoot({ elf_sha256: 'new' }), /elf_sha256/); assert.deepEqual(calls, [true, false, true]);
});
test('Web Serial reader timeout leaves one pump, releases locks on close', async () => {
  let controller, closed = false;
  const port = { readable: new ReadableStream({ start(c) { controller = c; } }), writable: new WritableStream(),
    open: async options => assert.equal(options.baudRate, 115200), setSignals: async value => assert.equal(value.dataTerminalReady, false), close: async () => { closed = true; } };
  const serial = new SerialTransport(port); await serial.open(); assert.equal((await serial.read(2)).length, 0);
  const pending = serial.read(100); controller.enqueue(Uint8Array.of(1, 2, 3)); assert.deepEqual(await pending, Uint8Array.of(1, 2, 3));
  await serial.close(); assert.equal(port.readable.locked, false); assert.equal(port.writable.locked, false); assert.equal(closed, true);
});
