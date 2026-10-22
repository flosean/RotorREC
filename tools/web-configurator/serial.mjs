import { TimeoutError } from './protocol.mjs';

// One reader owns the stream; request timeouts never leave a second reader pending.
export class SerialTransport {
  constructor(port) { this.port = port; this.queue = []; this.bytes = 0; this.error = null; this.waiter = null; }
  async open() {
    await this.port.open({ baudRate: 115200, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none', bufferSize: 8192 });
    try {
      await this.port.setSignals({ dataTerminalReady: false, requestToSend: false });
      this.reader = this.port.readable.getReader(); this.writer = this.port.writable.getWriter(); this.pump = this.receive();
    } catch (error) { await this.port.close().catch(() => {}); throw error; }
  }
  async receive() {
    try {
      while (true) {
        const { value, done } = await this.reader.read(); if (done) break;
        if (this.waiter) { const resolve = this.waiter; this.waiter = null; resolve(value); }
        else { this.queue.push(value); this.bytes += value.length; if (this.bytes > 65536) throw new Error('序列接收緩衝區已滿，請重新連線'); }
      }
      this.error ||= new Error('序列埠已關閉');
    } catch (error) { this.error = error; }
    finally { if (this.waiter) { const resolve = this.waiter; this.waiter = null; resolve(new Uint8Array()); } this.reader.releaseLock(); }
  }
  clear() { this.queue = []; this.bytes = 0; }
  async read(timeout = 100) {
    if (this.error) throw this.error;
    if (this.queue.length) { const value = this.queue.shift(); this.bytes -= value.length; return value; }
    if (this.waiter) throw new Error('不允許並行讀取序列埠');
    return new Promise(resolve => {
      const finish = value => { clearTimeout(timer); resolve(value); };
      const timer = setTimeout(() => { if (this.waiter === finish) this.waiter = null; resolve(new Uint8Array()); }, timeout);
      this.waiter = finish;
    });
  }
  async write(bytes) {
    if (this.error) throw this.error;
    let timer;
    try {
      await Promise.race([this.writer.write(bytes), new Promise((_, reject) => { timer = setTimeout(() => reject(new TimeoutError('序列寫入逾時，請重新連線')), 3000); })]);
    } catch (error) { this.error = error; throw error; }
    finally { clearTimeout(timer); }
  }
  async close() {
    if (this.reader) await this.reader.cancel().catch(() => {});
    if (this.pump) await this.pump;
    if (this.writer) { await this.writer.abort().catch(() => {}); this.writer.releaseLock(); }
    await this.port.close().catch(() => {}); this.clear();
  }
}
