// web/app.js against a fake device (spec §10.3, §10.4). Node's own test runner, no packages:
// `node --test test/web/test_app.mjs`; ctest runs it too (test/host/CMakeLists.txt).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const APP_JS = fileURLToPath(new URL('../../web/app.js', import.meta.url));

/* ---- a DOM just big enough for app.js ---- */

class FakeNode {}

class FakeElement extends FakeNode {
  constructor(tag) {
    super();
    Object.assign(this, { tag, children: [], attrs: {}, listeners: {}, className: '', textContent: '', value: '',
                          hidden: false, disabled: false, checked: false, style: {} });
    this.classList = { toggle() {}, add() {}, remove() {} };
  }
  append(...kids) { this.children.push(...kids); }
  replaceChildren(...kids) { this.children = kids; }
  remove() {}
  focus() {}
  setAttribute(k, v) { this.attrs[k] = v; }
  getAttribute(k) { return this.attrs[k]; }
  get href() { return this.attrs.href; }
  addEventListener(type, fn) { (this.listeners[type] ||= []).push(fn); }
  click() { return Promise.all((this.listeners.click || []).map((fn) => fn({ preventDefault() {} }))); }
  querySelectorAll(selector) { return below(this).filter((e) => e.tag === selector); }
}

/* `el` and every element under it. */
function below(el) {
  return [el, ...el.children.filter((k) => k instanceof FakeElement).flatMap(below)];
}

/* The text of an element and everything under it. */
function text(el) {
  return typeof el === 'string' ? el : el.textContent + el.children.map(text).join('');
}

function buttonNamed(root, name) {
  const b = below(root).find((e) => e.tag === 'button' && text(e) === name);
  assert.ok(b, `no button "${name}"`);
  return b;
}

/* ---- the device ---- */

function reply(status, body, type = 'application/json') {
  return {
    status, ok: status >= 200 && status < 300, statusText: String(status),
    headers: { get: (k) => (k.toLowerCase() === 'content-type' ? type : null) },
    json: async () => body,
    blob: async () => new Blob([typeof body === 'string' ? body : JSON.stringify(body)], { type }),
  };
}

/* The server's own rule (components/webui/webui.c): a request that changes something, other than a
 * firmware upload, must say it is JSON, which a cross-site form cannot. */
function device(routes) {
  return async (path, init = {}) => {
    const method = init.method || 'GET';
    const type = (init.headers || {})['Content-Type'] || '';
    if (method !== 'GET' && path !== '/api/ota' && !type.startsWith('application/json')) {
      return reply(415, { error: 'send application/json' });
    }
    const route = routes[`${method} ${path.split('?')[0]}`];
    return route ? route(init) : reply(404, { error: 'no such API' });
  };
}

/* Loads app.js with the device answering `routes`. */
async function load(routes = {}, opts = {}) {
  const calls = [], blobs = [], byId = {};
  const document = {
    getElementById: (id) => (byId[id] ||= new FakeElement('div')),
    createElement: (tag) => new FakeElement(tag),
    querySelectorAll: () => [],
    body: new FakeElement('body'),
  };
  const url = {
    createObjectURL(obj) {
      if (!(obj instanceof Blob)) throw new TypeError('createObjectURL needs a Blob'); /* as browsers do */
      blobs.push(obj);
      return `blob:${blobs.length}`;
    },
    revokeObjectURL() {},
  };
  const answer = device({
    'GET /api/auth': () => reply(200, { password_set: true, logged_in: false, on_ap: true, host: 'reflbo-bb94',
                                        ap_ssid: 'reflbo-bb94', version: '0.4.0' }),
    ...routes,
  });
  const ctx = vm.createContext({
    document, Node: FakeNode, Blob, TextEncoder, Intl, URL: url, console,
    fetch: (path, init) => { calls.push({ path, init }); return answer(path, init); },
    setTimeout: () => 0, clearTimeout() {}, confirm: opts.confirm || (() => true),
    location: { hash: '', reload() {} }, window: { scrollTo() {}, scrollY: 0 }, ZONES: {},
  });
  vm.runInContext(fs.readFileSync(APP_JS, 'utf8'), ctx, { filename: 'app.js' });
  await settle();
  return { ctx, calls, blobs, main: byId.main, byId };
}

/* Lets the fake device's answers run through their promise chains. */
async function settle() {
  for (let i = 0; i < 10; i++) await new Promise((r) => setImmediate(r));
}

/* ---- the tests ---- */

test('requests without a body still say they are JSON', async () => {
  const ok = () => reply(200, { ok: true });
  const { ctx, calls } = await load({
    'POST /api/done': ok, 'POST /api/auth/logout': ok, 'POST /api/reboot': ok, 'POST /api/factory-reset': ok,
    'DELETE /api/wifi/networks': ok,
  });
  for (const [method, path] of [['POST', '/api/done'], ['POST', '/api/auth/logout'], ['POST', '/api/reboot'],
                                ['POST', '/api/factory-reset'], ['DELETE', '/api/wifi/networks?ssid=Home']]) {
    await ctx.api(method, path);
    assert.equal(calls.at(-1).init.headers['Content-Type'], 'application/json', `${method} ${path}`);
  }
});

test('Done turns Wi-Fi off', async () => {
  const { byId } = await load({ 'POST /api/done': () => reply(200, { ok: true }) });
  await byId.done.onclick();
  assert.match(text(byId.main), /Wi-Fi is off/);
});

test('Download backup saves the backup as a JSON file', async () => {
  const backup = { reflbo_backup: 1, device: 'reflbo-bb94', firmware: '0.4.0',
                   files: { 'settings.json': { schema: 1, language: 'cs' } } };
  const { ctx, blobs, main } = await load({ 'GET /api/backup': () => reply(200, backup) });
  await ctx.backupPage();
  await buttonNamed(main, 'Download backup').click();
  assert.equal(blobs.length, 1);
  assert.deepEqual(JSON.parse(await blobs[0].text()), backup);
});

test('Factory reset erases and says so', async () => {
  const { ctx, calls, main } = await load({ 'POST /api/factory-reset': () => reply(200, { ok: true }) });
  await ctx.backupPage();
  await buttonNamed(main, 'Factory reset…').click();
  assert.equal(calls.at(-1).path, '/api/factory-reset');
  assert.match(text(main), /Erased/);
});

/* ---- the preset editor ---- */

const CATALOGUE = {
  width: 400, height: 300, status_h: 20,
  layouts: [{ id: 'classic', slots: [
    { id: 'main', x: 0, y: 21, w: 400, h: 150, size: 'XL', kinds: ['time'] },
    { id: 's1', x: 0, y: 172, w: 200, h: 128, size: 'S', kinds: ['number'] },
  ] }],
};
const FIELDS = { fields: [{ id: 'time.clock', kind: 'time', name: 'Time', value: '12:15' },
                          { id: 'env.temp', kind: 'number', name: 'Temperature', value: '23.7 °C' }] };
const preset = (id, name, inCycle) => ({ id, name, layout: 'classic', in_cycle: inCycle,
                                         slots: { main: 'time.clock', s1: 'env.temp' }, options: {} });

/* A device with presets: Home in the cycle, Weather out of it; PUTs are kept in `saved`. */
function presetDevice(saved) {
  const doc = { schema: 1, active: 'weather', presets: [preset('home', 'Home', true), preset('weather', 'Weather', false)],
                cycle: { enabled: false, interval_s: 60 }, schedule: { enabled: false, entries: [] } };
  return {
    'GET /api/layouts': () => reply(200, CATALOGUE),
    'GET /api/presets': () => reply(200, JSON.parse(JSON.stringify(doc))),
    'GET /api/fields': () => reply(200, FIELDS),
    'POST /api/preview.bmp': () => reply(200, 'BM', 'image/bmp'),
    'PUT /api/presets': (init) => { saved.push(JSON.parse(init.body)); return reply(200, JSON.parse(init.body)); },
  };
}

test('a new preset joins the cycle, even as a copy of one outside it', async () => {
  const saved = [];
  const { ctx, main } = await load(presetDevice(saved));
  await ctx.presetsPage(); /* Weather, the active one, is selected */
  await buttonNamed(main, 'New preset').click();
  await buttonNamed(main, 'Save').click();
  await settle();
  const added = saved.at(-1).presets.find((p) => p.name.startsWith('Weather copy'));
  assert.ok(added, 'the copy was saved');
  assert.equal(added.in_cycle, true);
});

test('Undo changes asks before it drops the edits', async () => {
  const saved = [];
  let asked = 0;
  const { ctx, calls, main } = await load(presetDevice(saved), { confirm: () => { asked++; return false; } });
  await ctx.presetsPage();
  await buttonNamed(main, 'New preset').click();
  const before = calls.filter((c) => c.path === '/api/presets').length;
  await buttonNamed(main, 'Undo changes').click();
  await settle();
  assert.equal(asked, 1);
  assert.equal(calls.filter((c) => c.path === '/api/presets').length, before, 'nothing reloaded');
  assert.match(text(main), /Weather copy/);
});

test('the preview names each slot where the layout puts it', async () => {
  const { ctx, main } = await load(presetDevice([]));
  await ctx.presetsPage();
  const tags = below(main).filter((e) => e.className.split(' ').includes('slot-tag'));
  assert.deepEqual(tags.map(text), ['main', 's1']);
  assert.deepEqual(tags.map((e) => [e.style.left, e.style.top]), [['100%', '7%'], ['50%', '57.333%']]); /* top right */
});

/* ---- the Device page's battery calibration (owner, 2026-09-30) ---- */

/* The control a field() label introduces: the element after it. */
function control(root, label) {
  for (const el of below(root)) {
    const i = el.children.findIndex((k) => k instanceof FakeElement && k.tag === 'label' && text(k) === label);
    if (i >= 0) return el.children[i + 1];
  }
  assert.fail(`no field "${label}"`);
}

function settingsDevice(patches, battery = {}, learn = { state: 'off', hours: 0 }, learnCalls = []) {
  const settings = { schema: 1, language: 'en', units: { temp: 'C' },
                     sensors: { interval_min: 5, temp_offset_c: -2, hum_offset_pct: 0 },
                     display: { update_min: 1, lpm_hz: 1 },
                     battery: { level_from: 'curve', empty_v: 3.27, full_v: 4.2, ...battery } };
  return {
    'GET /api/settings': () => reply(200, settings),
    'GET /api/status': () => reply(200, { battery: { percent: 82, mv: 4050, learn } }),
    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, settings); },
    'POST /api/battery/learn': (init) => { learnCalls.push(JSON.parse(init.body)); return reply(200, { state: 'waiting' }); },
  };
}

const options = (sel) => sel.children.filter((k) => k instanceof FakeElement).map((o) => o.value);

test('the Device page saves your own battery voltages', async () => {
  const patches = [];
  const { ctx, main } = await load(settingsDevice(patches));
  await ctx.devicePage();
  control(main, 'Battery level from').value = 'manual';
  control(main, 'Full, V').value = '4.12';
  control(main, 'Empty, V').value = '3.45';
  await buttonNamed(main, 'Save').click();
  await settle();
  assert.deepEqual(patches.at(-1).battery, { level_from: 'manual', empty_v: 3.45, full_v: 4.12 });
});

test('the Device page wants room between the battery voltages', async () => {
  const patches = [];
  const { ctx, main } = await load(settingsDevice(patches));
  await ctx.devicePage();
  control(main, 'Battery level from').value = 'manual';
  control(main, 'Full, V').value = '4.0';
  control(main, 'Empty, V').value = '3.9';
  await buttonNamed(main, 'Save').click();
  await settle();
  assert.equal(patches.length, 0);
  assert.match(text(main), /0\.3 V/);
});

/* ---- learning the battery curve from a full discharge (D21) ---- */

test('the learned curve is on offer once there is one', async () => {
  let page = await load(settingsDevice([]));
  await page.ctx.devicePage();
  assert.deepEqual(options(control(page.main, 'Battery level from')), ['curve', 'manual']);
  const curve = Array.from({ length: 21 }, (_, i) => 3300 + 45 * i);
  page = await load(settingsDevice([], { level_from: 'learned', learned_mv: curve, learned_at: 1790786009 }));
  await page.ctx.devicePage();
  assert.deepEqual(options(control(page.main, 'Battery level from')), ['curve', 'manual', 'learned']);
});

test('Learn from the next full discharge arms it', async () => {
  const learnCalls = [];
  const { ctx, main } = await load(settingsDevice([], {}, { state: 'off', hours: 0 }, learnCalls));
  await ctx.devicePage();
  await buttonNamed(main, 'Learn from the next full discharge').click();
  await settle();
  assert.deepEqual(learnCalls, [{ start: true }]);
});

test('a discharge being learned shows its hours and can be stopped', async () => {
  const learnCalls = [];
  const { ctx, main } = await load(settingsDevice([], {}, { state: 'recording', hours: 42 }, learnCalls));
  await ctx.devicePage();
  assert.match(text(main), /42 h/);
  await buttonNamed(main, 'Stop learning').click();
  await settle();
  assert.deepEqual(learnCalls, [{ stop: true }]);
});

/* ---- sync (spec §9.3, D25) ---- */

const SYNC_SETTINGS = { schema: 1, sync: { mode: 'times', times: ['05:30'], interval_min: 60,
                                           quiet: { enabled: false, from: '23:00', to: '06:00' } } };
const syncStatus = (sync) => ({ device: {}, time: { valid: true, rtc: { trim_steps: -9, drift_s_per_day: -0.2 } },
                                battery: {}, sensors: {}, preset: {}, sync,
                                wifi: { state: sync.mode === 'always' ? 'station' : 'off', ap_on: false } });

test('the Sync page saves the mode, the times and the quiet hours', async () => {
  const patches = [];
  const { ctx, main } = await load({
    'GET /api/settings': () => reply(200, SYNC_SETTINGS),
    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false })),
    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, SYNC_SETTINGS); },
  });
  await ctx.syncPage();
  const inputs = below(main).filter((e) => e.tag === 'input');
  const time = inputs.find((e) => e.attrs.type === 'time' && e.value === '05:30');
  time.value = '04:45';
  await Promise.all((time.listeners.change || []).map((fn) => fn({ target: time })));
  inputs.find((e) => e.attrs.type === 'checkbox').checked = true; /* quiet hours 23:00-06:00 */
  await buttonNamed(main, 'Save').click();
  assert.deepEqual(patches.at(-1), { sync: { mode: 'times', times: ['04:45'], interval_min: 60,
                                             quiet: { enabled: true, from: '23:00', to: '06:00' } } });
  assert.match(text(main), /04:45 falls in the quiet hours: it runs at 06:00/);
});

test('the Balanced shortcut syncs every hour', async () => {
  const patches = [];
  const { ctx, main } = await load({
    'GET /api/settings': () => reply(200, SYNC_SETTINGS),
    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false })),
    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, SYNC_SETTINGS); },
  });
  await ctx.syncPage();
  await buttonNamed(main, 'Balanced').click();
  await buttonNamed(main, 'Save').click();
  assert.equal(patches.at(-1).sync.mode, 'interval');
  assert.equal(patches.at(-1).sync.interval_min, 60);
});

test('Sync now follows the sync to its end', async () => {
  let polls = 0;
  const done = { mode: 'times', running: false, last: { at: 1790859600, failed: 'weather', detail: 'HTTP 503',
                 steps: { wifi: 'ok', time: 'ok', weather: 'failed', air: 'ok' } } };
  const { ctx, calls, main } = await load({
    'GET /api/settings': () => reply(200, SYNC_SETTINGS),
    'GET /api/status': () => reply(200, syncStatus(++polls < 3 ? { mode: 'times', running: true, step: 'weather' } : done)),
    'POST /api/sync': () => reply(202, { started: true }),
  });
  ctx.setTimeout = (fn) => { fn(); return 0; }; /* sleep() returns at once */
  await ctx.syncPage();
  await buttonNamed(main, 'Sync now').click();
  await settle();
  assert.ok(calls.some((c) => c.path === '/api/sync' && c.init.method === 'POST'));
  assert.match(text(main), /The sync failed/);
  assert.match(text(main), /failed: HTTP 503/);
});

test('the place search fills in the name and the coordinates', async () => {
  const { ctx, calls, main } = await load({
    'GET /api/settings': () => reply(200, { schema: 1, location: { name: 'Brno', lat: 49.1951, lon: 16.6068 }, time: {} }),
    'GET /api/geocode': () => reply(200, { places: [{ name: 'Olomouc', region: 'Olomoucký', country: 'CZ', lat: 49.5938,
                                                      lon: 17.2509, timezone: 'Europe/Prague' }] }),
  });
  await ctx.placePage();
  const query = below(main).find((e) => e.tag === 'input' && e.attrs.type === 'search' && e.attrs.placeholder === 'A town or city');
  query.value = 'Olomouc';
  await buttonNamed(main, 'Search').click();
  assert.equal(calls.at(-1).path, '/api/geocode?q=Olomouc');
  await buttonNamed(main, 'Olomouc, Olomoucký, CZ').click();
  const values = below(main).filter((e) => e.tag === 'input').map((e) => e.value);
  assert.ok(values.includes('Olomouc') && values.includes('49.5938') && values.includes('17.2509'), values.join(' '));
});

test('Done keeps the page when sync mode Always on keeps the network', async () => {
  const { byId } = await load({
    'GET /api/status': () => reply(200, syncStatus({ mode: 'always', running: false })),
    'POST /api/done': () => reply(200, { ok: true }),
  });
  await byId.done.onclick();
  assert.doesNotMatch(text(byId.main), /Wi-Fi is off/);
});

/* ---- the Radar page (spec §10.3, M6) ---- */

const RADAR_SETTINGS = { schema: 1, location: { name: 'Brno', lat: 49.1951, lon: 16.6068 }, sync: { mode: 'times' },
                         radar: { weather: { zoom: 6.5 }, flights: { range_km: 50, min_alt_ft: 0, ground: false, max: 100 } } };
const radarStatus = (radar) => ({ device: {}, time: { valid: true }, battery: {}, sensors: {}, preset: {}, sync: {}, radar });

function radarDevice(settings, patches, previews) {
  return {
    'GET /api/settings': () => reply(200, settings),
    'GET /api/status': () => reply(200, radarStatus({ weather: { source: 'chmu', frames: 1, frame_at: 1790880000 },
                                                      flights: { on: false, aircraft: 0, failed: false } })),
    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, settings); },
    'POST /api/preview.bmp': (init) => { previews.push(JSON.parse(init.body)); return reply(200, 'BM', 'image/bmp'); },
  };
}

/* The radio buttons of a radar's card: a browser unchecks the other of a group by itself. */
function pick(main, group, label) {
  const radios = below(main).filter((e) => e.tag === 'input' && e.attrs.type === 'radio' && e.attrs.name === group);
  const labels = below(main).filter((e) => e.tag === 'label' && e.children.some((k) => radios.includes(k)));
  for (const l of labels) {
    const r = l.children.find((k) => radios.includes(k));
    r.checked = text(l).startsWith(label);
    if (r.checked) (r.listeners.change || []).forEach((fn) => fn({ target: r }));
  }
}

test('the Radar page saves the weather radar\'s own centre and zoom', async () => {
  const patches = [], previews = [];
  const { ctx, main } = await load(radarDevice(RADAR_SETTINGS, patches, previews));
  await ctx.radarPage();
  pick(main, 'wx-centre', 'A place of its own');
  const numbers = below(main).filter((e) => e.tag === 'input' && e.attrs.type === 'number');
  numbers[0].value = '50.0755'; /* the weather card's latitude and longitude come first */
  numbers[1].value = '14.4378';
  const zoom = below(main).find((e) => e.tag === 'select' && e.attrs.name === 'zoom');
  zoom.value = '7.25';
  await buttonNamed(main, 'Save weather radar').click();
  assert.deepEqual(patches.at(-1), { radar: { weather: { lat: 50.0755, lon: 14.4378, zoom: 7.25 } } });
});

test('the location again clears a radar\'s own centre', async () => {
  const patches = [], previews = [];
  const settings = { ...RADAR_SETTINGS, radar: { weather: { lat: 50.0755, lon: 14.4378, zoom: 7 }, flights: {} } };
  const { ctx, main } = await load(radarDevice(settings, patches, previews));
  await ctx.radarPage();
  pick(main, 'wx-centre', 'The location');
  await buttonNamed(main, 'Save weather radar').click();
  assert.deepEqual(patches.at(-1), { radar: { weather: { lat: null, lon: null, zoom: 7 } } }); /* RFC 7396 removes them */
});

test('the flight radar\'s filters are saved within their bounds', async () => {
  const patches = [], previews = [];
  const { ctx, main } = await load(radarDevice(RADAR_SETTINGS, patches, previews));
  await ctx.radarPage();
  const range = below(main).find((e) => e.tag === 'select' && e.attrs.name === 'range');
  range.value = '25';
  const byName = (n) => below(main).find((e) => e.tag === 'input' && e.attrs.name === n);
  byName('min_alt_ft').value = '3000';
  byName('ground').checked = true;
  byName('max').value = '0';
  await buttonNamed(main, 'Save flight radar').click();
  assert.equal(patches.length, 0);
  assert.match(text(main), /Aircraft at most: 1 to 100/);
  byName('max').value = '40';
  await buttonNamed(main, 'Save flight radar').click();
  assert.deepEqual(patches.at(-1), { radar: { flights: { lat: null, lon: null, range_km: 25, min_alt_ft: 3000,
                                                         ground: true, max: 40 } } });
});

test('the flight radar says it runs only in sync mode Always on', async () => {
  const { ctx, main } = await load(radarDevice(RADAR_SETTINGS, [], []));
  await ctx.radarPage();
  assert.match(text(main), /runs only in sync mode Always on/);
  const always = { ...RADAR_SETTINGS, sync: { mode: 'always' } };
  const again = await load(radarDevice(always, [], []));
  await again.ctx.radarPage();
  assert.doesNotMatch(text(again.main), /runs only in sync mode Always on/);
});

test('the flight radar says why its last poll failed and when routes come back', async () => {
  const device = radarDevice({ ...RADAR_SETTINGS, sync: { mode: 'always' } }, [], []);
  device['GET /api/status'] = () => reply(200, radarStatus({ weather: { source: 'chmu', frames: 1, frame_at: 1790880000 },
    flights: { on: true, aircraft: 0, failed: true, error: 'HTTP 429', routes_paused_until: 1790883600 } }));
  const { ctx, main } = await load(device);
  await ctx.radarPage();
  assert.match(text(main), /Last pollfailed: HTTP 429/);
  assert.match(text(main), /Routespaused by adsb\.lol until /);
});

test('the Radar page previews both radars and credits their sources', async () => {
  const previews = [];
  const { ctx, main } = await load(radarDevice(RADAR_SETTINGS, [], previews));
  await ctx.radarPage();
  await settle();
  assert.deepEqual(previews.map((d) => d.presets[0].layout).sort(), ['flights', 'radar']);
  const all = text(main);
  for (const credit of ['ČHMÚ', 'CC BY 4.0', 'RainViewer', 'adsb.fi', 'adsb.lol', 'GeoNames']) assert.ok(all.includes(credit), credit);
  assert.match(all, /Newest frame/);
});

test('the Sync page shows the radar\'s step', async () => {
  const last = { at: 1790880000, steps: { wifi: 'ok', time: 'ok', weather: 'ok', air: 'ok', radar: 'failed' },
                 failed: 'radar', detail: 'HTTP 503' };
  const { ctx, main } = await load({
    'GET /api/settings': () => reply(200, SYNC_SETTINGS),
    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false, last })),
  });
  await ctx.syncPage();
  assert.match(text(main), /Radar failed \(HTTP 503\)/);
  assert.match(text(main), /Radarfailed: HTTP 503/); /* the steps' list: its name, then its result */
});

test('a preset on a radar layout has no slots to fill', async () => {
  const catalogue = { ...CATALOGUE, layouts: [...CATALOGUE.layouts, { id: 'radar', slots: [] }, { id: 'flights', slots: [] }] };
  const doc = { schema: 1, active: 'rain', presets: [{ id: 'rain', name: 'Rain radar', layout: 'radar', in_cycle: true,
                                                         slots: {}, options: {} }],
                cycle: { enabled: false, interval_s: 60 }, schedule: { enabled: false, entries: [] } };
  const { ctx, main } = await load({
    'GET /api/layouts': () => reply(200, catalogue), 'GET /api/presets': () => reply(200, doc),
    'GET /api/fields': () => reply(200, FIELDS), 'POST /api/preview.bmp': () => reply(200, 'BM', 'image/bmp'),
  });
  await ctx.presetsPage();
  assert.match(text(main), /Rain radar Radar/); /* the list names its layout */
  assert.match(text(main), /This layout draws the weather radar/);
});

/* ---- the split layout's editor (M6b, spec §5.2, §10.3, D31) ---- */

/* The device's rules (GET /api/layouts, ui_catalog.c) for the kinds these tests use. */
const SPLIT = {
  x: 0, y: 21, w: 400, h: 279, cells: 24, min_w: 40, min_h: 20, narrow_w: 150, inset: 8, /* M6c, D34 */
  ratios: ['1/4', '1/3', '1/2', '2/3', '3/4'],
  sizes: [
    { size: 'XL', min_w: 400, min_h: [120, 120], kinds: { time: [120, 120], number: [120, 120] } },
    { size: 'L', min_w: 200, min_h: [150, 150], kinds: { time: [150, 150], number: [150, 150] } },
    { size: 'M', min_w: 130, min_h: [80, 80], kinds: { time: [80, 80], number: [80, 80], series: [80, 80] } },
    { size: 'S', min_w: 90, min_h: [40, 40], kinds: { time: [40, 40], number: [40, 40] } },
    { size: 'XS', min_w: 40, min_h: [20, 20], kinds: { time: [20, 20], number: [20, 20] } },
  ],
};
const SPLIT_CATALOGUE = { ...CATALOGUE, layouts: [...CATALOGUE.layouts, { id: 'split', slots: [] }], split: SPLIT };
const SPLIT_FIELDS = { fields: [...FIELDS.fields, { id: 'wx.hourly', kind: 'series', label: 'Next hours', value: '' }] };

/* A device with one preset, Home: Classic, or on the split layout with `tree`. */
function splitDevice(saved, tree) {
  const home = tree ? { id: 'home', name: 'Home', layout: 'split', in_cycle: true, split: tree, options: {} }
                    : preset('home', 'Home', true);
  const doc = { schema: 1, active: 'home', presets: [home], cycle: { enabled: false, interval_s: 60 },
                schedule: { enabled: false, entries: [] } };
  return {
    'GET /api/layouts': () => reply(200, SPLIT_CATALOGUE),
    'GET /api/presets': () => reply(200, JSON.parse(JSON.stringify(doc))),
    'GET /api/fields': () => reply(200, SPLIT_FIELDS),
    'POST /api/preview.bmp': () => reply(200, 'BM', 'image/bmp'),
    'PUT /api/presets': (init) => { saved.push(JSON.parse(init.body)); return reply(200, JSON.parse(init.body)); },
  };
}

async function change(el, value) {
  if (el.attrs.type === 'checkbox') el.checked = value;
  else el.value = value;
  await Promise.all((el.listeners.change || []).map((fn) => fn({ target: el })));
}

const ratioSelects = (root) => below(root).filter((e) => e.tag === 'select' && options(e).includes('1/4'));
const cellLabels = (root) => below(root).filter((e) => e.className === 'cell-name').map(text);

async function savedTree(main, saved) {
  await buttonNamed(main, 'Save').click();
  await settle();
  return saved.at(-1).presets[0];
}

test('switching a preset to Split starts from one cell holding its first field', async () => {
  const saved = [];
  const { ctx, main } = await load(splitDevice(saved));
  await ctx.presetsPage();
  await change(control(main, 'Layout'), 'split');
  assert.deepEqual(cellLabels(main), ['1 · 400×279 · XL']);
  const p = await savedTree(main, saved);
  assert.deepEqual(p.split, { field: 'time.clock' });
  assert.equal(p.slots, undefined, 'a split preset has its tree instead of slots');
});

test('Split into rows halves a cell, its field going to the first part', async () => {
  const saved = [];
  const { ctx, main } = await load(splitDevice(saved, { field: 'time.clock' }));
  await ctx.presetsPage();
  await buttonNamed(main, 'Split into rows').click();
  assert.deepEqual(cellLabels(main), ['1 · 400×139 · XL', '2 · 400×139 · XL']); /* XL from 120 px */
  const tags = below(main).filter((e) => e.className.split(' ').includes('slot-tag'));
  assert.deepEqual(tags.map((e) => [text(e), e.style.left, e.style.top]),
                   [['1', '100%', '7%'], ['2', '100%', '53.667%']]); /* each cell's number at its top right */
  const p = await savedTree(main, saved);
  assert.deepEqual(p.split, { split: 'rows', ratio: '1/2', line: true, a: { field: 'time.clock' }, b: {} });
});

test('a split offers only the ratios that leave every part 40×20', async () => {
  const { ctx, main } = await load(splitDevice([], { split: 'rows', ratio: '1/4', line: true,
                                                     a: { split: 'rows', ratio: '1/2', line: true, a: {}, b: {} },
                                                     b: {} }));
  await ctx.presetsPage();
  const [outer, inner] = ratioSelects(main);
  const disabled = (sel) => sel.children.filter((o) => o.disabled).map((o) => o.value);
  assert.deepEqual(disabled(outer), []);             /* a quarter, 69 px, halves into 34 */
  assert.deepEqual(disabled(inner), ['1/4', '3/4']); /* 69 px: 17 at a quarter */
  const splitButtons = below(main).filter((e) => e.tag === 'button' && /^Split into/.test(text(e)));
  assert.deepEqual(splitButtons.map((b) => b.disabled), [true, false, true, false, false, false]); /* 400×34 */
});

/* Eight columns of halves, a row of cells 50 or 49 px wide (M6c). */
const eightColumns = () => {
  const half = (a, b) => ({ split: 'columns', ratio: '1/2', line: true, a, b });
  const four = () => half(half({}, {}), half({}, {}));
  return half(four(), four());
};

test('a cell under 90 px wide is XS', async () => {
  const { ctx, main } = await load(splitDevice([], { split: 'columns', ratio: '1/4', line: true,
    a: { split: 'columns', ratio: '1/2', line: true, a: { field: 'env.temp' }, b: {} }, b: {} }));
  await ctx.presetsPage();
  assert.deepEqual(cellLabels(main), ['1 · 50×279 · XS', '2 · 49×279 · XS', '3 · 299×279 · L']);
});

test('a tree of 24 cells splits no further', async () => {
  const rows = { split: 'rows', ratio: '1/3', line: true, a: eightColumns(),
                 b: { split: 'rows', ratio: '1/2', line: true, a: eightColumns(), b: eightColumns() } };
  const { ctx, main } = await load(splitDevice([], rows));
  await ctx.presetsPage();
  assert.equal(cellLabels(main).length, 24);
  assert.equal(cellLabels(main)[23], '24 · 49×92 · XS');
  const splitButtons = below(main).filter((e) => e.tag === 'button' && /^Split into/.test(text(e)));
  assert.equal(splitButtons.length, 48);
  assert.ok(splitButtons.every((b) => b.disabled));
});

test('a cell offers only the fields that fit it', async () => {
  const { ctx, main } = await load(splitDevice([], { split: 'rows', ratio: '3/4', line: true, a: {}, b: {} }));
  await ctx.presetsPage();
  const fieldSelects = below(main).filter((e) => e.tag === 'select' && options(e).includes(''));
  assert.deepEqual(options(fieldSelects[0]), ['', 'time.clock', 'env.temp', 'wx.hourly']); /* 400×209 */
  assert.deepEqual(options(fieldSelects[1]), ['', 'time.clock', 'env.temp']); /* 400×69: no series */
});

test('Join keeps the first field found inside the split', async () => {
  const saved = [];
  const { ctx, main } = await load(splitDevice(saved, { split: 'columns', ratio: '1/2', line: true, a: {},
    b: { split: 'rows', ratio: '1/2', line: true, a: { field: 'env.temp' }, b: { field: 'time.clock' } } }));
  await ctx.presetsPage();
  await buttonNamed(main, 'Join').click(); /* the outer split's, the first */
  const p = await savedTree(main, saved);
  assert.deepEqual(p.split, { field: 'env.temp' });
});

test('each split keeps its own separator', async () => {
  const saved = [];
  const { ctx, main } = await load(splitDevice(saved, { split: 'rows', ratio: '1/2', line: true, a: {},
                                                        b: { split: 'columns', ratio: '1/2', line: true, a: {}, b: {} } }));
  await ctx.presetsPage();
  const boxes = below(main).filter((e) => e.tag === 'label' && text(e) === 'Separator').map((l) => l.children[0]);
  assert.equal(boxes.length, 2);
  await change(boxes[1], false);
  const p = await savedTree(main, saved);
  assert.equal(p.split.line, true);
  assert.equal(p.split.b.line, false);
});

test('a ratio that leaves a field without room empties its cell, and says so', async () => {
  const saved = [];
  const { ctx, main, byId } = await load(splitDevice(saved, { split: 'rows', ratio: '1/2', line: true, a: {},
                                                              b: { field: 'wx.hourly' } }));
  await ctx.presetsPage();
  await change(ratioSelects(main)[0], '3/4'); /* the bottom cell: 400×69, too low for a series */
  assert.equal(text(byId.toast), 'No room for Next hours');
  const p = await savedTree(main, saved);
  assert.deepEqual(p.split.b, {});
});

test('a split that leaves no room for the cell\'s field says so', async () => {
  const saved = [];
  const { ctx, main, byId } = await load(splitDevice(saved, { split: 'rows', ratio: '1/2', line: true,
                                                              a: { field: 'wx.hourly' }, b: {} }));
  await ctx.presetsPage();
  await buttonNamed(main, 'Split into rows').click(); /* the first cell's: 400×69 halves, too low for a series */
  assert.equal(text(byId.toast), 'No room for Next hours');
  const p = await savedTree(main, saved);
  assert.deepEqual(p.split.a, { split: 'rows', ratio: '1/2', line: true, a: {}, b: {} });
});

test('a cell says when its field draws at a smaller size than the cell\'s', async () => {
  const { ctx, main } = await load(splitDevice([], { split: 'rows', ratio: '3/4', line: true,
                                                     a: { field: 'wx.hourly' }, b: { field: 'env.temp' } }));
  await ctx.presetsPage();
  assert.deepEqual(cellLabels(main), ['1 · 400×209 · XL (Next hours at M)', '2 · 400×69 · S']);
});

/* ---- M6d: the steps' switches, the Solar page, the editor's Solar and Energy fields ---- */

const STEP_SETTINGS = { ...SYNC_SETTINGS, sync: { ...SYNC_SETTINGS.sync,
                                                  steps: ['weather', 'air', 'radar', 'solar', 'energy'] } };

test('the Sync page switches the data steps on and off, the time always on', async () => {
  const patches = [];
  const { ctx, main } = await load({
    'GET /api/settings': () => reply(200, STEP_SETTINGS),
    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false })),
    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, STEP_SETTINGS); },
  });
  await ctx.syncPage();
  const box = (name) => below(main).find((e) => e.tag === 'label' && text(e) === name).children[0];
  assert.equal(box('Radar').checked, true);
  box('Radar').checked = false;
  box('House energy').checked = false;
  await buttonNamed(main, 'Save steps').click();
  assert.deepEqual(patches.at(-1), { sync: { steps: ['weather', 'air', 'solar'] } });
  assert.match(text(main), /The time always runs/);
});

test('the Sync page shows the Solar and Energy steps, a kept one as kept', async () => {
  const last = { at: 1790880000, failed: 'energy', detail: 'tokenId is invalid',
                 steps: { wifi: 'ok', time: 'ok', weather: 'ok', air: 'ok', radar: 'ok', solar: 'kept', energy: 'failed' } };
  const { ctx, main } = await load({
    'GET /api/settings': () => reply(200, STEP_SETTINGS),
    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false, last })),
  });
  await ctx.syncPage();
  assert.match(text(main), /Solar forecastkept/);
  assert.match(text(main), /House energyfailed: tokenId is invalid/);
});

test('the Sync page says why a step was kept or skipped', async () => {
  const last = { at: 1790880000, steps: { wifi: 'ok', time: 'ok', weather: 'skipped', air: 'ok', radar: 'ok',
                                          solar: 'kept', energy: 'skipped' },
                 details: { weather: 'off', solar: 'HTTP 429', energy: 'no source' } };
  const { ctx, main } = await load({
    'GET /api/settings': () => reply(200, STEP_SETTINGS),
    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false, last })),
  });
  await ctx.syncPage();
  assert.match(text(main), /Weatherskipped: off/);
  assert.match(text(main), /Solar forecastkept: HTTP 429/);
  assert.match(text(main), /House energyskipped: no source/);
});

const SOLAR_SETTINGS = { schema: 1, location: { name: 'Brno', lat: 49.1951, lon: 16.6068 },
                         sync: { steps: ['weather', 'air', 'radar', 'solar', 'energy'] },
                         solar: { source: 'open-meteo', planes: [{ kwp: 5, tilt: 35, azimuth: 0 }], losses_pct: 14,
                                  inverter_kw: 0, keys: { fs_key: false, solcast_key: false, solcast_sites: 0 } },
                         energy: { source: 'off', battery: 'auto', keys: { solax_token: false, solax_sn: false } } };
const solarStatus = (solar = {}, energy = {}) => ({ device: {}, time: { valid: true }, battery: {}, sensors: {},
  preset: {}, wifi: { state: 'station', ap_on: false }, sync: { mode: 'times', running: false },
  solar: { source: 'open-meteo', ...solar }, energy: { source: 'off', ...energy } });

function solarDevice(patches, settings = SOLAR_SETTINGS, extra = {}) {
  return {
    'GET /api/settings': () => reply(200, JSON.parse(JSON.stringify(settings))),
    'GET /api/status': () => reply(200, solarStatus()),
    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, settings); },
    ...extra,
  };
}

/* An input by its label's text, in the order the page shows them. */
function inputNamed(root, name, n = 0) {
  const labels = below(root).filter((e) => e.tag === 'label' && text(e) === name);
  assert.ok(labels.length > n, `no field "${name}"`);
  const all = below(root);
  return all.slice(all.indexOf(labels[n]) + 1).find((e) => e.tag === 'input' || e.tag === 'select');
}

async function type(el, value) {
  el.value = value;
  await Promise.all([...(el.listeners.input || []), ...(el.listeners.change || [])].map((fn) => fn({ target: el })));
}

test('the Solar page saves our model\'s planes, losses and inverter limit', async () => {
  const patches = [];
  const { ctx, main } = await load(solarDevice(patches));
  await ctx.solarPage();
  await type(inputNamed(main, 'kWp'), '5.2');
  await buttonNamed(main, 'Add a second plane').click();
  await type(inputNamed(main, 'kWp', 1), '2.4');
  await type(inputNamed(main, 'Tilt (°)', 1), '20');
  await type(inputNamed(main, 'Azimuth (°)', 1), '-90');
  await type(inputNamed(main, 'Losses (%)'), '10');
  await type(inputNamed(main, 'Inverter limit (kW)'), '4.6');
  await buttonNamed(main, 'Save').click();
  assert.deepEqual(patches.at(-1).solar, { source: 'open-meteo', planes: [{ kwp: 5.2, tilt: 35, azimuth: 0 },
                                                                        { kwp: 2.4, tilt: 20, azimuth: -90 }],
                                           losses_pct: 10, inverter_kw: 4.6 });
});

test('keys are write-only: the page says which are set and sends only what you type', async () => {
  const patches = [];
  const settings = { ...SOLAR_SETTINGS, solar: { ...SOLAR_SETTINGS.solar, source: 'forecast-solar',
                                                 keys: { fs_key: true, solcast_key: false, solcast_sites: 0 } } };
  const { ctx, main } = await load(solarDevice(patches, settings));
  await ctx.solarPage();
  const key = inputNamed(main, 'Forecast.Solar key (optional)');
  assert.equal(key.value, '');
  assert.equal(key.attrs.type, 'password');
  assert.match(text(main), /A key is set/);
  await buttonNamed(main, 'Save').click();
  assert.equal(patches.at(-1).solar.fs_key, undefined); /* nothing typed: the key stays as it is */
  await type(key, 'AbC123');
  await buttonNamed(main, 'Save').click();
  assert.equal(patches.at(-1).solar.fs_key, 'AbC123');
  await buttonNamed(main, 'Clear the key').click();
  await buttonNamed(main, 'Save').click();
  assert.equal(patches.at(-1).solar.fs_key, null);
});

test('a refused save says why: a second plane needs a Forecast.Solar key', async () => {
  const patches = [];
  const settings = { ...SOLAR_SETTINGS, solar: { ...SOLAR_SETTINGS.solar, source: 'forecast-solar' } };
  const { ctx, main } = await load(solarDevice(patches, settings, {
    'PATCH /api/settings': () => reply(400, { error: 'a second plane needs a Forecast.Solar key' }),
  }));
  await ctx.solarPage();
  await buttonNamed(main, 'Add a second plane').click();
  await buttonNamed(main, 'Save').click();
  assert.match(text(main), /A second plane needs a Forecast.Solar key/);
});

test('Solcast takes its key and two sites, and its planes are set on solcast.com', async () => {
  const patches = [];
  const settings = { ...SOLAR_SETTINGS, solar: { ...SOLAR_SETTINGS.solar, source: 'solcast' } };
  const { ctx, main } = await load(solarDevice(patches, settings));
  await ctx.solarPage();
  assert.equal(below(main).some((e) => e.tag === 'label' && text(e) === 'kWp' && !hiddenAbove(main, e)), false);
  await type(inputNamed(main, 'Solcast API key'), 'Kk_1-2');
  await type(inputNamed(main, 'First site id'), 'ab12-cd34');
  await type(inputNamed(main, 'Second site id (optional)'), 'ef56');
  await buttonNamed(main, 'Save').click();
  assert.equal(patches.at(-1).solar.solcast_key, 'Kk_1-2');
  assert.deepEqual(patches.at(-1).solar.solcast_sites, ['ab12-cd34', 'ef56']);
  assert.match(text(main), /10 calls a day/);
});

/* Whether `el` sits in a part of the page that is hidden. */
function hiddenAbove(root, el) {
  const path = (node, target, trail = []) => {
    if (node === target) return trail;
    for (const k of node.children.filter((c) => c instanceof FakeElement)) {
      const found = path(k, target, [...trail, node]);
      if (found) return found;
    }
    return null;
  };
  return (path(root, el) || []).some((n) => n.hidden);
}

test('the house\'s energy takes SolaX Cloud\'s token and registration number, and the battery', async () => {
  const patches = [];
  const { ctx, main } = await load(solarDevice(patches));
  await ctx.solarPage();
  await type(inputNamed(main, 'Source', 1), 'solax');
  await type(inputNamed(main, 'Token'), '20200722');
  await type(inputNamed(main, 'Registration number'), 'SXA1B2C3D4');
  await type(inputNamed(main, 'Home battery'), 'on');
  await buttonNamed(main, 'Save').click();
  assert.deepEqual(patches.at(-1).energy, { source: 'solax', battery: 'on', solax_token: '20200722',
                                            solax_sn: 'SXA1B2C3D4' });
});

test('Check now runs the two steps and shows what they brought', async () => {
  let polls = 0;
  const { ctx, calls, main } = await load(solarDevice([], SOLAR_SETTINGS, {
    'GET /api/status': () => reply(200, ++polls < 3 ? solarStatus({ tried_at: 100 }, { source: 'solax', tried_at: 100 })
      : solarStatus({ tried_at: 200, fetched_at: 200, day: 20731 },
                    { source: 'solax', tried_at: 200, error: 'tokenId is invalid' })),
    'POST /api/solar/check': () => reply(202, { started: true }),
  }));
  ctx.setTimeout = (fn) => { fn(); return 0; };
  await ctx.solarPage();
  await buttonNamed(main, 'Check now').click();
  await settle();
  assert.ok(calls.some((c) => c.path === '/api/solar/check' && c.init.method === 'POST'));
  assert.match(text(main), /tokenId is invalid/);
  assert.match(text(main), /Checked/);
});

test('the Solar page says when Solcast is asked again, and why its last call failed', async () => {
  const solcast = solarStatus({ source: 'solcast', fetched_at: 100, tried_at: 300, kept: 'kept', next_at: 1790890800,
                                error: 'HTTP 401' });
  const { ctx, main } = await load(solarDevice([], SOLAR_SETTINGS, { 'GET /api/status': () => reply(200, solcast) }));
  await ctx.solarPage();
  assert.match(text(main), /Its last stepkept the forecast: Solcast is asked again from /);
  assert.match(text(main), /Its last callfailed: HTTP 401/);
  const limited = solarStatus({ source: 'forecast-solar', fetched_at: 100, tried_at: 300, kept: 'HTTP 429' });
  const page = await load(solarDevice([], SOLAR_SETTINGS, { 'GET /api/status': () => reply(200, limited) }));
  await page.ctx.solarPage();
  assert.match(text(page.main), /Its last stepkept the forecast \(HTTP 429\)/);
  assert.doesNotMatch(text(page.main), /Its last call/);
});

test('the Solar page credits its sources', async () => {
  const { ctx, main } = await load(solarDevice([]));
  await ctx.solarPage();
  const all = text(main);
  for (const credit of ['Open-Meteo', 'Forecast.Solar', 'CC BY-SA 4.0', 'Solcast', 'personal use', 'SolaX Cloud']) {
    assert.ok(all.includes(credit), credit);
  }
});

test('a preset on the Solar or Energy layout has no slots to fill', async () => {
  const catalogue = { ...CATALOGUE, layouts: [...CATALOGUE.layouts, { id: 'solar', slots: [] }, { id: 'energy', slots: [] }] };
  const doc = { schema: 1, active: 'solar', presets: [{ id: 'solar', name: 'Solar', layout: 'solar', in_cycle: false,
                                                          slots: {}, options: {} }],
                cycle: { enabled: false, interval_s: 60 }, schedule: { enabled: false, entries: [] } };
  const { ctx, main } = await load({
    'GET /api/layouts': () => reply(200, catalogue), 'GET /api/presets': () => reply(200, doc),
    'GET /api/fields': () => reply(200, FIELDS), 'POST /api/preview.bmp': () => reply(200, 'BM', 'image/bmp'),
    'GET /api/settings': () => reply(200, STEP_SETTINGS),
  });
  await ctx.presetsPage();
  assert.match(text(main), /today's PV forecast: its source is on the Solar page/);
  assert.match(text(main), /Solar/);
});

test('the field lists group the Solar and Energy fields and mark those whose step is off', async () => {
  const catalogue = { ...CATALOGUE, layouts: [{ id: 'classic', slots: [
    { id: 's1', x: 0, y: 172, w: 200, h: 128, size: 'S', kinds: ['number'] }] }] };
  const fields = { fields: [{ id: 'env.temp', kind: 'number', label: 'Temperature', value: '23.7 °C' },
                            { id: 'pv.now', kind: 'number', label: 'Forecast now', value: '3.50 kW' },
                            { id: 'energy.pv', kind: 'number', label: 'Solar', value: '3.42 kW' }] };
  const doc = { schema: 1, active: 'home', presets: [{ id: 'home', name: 'Home', layout: 'classic', in_cycle: true,
                                                         slots: {}, options: {} }],
                cycle: { enabled: false, interval_s: 60 }, schedule: { enabled: false, entries: [] } };
  const settings = { ...STEP_SETTINGS, sync: { ...STEP_SETTINGS.sync, steps: ['weather', 'air', 'radar', 'solar'] } };
  const { ctx, main } = await load({
    'GET /api/layouts': () => reply(200, catalogue), 'GET /api/presets': () => reply(200, doc),
    'GET /api/fields': () => reply(200, fields), 'POST /api/preview.bmp': () => reply(200, 'BM', 'image/bmp'),
    'GET /api/settings': () => reply(200, settings),
  });
  await ctx.presetsPage();
  const groups = below(main).filter((e) => e.tag === 'optgroup').map((g) => g.attrs.label);
  assert.ok(groups.includes('Solar forecast') && groups.includes('House energy'), groups.join());
  const options = below(main).filter((e) => e.tag === 'option').map(text);
  assert.ok(options.some((o) => /^Solar — 3.42 kW \(its sync step is off\)$/.test(o)), options.join(' | '));
  assert.ok(options.some((o) => /^Forecast now — 3.50 kW$/.test(o)), options.join(' | '));
});
