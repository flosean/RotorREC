import {sleep} from './protocol.mjs';
export class HttpTransport {
  queue = []; token = ''; closed = false;
  constructor(fetcher = fetch) { this.fetcher = fetcher; }
  async open() {
    this.closed = false;
    const response = await this.fetcher('/session', {cache: 'no-store', signal: AbortSignal.timeout(3000)});
    if (!response.ok) throw new Error('無法建立本機 Wi-Fi 維護連線');
    const session = await response.json();
    if (!/^[0-9a-f]{32}$/.test(session.token)) throw new Error('Wi-Fi 維護識別無效');
    this.token = session.token;
  }
  async write(wire) {
    if (this.closed) throw new Error('Wi-Fi 連線已關閉');
    try {
      if (!this.token) await this.open();
      const response = await this.fetcher('/api', {method: 'POST', headers: {'Content-Type': 'application/octet-stream', 'X-RotorREC': this.token},
        body: wire, cache: 'no-store', signal: AbortSignal.timeout(5000)});
      if (response.status === 403) { this.token = ''; return; } // ESP rebooted: renew token before replaying the same wire packet.
      if (response.ok) this.queue.push(new Uint8Array(await response.arrayBuffer()));
    } catch (error) {
      // No new sequence and no synthetic ACK after a network failure. Client will
      // retry the exact CRC/session/sequence packet or verify the reboot outcome.
      if (this.closed) throw error;
      this.token = '';
    }
  }
  async read(timeout = 100) {
    if (this.queue.length) return this.queue.shift();
    await sleep(Math.min(timeout, 100)); return this.queue.shift() ?? new Uint8Array();
  }
  clear() { this.queue = []; }
  async close() { this.closed = true; this.clear(); }
}
