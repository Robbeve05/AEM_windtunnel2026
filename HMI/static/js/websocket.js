export class LiveConnection {
  constructor(onFrame, onState) { this.onFrame = onFrame; this.onState = onState; this.retry = 0; this.last = 0; this.connect();
    this.watchdog = setInterval(() => {if (this.socket?.readyState === WebSocket.OPEN && Date.now() - this.last > 4000) this.socket.close();}, 1000);
  }
  connect() {
    this.onState(false);
    const socket = this.socket = new WebSocket(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws/live`);
    socket.onopen = () => {this.last = Date.now(); this.retry = 0;};
    socket.onmessage = event => {try {const frame = JSON.parse(event.data); this.last = Date.now(); this.onFrame(frame); this.onState(true);} catch (error) {console.error('Telemetry update failed', error);}};
    socket.onerror = () => socket.close();
    socket.onclose = () => {this.onState(false); this.timer = setTimeout(() => this.connect(), Math.min(10000, 500 * 2 ** this.retry++));};
  }
}
