#!/usr/bin/env python3
"""Map Picker: choose any area for VisorMesh's Map screen and build its
basemap (VisorMeshMap.pdb), on macOS, Windows or Linux. Needs only Python 3.

    python3 tools/map_picker.py [--port 8765] [--cache DIR] [--out-dir DIR] [--no-browser]

It opens a page in the browser with a world map. Drag the blue box over the
area to cover and the orange one over the part that gets street-level detail;
the page shows each zoom's tiles and the file's estimated size. Build fetches
the OpenStreetMap data for the area from the Overpass API (cached, so only
once), draws the tiles with make_map.py and writes the database to the output
folder (default ~/VisorMeshMap), also offered as a download.

Install it like any Palm database: `pilot-xfer -p usb: -i VisorMeshMap.pdb`
(macOS, Linux; or `make install-map` in palm-client), or Palm Desktop's
Install Tool (Windows). It replaces the map that's on the Visor.

The server only listens on this computer (127.0.0.1).
"""
import argparse
import json
import os
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_map  # noqa: E402

# Rough sizes per tile, from the Philadelphia map: street-level tiles in a
# built-up area, and the wider zooms. Countryside comes out smaller.
DETAIL_KB = 5.0
OTHER_KB = 3.2
MAX_SPAN = 6.0          # degrees per side: the header's projection tables, and patience
MAX_KB = 6000           # the Visor Neo has 8 MB for everything
MAX_FETCHES = 400       # cells x layers, to stay polite to the public Overpass server


def check(req):
    """The request's area, detail area and max zoom, or a ValueError for the page."""
    try:
        area = [float(v) for v in req['area']]
        detail = [float(v) for v in req['detail']]
        max_zoom = int(req['maxZoom'])
    except (KeyError, TypeError, ValueError):
        raise ValueError('Bad request.')
    s, w, n, e = area
    if not (-85 <= s < n <= 85 and -180 <= w < e <= 180):
        raise ValueError('The area must be within latitude 85 and not cross the 180th meridian.')
    if n - s > MAX_SPAN or e - w > MAX_SPAN:
        raise ValueError('The area can be at most %g degrees on a side.' % MAX_SPAN)
    if max_zoom not in (12, 13, 14):
        raise ValueError('The street-level zoom must be 12, 13 or 14.')
    # the street-level area, kept inside the whole area
    ds, dw, dn, de = max(detail[0], s), max(detail[1], w), min(detail[2], n), min(detail[3], e)
    if ds >= dn or dw >= de:
        raise ValueError('The street-level area (orange) must overlap the map area (blue).')
    return (s, w, n, e), (ds, dw, dn, de), max_zoom


def plan(cache, area, detail, max_zoom):
    min_zoom = make_map.auto_min_zoom(area, max_zoom)
    zooms, total = [], 0.0
    for z, d, _, cols, rows in make_map.zoom_plan(area, detail, min_zoom, max_zoom):
        kb = cols * rows * (DETAIL_KB if d else OTHER_KB)
        zooms.append({'z': z, 'detail': d, 'cols': cols, 'rows': rows, 'kb': round(kb)})
        total += kb
    jobs, cached = make_map.cell_jobs(cache, area, detail)
    problems = []
    if total > MAX_KB:
        problems.append('About %.1f MB is too big for the Visor (at most %.0f MB here): make the orange box '
                        'smaller or the street-level zoom lower.' % (total / 1024, MAX_KB / 1024))
    if len(jobs) - cached > MAX_FETCHES:
        problems.append('That needs %d downloads from the OpenStreetMap server; pick a smaller area '
                        '(at most %d at a time).' % (len(jobs) - cached, MAX_FETCHES))
    return {'minZoom': min_zoom, 'zooms': zooms, 'totalKb': round(total), 'fetches': len(jobs),
            'cached': cached, 'problems': problems, 'area': area, 'detail': detail}


class Job:
    """The one build at a time, and what the page polls for."""

    def __init__(self):
        self.lock = threading.Lock()
        self.running = False
        self.cancel = False
        self.phase = ''
        self.done = self.total = 0
        self.log = []
        self.result = None      # {'file', 'kb', 'previews'} once built
        self.error = None

    def say(self, msg):
        with self.lock:
            self.log.append(time.strftime('%H:%M:%S ') + msg)
            del self.log[:-300]

    def status(self):
        with self.lock:
            return {'running': self.running, 'phase': self.phase, 'done': self.done, 'total': self.total,
                    'log': list(self.log), 'result': self.result, 'error': self.error}


def run_build(job, cache, out_dir, area, detail, max_zoom):
    try:
        jobs, _ = make_map.cell_jobs(cache, area, detail)
        job.phase, job.done, job.total = 'Downloading OpenStreetMap data', 0, len(jobs)
        failed = 0
        for name, cell in jobs:
            if job.cancel:
                raise RuntimeError('Stopped.')
            if not os.path.exists(make_map.cell_path(cache, name, cell)):
                job.say('fetching %s for %+.2f, %+.2f' % (name[:-5], cell[0], cell[1]))
                if not make_map.fetch_cell(cache, name, cell, log=job.say):
                    failed += 1
            job.done += 1
        if failed:
            raise RuntimeError('%d downloads failed (the server is busy?). Build again later: '
                               'what was fetched is kept.' % failed)
        job.phase, job.done, job.total = 'Reading the data', 0, 0
        layers = make_map.cell_layers(cache, area, detail)
        job.say('%d roads, %d streets, %d water, %d parks and rail' % (
            len(layers['major']), len(layers['minor']), len(layers['water']), len(layers['parks'])))
        min_zoom = make_map.auto_min_zoom(area, max_zoom)
        job.phase, job.total = 'Drawing the tiles', max_zoom - min_zoom + 1
        out = os.path.join(out_dir, 'VisorMeshMap.pdb')
        preview = os.path.join(out_dir, 'preview')

        def log(msg):
            job.say(msg)
            if msg.startswith('zoom '):
                job.done += 1
        size = make_map.build(area, detail, min_zoom, max_zoom, 2, layers, out, preview, log=log)
        with job.lock:
            job.result = {'file': out, 'kb': size // 1024,
                          'previews': ['z%d.png' % z for z in range(min_zoom, max_zoom + 1)]}
            job.phase = 'Done'
    except Exception as e:
        with job.lock:
            job.error = str(e)
            job.phase = 'Stopped'
        job.say('error: %s' % e)
    finally:
        with job.lock:
            job.running = False


def make_handler(cache, out_dir, job):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def send(self, code, body, ctype='application/json', extra=None):
            if isinstance(body, (dict, list)):
                body = json.dumps(body).encode()
            elif isinstance(body, str):
                body = body.encode()
            self.send_response(code)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(body)

        def file(self, path, ctype, name=None):
            if not os.path.isfile(path):
                return self.send(404, {'error': 'Not built yet.'})
            with open(path, 'rb') as f:
                data = f.read()
            extra = {'Content-Disposition': 'attachment; filename="%s"' % name} if name else None
            self.send(200, data, ctype, extra)

        def do_GET(self):
            if self.path == '/':
                return self.send(200, PAGE, 'text/html; charset=utf-8')
            if self.path == '/api/status':
                return self.send(200, job.status())
            if self.path == '/api/download':
                return self.file(os.path.join(out_dir, 'VisorMeshMap.pdb'), 'application/octet-stream',
                                 'VisorMeshMap.pdb')
            if self.path.startswith('/api/preview/'):
                name = os.path.basename(self.path)
                if name.startswith('z') and name.endswith('.png') and name[1:-4].isdigit():
                    return self.file(os.path.join(out_dir, 'preview', name), 'image/png')
            self.send(404, {'error': 'Not found.'})

        def do_POST(self):
            # a page on another site can't post JSON here without CORS, but check anyway
            origin = self.headers.get('Origin')
            if origin and not origin.startswith(('http://127.0.0.1:', 'http://localhost:')):
                return self.send(403, {'error': 'Forbidden.'})
            try:
                req = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))) or b'{}')
            except ValueError:
                return self.send(400, {'error': 'Bad request.'})
            if self.path == '/api/cancel':
                job.cancel = True
                return self.send(200, {'ok': True})
            if self.path not in ('/api/plan', '/api/build'):
                return self.send(404, {'error': 'Not found.'})
            try:
                area, detail, max_zoom = check(req)
            except ValueError as e:
                return self.send(400, {'error': str(e)})
            p = plan(cache, area, detail, max_zoom)
            if self.path == '/api/plan':
                return self.send(200, p)
            if p['problems']:
                return self.send(400, {'error': ' '.join(p['problems'])})
            with job.lock:
                if job.running:
                    return self.send(409, {'error': 'A build is already running.'})
                job.running, job.cancel, job.log, job.result, job.error = True, False, [], None, None
            threading.Thread(target=run_build, args=(job, cache, out_dir, area, detail, max_zoom),
                             daemon=True).start()
            return self.send(200, {'ok': True})
    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, default=8765)
    ap.add_argument('--cache', default=os.path.join(os.path.expanduser('~'), '.visormesh', 'map-cache'))
    ap.add_argument('--out-dir', default=os.path.join(os.path.expanduser('~'), 'VisorMeshMap'))
    ap.add_argument('--no-browser', action='store_true')
    a = ap.parse_args()
    os.makedirs(a.cache, exist_ok=True)
    os.makedirs(a.out_dir, exist_ok=True)
    handler = make_handler(a.cache, a.out_dir, Job())
    try:
        server = ThreadingHTTPServer(('127.0.0.1', a.port), handler)
    except OSError:
        server = ThreadingHTTPServer(('127.0.0.1', 0), handler)     # the port is taken: any free one
    url = 'http://127.0.0.1:%d/' % server.server_address[1]
    print('VisorMesh Map Picker at %s' % url, flush=True)
    print('Maps are saved in %s; OpenStreetMap data is cached in %s' % (a.out_dir, a.cache))
    print('Press Ctrl+C to quit.', flush=True)
    if not a.no_browser:
        threading.Timer(0.5, webbrowser.open, (url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


PAGE = r'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VisorMesh Map Picker</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<style>
  :root { --bg: #f6f6f3; --panel: #ffffff; --text: #1d1d1b; --muted: #66665f; --line: #deded8;
          --area: #2a6fdb; --detail: #e07b00; --bad: #b42318; --ok: #1f7a3d; }
  @media (prefers-color-scheme: dark) {
    :root { --bg: #161615; --panel: #1f1f1d; --text: #ecece6; --muted: #a3a39b; --line: #34342f;
            --area: #6aa1ff; --detail: #ffa640; --bad: #ff7a6b; --ok: #5fcf85; }
  }
  * { box-sizing: border-box; }
  html, body { height: 100%; margin: 0; }
  body { background: var(--bg); color: var(--text); font: 14px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif;
         display: flex; }
  #map { flex: 1; min-width: 0; }
  #panel { width: 360px; background: var(--panel); border-left: 1px solid var(--line); padding: 16px;
           overflow-y: auto; display: flex; flex-direction: column; gap: 12px; }
  @media (max-width: 760px) {
    body { flex-direction: column; }
    #map { flex: none; height: 55vh; }
    #panel { width: auto; border-left: 0; border-top: 1px solid var(--line); }
  }
  h1 { font-size: 18px; margin: 0; }
  p { margin: 0; }
  .muted { color: var(--muted); }
  .key { display: inline-block; width: 10px; height: 10px; border-radius: 2px; margin-right: 4px; vertical-align: 0; }
  table { width: 100%; border-collapse: collapse; font-variant-numeric: tabular-nums; }
  td, th { padding: 3px 4px; border-bottom: 1px solid var(--line); text-align: right; }
  td:first-child, th:first-child { text-align: left; }
  th { font-weight: 600; color: var(--muted); font-size: 12px; }
  tfoot td { font-weight: 600; border-bottom: 0; }
  label { display: flex; align-items: center; gap: 8px; }
  select, button { font: inherit; }
  button { padding: 7px 12px; border-radius: 6px; border: 1px solid var(--line); background: var(--bg);
           color: var(--text); cursor: pointer; }
  button.primary { background: var(--area); border-color: var(--area); color: #fff; font-weight: 600; }
  button:disabled { opacity: .5; cursor: default; }
  .row { display: flex; gap: 8px; flex-wrap: wrap; }
  .problem { color: var(--bad); }
  .ok { color: var(--ok); }
  progress { width: 100%; }
  #log { font: 12px/1.4 ui-monospace, Menlo, Consolas, monospace; background: var(--bg); border: 1px solid var(--line);
         border-radius: 6px; padding: 6px; height: 140px; overflow-y: auto; white-space: pre-wrap; }
  .previews { display: flex; gap: 6px; flex-wrap: wrap; }
  .previews a { font-size: 12px; }
  code { font: 12px ui-monospace, Menlo, Consolas, monospace; background: var(--bg); padding: 1px 4px; border-radius: 4px; }
  .handle { width: 14px; height: 14px; border-radius: 50%; border: 2px solid #fff; box-shadow: 0 0 0 1px rgba(0,0,0,.4); }
  .handle.move { border-radius: 3px; }
</style>
</head>
<body>
<div id="map"></div>
<div id="panel">
  <div>
    <h1>VisorMesh Map Picker</h1>
    <p class="muted">Choose the area for the Visor's Map screen.</p>
  </div>
  <p><span class="key" style="background:var(--area)"></span><b>Map area</b>: every zoom covers it, with the main roads, water and parks.<br>
     <span class="key" style="background:var(--detail)"></span><b>Street level</b>: the closest zoom, with every street. This is most of the size.</p>
  <p class="muted">Drag a box's corners to resize it, its centre square to move it. Or pan the map to a place and use
     <i>Fit to view</i>.</p>
  <div class="row">
    <button id="fit">Fit to view</button>
    <label>Street-level zoom
      <select id="zoom"><option value="12">12 (coarser)</option><option value="13" selected>13</option><option value="14">14 (finer)</option></select>
    </label>
  </div>
  <table>
    <thead><tr><th>Zoom</th><th>Covers</th><th>Tiles</th><th>About</th></tr></thead>
    <tbody id="zooms"></tbody>
    <tfoot><tr><td colspan="3">Map file</td><td id="total"></td></tr></tfoot>
  </table>
  <p id="fetches" class="muted"></p>
  <div id="problems"></div>
  <div class="row">
    <button id="build" class="primary">Build Map</button>
    <button id="cancel" hidden>Stop</button>
  </div>
  <div id="progress" hidden>
    <p id="phase"></p>
    <progress id="bar"></progress>
    <div id="log"></div>
  </div>
  <div id="result" hidden>
    <p class="ok" id="built"></p>
    <div class="row"><a id="download" href="/api/download"><button>Download VisorMeshMap.pdb</button></a></div>
    <p class="muted">Previews:</p>
    <div class="previews" id="previews"></div>
    <p><b>Install it</b> (it replaces the map on the Visor):<br>
      macOS or Linux: <code>pilot-xfer -p usb: -i VisorMeshMap.pdb</code>, then press HotSync.<br>
      Windows: open it with Palm Desktop's Install Tool, then HotSync.</p>
  </div>
  <p class="muted" style="margin-top:auto">Map data © OpenStreetMap contributors (ODbL). Downloads use the public
    Overpass API and are cached, so each place is only fetched once.</p>
</div>
<script>
const $ = id => document.getElementById(id);
const css = getComputedStyle(document.documentElement);
const colour = name => css.getPropertyValue(name).trim();
const map = L.map('map', { worldCopyJump: false }).setView([39.95, -75.16], 9);
L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
  maxZoom: 18, attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
}).addTo(map);

// A box with two corner handles and a move handle. bounds: [[s, w], [n, e]]
function makeBox(bounds, col, onChange) {
  const rect = L.rectangle(bounds, { color: col, weight: 2, fillOpacity: 0.08 }).addTo(map);
  const icon = cls => L.divIcon({ className: '', html: '<div class="handle ' + cls + '" style="background:' + col + '"></div>',
                                  iconSize: [14, 14], iconAnchor: [7, 7] });
  const sw = L.marker(bounds[0], { draggable: true, icon: icon('') }).addTo(map);
  const ne = L.marker(bounds[1], { draggable: true, icon: icon('') }).addTo(map);
  const mid = L.marker(rect.getBounds().getCenter(), { draggable: true, icon: icon('move') }).addTo(map);
  const box = {
    get() { const b = rect.getBounds(); return [b.getSouth(), b.getWest(), b.getNorth(), b.getEast()]; },
    set(s, w, n, e) {
      const b = L.latLngBounds([Math.min(s, n), Math.min(w, e)], [Math.max(s, n), Math.max(w, e)]);
      rect.setBounds(b); sw.setLatLng(b.getSouthWest()); ne.setLatLng(b.getNorthEast()); mid.setLatLng(b.getCenter());
    },
  };
  const corner = () => { const a = sw.getLatLng(), b = ne.getLatLng(); box.set(a.lat, a.lng, b.lat, b.lng); onChange(box); };
  sw.on('drag', corner); ne.on('drag', corner);
  let start;
  mid.on('dragstart', () => { start = { c: mid.getLatLng(), b: box.get() }; moving = true; last = rect.getBounds().getCenter(); });
  mid.on('dragend', () => { moving = false; });
  mid.on('drag', () => {
    const c = mid.getLatLng(), dy = c.lat - start.c.lat, dx = c.lng - start.c.lng, b = start.b;
    box.set(b[0] + dy, b[1] + dx, b[2] + dy, b[3] + dx); onChange(box);
  });
  return box;
}

let area, detail, moving = false, last = null;   // last: the blue box's centre while it's moved
function keepInside() {        // the orange box stays inside the blue one
  const a = area.get(), d = detail.get();
  const s = Math.max(d[0], a[0]), w = Math.max(d[1], a[1]), n = Math.min(d[2], a[2]), e = Math.min(d[3], a[3]);
  if (s < n && w < e) { if (s !== d[0] || w !== d[1] || n !== d[2] || e !== d[3]) detail.set(s, w, n, e); }
  else { const lat = (a[0] + a[2]) / 2, lon = (a[1] + a[3]) / 2, h = (a[2] - a[0]) / 4, v = (a[3] - a[1]) / 4;
         detail.set(lat - h, lon - v, lat + h, lon + v); }
}
// moving the blue box takes the orange one along
area = makeBox([[39.70, -75.49], [40.20, -74.83]], colour('--area'), () => {
  const c = L.latLngBounds([[area.get()[0], area.get()[1]], [area.get()[2], area.get()[3]]]).getCenter();
  if (moving && last) { const d = detail.get(), dy = c.lat - last.lat, dx = c.lng - last.lng;
                        detail.set(d[0] + dy, d[1] + dx, d[2] + dy, d[3] + dx); }
  last = c; keepInside(); replan();
});
detail = makeBox([[39.82, -75.42], [40.10, -74.95]], colour('--detail'), () => { keepInside(); replan(); });
map.fitBounds([[39.70, -75.49], [40.20, -74.83]], { padding: [20, 20] });

$('fit').onclick = () => {
  const b = map.getBounds().pad(-0.1);
  const s = b.getSouth(), w = b.getWest(), n = b.getNorth(), e = b.getEast();
  area.set(s, w, n, e);
  const h = (n - s) / 4, v = (e - w) / 4;
  detail.set(s + h, w + v, n - h, e - v);
  replan();
};
$('zoom').onchange = () => replan();

function request() { return { area: area.get(), detail: detail.get(), maxZoom: +$('zoom').value }; }
async function post(path, body) {
  const r = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  return { ok: r.ok, data: await r.json() };
}
const kb = n => n >= 1024 ? (n / 1024).toFixed(1) + ' MB' : n + ' KB';

let timer, planOk = false;
function replan() { clearTimeout(timer); timer = setTimeout(doPlan, 150); }
async function doPlan() {
  const { ok, data } = await post('/api/plan', request());
  const probs = $('problems');
  probs.innerHTML = '';
  if (!ok) { planOk = false; probs.innerHTML = '<p class="problem"></p>'; probs.firstChild.textContent = data.error;
             $('zooms').innerHTML = ''; $('total').textContent = ''; $('fetches').textContent = ''; updateButtons(); return; }
  $('zooms').innerHTML = data.zooms.map(z =>
    '<tr><td>' + z.z + '</td><td>' + (z.detail ? 'street level' : 'map area') + '</td><td>' + z.cols + ' × ' + z.rows +
    '</td><td>' + kb(z.kb) + '</td></tr>').join('');
  $('total').textContent = 'about ' + kb(data.totalKb);
  const need = data.fetches - data.cached;
  $('fetches').textContent = need ? need + ' of ' + data.fetches + ' downloads still needed (roughly ' +
    Math.max(1, Math.round(need * 4 / 60)) + ' min or more).' : 'All the data is downloaded already.';
  for (const p of data.problems) { const el = document.createElement('p'); el.className = 'problem'; el.textContent = p; probs.appendChild(el); }
  planOk = data.problems.length === 0;
  updateButtons();
}

let running = false;
function updateButtons() { $('build').disabled = running || !planOk; $('cancel').hidden = !running; }
$('build').onclick = async () => {
  const { ok, data } = await post('/api/build', request());
  if (!ok) { alertProblem(data.error); return; }
  running = true; updateButtons(); $('result').hidden = true; $('progress').hidden = false; poll();
};
$('cancel').onclick = () => post('/api/cancel', {});
function alertProblem(msg) { const el = document.createElement('p'); el.className = 'problem'; el.textContent = msg; $('problems').appendChild(el); }

async function poll() {
  const s = await (await fetch('/api/status')).json();
  $('phase').textContent = s.phase + (s.total ? ' (' + s.done + ' of ' + s.total + ')' : '');
  if (s.total) { $('bar').max = s.total; $('bar').value = s.done; } else { $('bar').removeAttribute('value'); }
  const log = $('log'); log.textContent = s.log.join('\n'); log.scrollTop = log.scrollHeight;
  if (s.running) { setTimeout(poll, 1000); return; }
  running = false; updateButtons();
  if (s.error) { alertProblem(s.error); }
  if (s.result) {
    $('progress').hidden = false; $('result').hidden = false;
    $('built').textContent = 'Built: ' + kb(s.result.kb) + ', saved as ' + s.result.file;
    $('previews').innerHTML = s.result.previews.map(p => '<a target="_blank" href="/api/preview/' + p + '">zoom ' + p.slice(1, -4) + '</a>').join('');
  }
  doPlan();
}
fetch('/api/status').then(r => r.json()).then(s => { if (s.running) { running = true; $('progress').hidden = false; updateButtons(); poll(); } });
doPlan();
</script>
</body>
</html>
'''

if __name__ == '__main__':
    sys.exit(main())
