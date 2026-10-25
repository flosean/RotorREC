import test from 'node:test';
import assert from 'node:assert/strict';
import {FlightSetup, MSP, MspParser, mspFrame, parseOsd, packPosition, unpackPosition} from '../tools/web-configurator/bf-setup.mjs';

function osd(positions = [0, 0, 0, 0]) {
  const bytes = new Uint8Array(191), view = new DataView(bytes.buffer);
  bytes[0] = 1; bytes[7] = 85; bytes[189] = 3; bytes[190] = 1;
  positions.forEach((raw, i) => view.setUint16(10 + (81 + i) * 2, raw, true)); return bytes;
}
class Flight extends FlightSetup {
  constructor() { super({}); this.positions = [0xc800, 4096, 0, 0]; this.armed = false; this.failsafe = false; this.calls = []; }
  async request(command, payload) {
    this.calls.push({command, payload});
    if (command === MSP.API) return Uint8Array.of(0, 1, 47);
    if (command === MSP.VARIANT) return new TextEncoder().encode('BTFL');
    if (command === MSP.BOXIDS) return Uint8Array.of(0, 27, 40);
    if (command === MSP.STATUS) { const b = new Uint8Array(22); b[16] = 5; b[17] = this.rxFailsafe ? 4 : 0; b[6] = Number(this.armed) | (Number(this.failsafe) << 1); return b; }
    if (command === MSP.OSD) return osd(this.positions);
    if (command === MSP.CANVAS) return Uint8Array.of(53, 20);
    if (command === MSP.MODES) return Uint8Array.of(40, 1, 32, 48);
    if (command === MSP.SET_OSD) { assert.equal(payload[3], 1); this.positions[payload[0] - 81] = payload[1] | (payload[2] << 8); return new Uint8Array(); }
    if (command === MSP.SAVE) { this.saved = [...this.positions]; return new Uint8Array(); }
    throw Error('Unexpected command');
  }
}
test('MSPv2 framing accepts fragments and rejects CRC, errors and forbidden commands', () => {
  const packet = mspFrame(MSP.API, Uint8Array.of(0, 1, 47), '>'), parser = new MspParser();
  assert.deepEqual([...packet], [36,88,62,0,1,0,3,0,0,1,47,0x49]);
  const replies = []; for (const byte of packet) replies.push(...parser.feed(Uint8Array.of(byte)));
  assert.equal(replies.length, 1); assert.equal(replies[0].payload[2], 47);
  const bad = packet.slice(); bad[9] ^= 1; assert.equal(parser.feed(bad).length, 0);
  assert.equal(parser.feed(packet).length, 1);
  assert.equal(parser.feed(mspFrame(MSP.API, new Uint8Array(), '!'))[0].error, true);
  for (const command of [99, 200, 214, 68]) assert.throws(() => mspFrame(command));
});
test('OSD schema, HD coordinates, type bits and unrelated profile visibility are preserved', () => {
  const parsed = parseOsd(osd()); assert.equal(parsed.profile, 1); assert.equal(parsed.positions.length, 4);
  const packed = packPosition(0xf800, {x: 37, y: 19, enabled: false}, 1, {cols: 53, rows: 20});
  assert.deepEqual(unpackPosition(packed, 1), {x: 37, y: 19, enabled: false});
  assert.equal(packed & 0xf800, 0xf000);
  assert.throws(() => packPosition(0, {x: 38, y: 0, enabled: true}, 1, {cols: 53, rows: 20}));
  for (let length = 0; length < 191; length++) assert.throws(() => parseOsd(osd().subarray(0, length)));
});
test('explicit OSD install checks disarmed state before each write and save, then reads back', async () => {
  const fc = new Flight(), before = await fc.read();
  assert.deepEqual(before.modes, [{id: 40, aux: 2, low: 1700, high: 2100}]);
  assert.ok(!fc.calls.some(c => [MSP.SET_OSD, MSP.SAVE].includes(c.command)));
  const rows = Array.from({length: 4}, (_, y) => ({x: 1, y, enabled: true}));
  const saved = await fc.apply(before, rows);
  assert.deepEqual(saved.positions, fc.saved);
  assert.equal(saved.positions[0] & 0xc000, 0xc000); assert.equal(saved.positions[1] & 4096, 4096);
  for (let i = 0; i < fc.calls.length; i++) if ([MSP.SET_OSD, MSP.SAVE].includes(fc.calls[i].command))
    assert.equal(fc.calls[i - 1].command, MSP.STATUS);
});
test('armed, failsafe, stale proposals and overlapping rows do not write', async () => {
  const rows = Array.from({length: 4}, (_, y) => ({x: 1, y, enabled: true}));
  for (const mode of ['armed', 'failsafe', 'rxFailsafe', 'stale', 'overlap']) {
    const fc = new Flight(), before = await fc.read(); fc.calls = [];
    if (mode === 'armed') fc.armed = true;
    if (mode === 'failsafe') fc.failsafe = true;
    if (mode === 'rxFailsafe') fc.rxFailsafe = true;
    if (mode === 'stale') fc.positions[0]++;
    await assert.rejects(fc.apply(before, mode === 'overlap' ? [rows[0], rows[0], ...rows.slice(2)] : rows));
    assert.ok(!fc.calls.some(c => [MSP.SET_OSD, MSP.SAVE].includes(c.command)));
  }
});
test('transport exceptions stop later commands until a fresh connection', async () => {
  for (const stage of ['write', 'read']) {
    let writes = 0;
    const fc = new FlightSetup({write: async () => { writes++; if (stage === 'write') throw Error('USB removed'); }, read: async () => { throw Error('USB removed'); }});
    await assert.rejects(fc.request(MSP.STATUS), /USB removed/);
    await assert.rejects(fc.request(MSP.SAVE), /未知/);
    assert.equal(writes, 1);
  }
});

test('unknown protocol and unterminated mode maps are rejected before writes', async () => {
  const fc = new Flight(), original = fc.request.bind(fc);
  fc.request = async (command, payload) => command === MSP.API ? Uint8Array.of(1, 1, 47) : original(command, payload);
  await assert.rejects(fc.read(), /僅支援/);
  const ids = Uint8Array.from({length: 128}, (_, i) => i);
  fc.request = async (command, payload) => command === MSP.BOXIDS ? ids.slice(payload[0] * 32, (payload[0] + 1) * 32) : original(command, payload);
  assert.deepEqual(await fc.groundState(), {armed: false, failsafe: false});
  const valid = fc.request;
  fc.request = async (command, payload) => command === MSP.BOXIDS && payload[0] === 4 ? Uint8Array.of(129) : valid(command, payload);
  await assert.rejects(fc.groundState(), /對照資料無效/);
  assert.ok(!fc.calls.some(c => [MSP.SET_OSD, MSP.SAVE].includes(c.command)));
});
test('an ARM change during application prevents remaining writes and EEPROM save', async () => {
  const fc = new Flight(), before = await fc.read(), request = fc.request.bind(fc);
  fc.request = async (command, payload) => { const result = await request(command, payload); if (command === MSP.SET_OSD) fc.armed = true; return result; };
  await assert.rejects(fc.apply(before, Array.from({length: 4}, (_, y) => ({x: 1, y, enabled: true}))));
  assert.equal(fc.calls.filter(c => c.command === MSP.SET_OSD).length, 1);
  assert.equal(fc.saved, undefined);
});
