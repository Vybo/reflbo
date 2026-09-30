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
async function load(routes = {}) {
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
    setTimeout: () => 0, clearTimeout() {}, confirm: () => true,
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
