// Lightweight offline canvas plotter for the Raspberry Pi HMI.
export class Chart {
  constructor(canvas, {cp = false, yMin = null, yMax = null} = {}) {
    this.canvas = canvas; this.cp = cp; this.series = []; this.minX = 0; this.maxX = 60;
    this.yMin = yMin; this.yMax = yMax;
    this.observer = new ResizeObserver(() => this.draw()); this.observer.observe(canvas);
  }
  update(series, minX = 0, maxX = 1) { this.series = series; this.minX = minX; this.maxX = maxX; this.draw(); }
  setYRange(min, max) { this.yMin = min; this.yMax = max; this.draw(); }
  destroy() { this.observer.disconnect(); }
  niceStep(range, target = 5) {
    const raw = Math.max(range / target, .0001);
    const power = 10 ** Math.floor(Math.log10(raw));
    const scaled = raw / power;
    const factor = scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10;
    return factor * power;
  }
  draw() {
    const canvas = this.canvas, bounds = canvas.getBoundingClientRect();
    if (bounds.width < 1 || bounds.height < 1) return;
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(bounds.width * ratio); canvas.height = Math.round(bounds.height * ratio);
    const ctx = canvas.getContext('2d'); ctx.scale(ratio, ratio);
    const w = bounds.width, h = bounds.height, left = 58, top = 12, right = 18, bottom = 34;
    const pw = Math.max(1, w - left - right), ph = Math.max(1, h - top - bottom);
    const values = this.series.flatMap(s => s.points.map(p => p.y)).filter(v => v !== null && Number.isFinite(v));
    let lo, hi;
    if (this.cp) {
      lo = Math.min(-1.5, ...values) - .15;
      hi = Math.max(.5, ...values) + .15;
    } else if (Number.isFinite(this.yMin) && Number.isFinite(this.yMax)) {
      lo = this.yMin; hi = this.yMax;
    } else {
      lo = 0; hi = Math.max(5, ...values) * 1.12;
    }
    if (hi <= lo) hi = lo + 1;
    const x = value => left + (value - this.minX) / Math.max(.01, this.maxX - this.minX) * pw;
    const y = value => top + (this.cp ? (value - lo) : (hi - value)) / (hi - lo) * ph;
    ctx.font = '600 11px Arial, sans-serif'; ctx.lineWidth = 1;
    const yLines = 4;
    for (let i = 0; i <= yLines; i++) {
      const yy = top + i / yLines * ph;
      ctx.strokeStyle = '#deded9'; ctx.beginPath(); ctx.moveTo(left, yy); ctx.lineTo(w - right, yy); ctx.stroke();
      ctx.fillStyle = '#666662'; ctx.textAlign = 'right';
      const label = this.cp ? lo + i / yLines * (hi - lo) : hi - i / yLines * (hi - lo);
      ctx.fillText(label.toFixed(this.cp ? 1 : (hi <= 10 ? 1 : 0)), left - 10, yy + 4);
    }
    for (let i = 0; i <= 4; i++) {
      ctx.textAlign = 'center'; ctx.fillStyle = '#777772';
      const value = this.minX + i / 4 * (this.maxX - this.minX);
      ctx.fillText(this.cp ? value.toFixed(2) : `${Math.round(value - this.maxX)}s`, left + i / 4 * pw, h - 10);
    }
    ctx.save(); ctx.beginPath(); ctx.rect(left, top, pw, ph); ctx.clip();
    for (const series of this.series) {
      ctx.strokeStyle = series.color; ctx.lineWidth = series.width || (series.dashed ? 2.2 : 3); ctx.setLineDash(series.dashed ? [9, 7] : []);
      ctx.lineJoin = 'round'; ctx.lineCap = 'round';
      ctx.beginPath(); let started = false;
      for (const point of series.points) {
        if (point.y === null || !Number.isFinite(point.y)) { started = false; continue; }
        if (!started) {ctx.moveTo(x(point.x), y(point.y)); started = true;} else ctx.lineTo(x(point.x), y(point.y));
      }
      ctx.stroke(); ctx.setLineDash([]);
      if (this.cp) for (const point of series.points) {
        if (point.y === null || !Number.isFinite(point.y)) continue;
        ctx.beginPath(); ctx.arc(x(point.x), y(point.y), 3.5, 0, Math.PI * 2); ctx.fillStyle = series.color; ctx.fill();
      }
    }
    ctx.restore();
    if (!values.length) {ctx.fillStyle = '#7f7f7a'; ctx.textAlign = 'center';ctx.fillText('Waiting for valid flow data', w / 2, h / 2);}
  }
}

export function cpSeries(points) {
  return ['upper', 'lower'].map((surface, i) => ({color: i ? '#e18b2d' : '#c8291e',
    points: points.filter(p => p.surface === surface).map(p => ({x: Number(p.x_c), y: p.cp === null || p.cp === '' ? null : Number(p.cp)})).sort((a,b) => a.x-b.x)}));
}
