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
