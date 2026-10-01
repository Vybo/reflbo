'use strict';
/* reflbo web configurator (spec §10.3, D18): plain JS, no build step, English only (spec §5.8).
 * Every page reads the device's REST API; nothing is kept in the browser. */

const main = document.getElementById('main');
let auth = null; /* GET /api/auth */

/* ---- helpers ---- */

function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k === 'value') el.value = v;
    else if (k === 'checked' || k === 'selected' || k === 'disabled') el[k] = true;
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat(Infinity)) {
    if (kid !== null && kid !== undefined && kid !== false) el.append(kid instanceof Node ? kid : String(kid));
  }
  return el;
}

const card = (title, ...kids) => h('section', { class: 'card' }, title ? h('h2', { text: title }) : null, ...kids);
const facts = (pairs) => h('dl', { class: 'facts' }, pairs.filter(Boolean).map(([k, v]) => [h('dt', { text: k }), h('dd', {}, v)]));
const actions = (...buttons) => h('div', { class: 'actions' }, ...buttons);
const button = (text, onclick, cls) => h('button', { class: 'btn' + (cls ? ' ' + cls : ''), type: 'button', onclick }, text);
const field = (label, input, hint) => [h('label', {}, label), input, hint ? h('p', { class: 'muted small' }, hint) : null];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const bytes = (s) => new TextEncoder().encode(s).length;
/* The device's messages are lowercase phrases: "wrong password" reads "Wrong password." */
const sentence = (m) => (m ? m[0].toUpperCase() + m.slice(1) + (/[.!?…]$/.test(m) ? '' : '.') : m);

class ApiError extends Error {
  constructor(message, status, relogin) {
    super(message);
    this.status = status;
    this.relogin = !!relogin; /* the session ended: start() shows the login page instead */
  }
}

async function api(method, path, body) {
  const init = { method, headers: {} };
  if (body instanceof Blob) {
    init.body = body;
    init.headers['Content-Type'] = 'application/octet-stream';
  } else if (method !== 'GET') {
    /* Spec §10.4: the device refuses changes that don't say they are JSON, as a cross-site form
     * can't; that includes requests without a body. */
    init.headers['Content-Type'] = 'application/json';
    if (body !== undefined) init.body = JSON.stringify(body);
  }
  let r;
  try {
    r = await fetch(path, init);
  } catch (e) {
    throw new ApiError("The device doesn't answer. Is this phone still on its network?", 0);
  }
  const json = (r.headers.get('Content-Type') || '').includes('application/json');
  const data = json ? await r.json() : await r.blob();
  if (r.status === 401 && !path.startsWith('/api/auth')) {
    start();
    throw new ApiError(sentence(data.error), 401, true);
  }
  if (!r.ok) throw new ApiError(sentence((json && data.error) || r.statusText), r.status);
  return data;
}

let toastTimer;
function toast(text) {
  const t = document.getElementById('toast');
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (t.hidden = true), 3000);
}

/* Runs `fn` with the element's buttons disabled; an error goes into `note`. */
async function busy(el, note, fn) {
  const buttons = [...el.querySelectorAll('button')];
  buttons.forEach((b) => (b.disabled = true));
  if (note) note.textContent = '';
  try {
    await fn();
  } catch (e) {
    if (note && !e.relogin) {
      note.className = 'bad';
      note.textContent = e.message;
    }
  } finally {
    buttons.forEach((b) => (b.disabled = false));
  }
}

function duration(s) {
  const d = Math.floor(s / 86400), hr = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? `${d} d ${hr} h` : hr ? `${hr} h ${m} min` : `${m} min`;
}

function phoneZone() {
  const tz = Intl.DateTimeFormat().resolvedOptions().timeZone;
  return tz && ZONES[tz] ? tz : null;
}

/* POST /api/time: the phone's clock, and its zone if asked. */
async function setClock(withZone) {
  const body = { epoch: Math.round(Date.now() / 1000) };
  const tz = withZone ? phoneZone() : null;
  if (tz) Object.assign(body, { tz_iana: tz, tz_posix: ZONES[tz] });
  await api('POST', '/api/time', body);
}

function download(blob, name) {
  const a = h('a', { href: URL.createObjectURL(blob), download: name });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

/* ---- start, password and login (D18) ---- */

async function start() {
  try {
    auth = await api('GET', '/api/auth');
  } catch (e) {
    main.replaceChildren(card('No answer', h('p', { text: e.message }), actions(button('Try again', start))));
    return;
  }
  document.getElementById('device').textContent = auth.host;
  const inside = auth.password_set && auth.logged_in;
  document.getElementById('nav').hidden = !inside;
  document.getElementById('done').hidden = !inside;
  if (!inside) return auth.password_set ? loginPage() : setupPage();
  window.onhashchange = route;
  route();
}

function passwordForm(title, text, submit, withOld) {
  const old = h('input', { type: 'password', autocomplete: 'current-password', required: true });
  const p1 = h('input', { type: 'password', autocomplete: 'new-password', minlength: 8, maxlength: 64, required: true });
  const p2 = h('input', { type: 'password', autocomplete: 'new-password', required: true });
  const note = h('p');
  const form = h('form', {
    onsubmit: (ev) => {
      ev.preventDefault();
      if (p1.value !== p2.value) {
        note.className = 'bad';
        note.textContent = 'The two passwords differ.';
        return;
      }
      busy(form, note, () => submit(p1.value, old.value, note, form));
    },
  },
  text ? h('p', { text }) : null,
  withOld ? field('Current password', old) : null,
  field('New password', p1, '8 to 64 characters.'),
  field('Repeat it', p2),
  note,
  actions(h('button', { class: 'btn primary', type: 'submit' }, title)));
  return form;
}

function setupPage() {
  if (!auth.on_ap) {
    main.replaceChildren(h('h1', { text: 'Welcome' }), card('Choose a password first',
      h('p', {}, 'Only a phone on the device\'s own Wi-Fi can choose the password for this page. Join ',
        h('b', { text: auth.ap_ssid }), ' (its password is on the device\'s screen), then open ',
        h('b', { text: 'http://192.168.4.1' }), '.')));
    return;
  }
  main.replaceChildren(h('h1', { text: 'Welcome' }), card('Choose a password for this page',
    passwordForm('Set password', 'Anyone who joins the device\'s Wi-Fi could open this page; the password keeps the ' +
      'settings to you. If you forget it: Menu ▸ Wi-Fi ▸ Reset web password on the device.',
      async (password) => {
        await api('POST', '/api/auth/setup', { password });
        toast('Password set');
        start();
      })));
}

function loginPage() {
  const pass = h('input', { type: 'password', autocomplete: 'current-password', required: true });
  const note = h('p');
  const form = h('form', {
    onsubmit: (ev) => {
      ev.preventDefault();
      busy(form, note, async () => {
        await api('POST', '/api/auth/login', { password: pass.value });
        start();
      });
    },
  }, field('Password', pass), note, actions(h('button', { class: 'btn primary', type: 'submit' }, 'Log in')));
  main.replaceChildren(h('h1', { text: 'Log in' }), card(null, form,
    h('p', { class: 'muted small', text: 'Forgot it? On the device: Menu ▸ Wi-Fi ▸ Reset web password.' })));
  pass.focus();
}

document.getElementById('done').onclick = async () => {
  let lan = false; /* sync mode Always on keeps the device on the network, and this page with it (spec §9.3) */
  try {
    const s = await api('GET', '/api/status');
    lan = !!s.sync && s.sync.mode === 'always' && s.wifi.state === 'station' && !s.wifi.ap_on;
  } catch (e) { /* the page decides below */ }
  try {
    await api('POST', '/api/done');
  } catch (e) {
    toast(e.message);
    return;
  }
  if (lan) {
    toast('Done. Sync mode Always on keeps the device on your network, so this page still works.');
    return;
  }
  document.getElementById('nav').hidden = true;
  document.getElementById('done').hidden = true;
  main.replaceChildren(card('Wi-Fi is off', h('p', { text: 'You can close this page. To come back, hold BOOT on the ' +
    'device for 3 seconds.' })));
};

/* ---- pages ---- */

const pages = { status: statusPage, wifi: wifiPage, place: placePage, sync: syncPage, device: devicePage,
                presets: presetsPage, firmware: firmwarePage, backup: backupPage };

function route() {
  const name = location.hash.slice(1) || 'status';
  const page = pages[name] || statusPage;
  for (const a of document.querySelectorAll('nav a')) a.classList.toggle('on', a.getAttribute('href') === '#' + name);
  main.replaceChildren(h('p', { class: 'muted', text: 'Loading…' }));
  window.scrollTo(0, 0);
  page().catch((e) => {
    if (!e.relogin) main.replaceChildren(card('Something went wrong', h('p', { class: 'bad', text: e.message }),
      actions(button('Try again', route))));
  });
}

function wifiText(w) {
  switch (w.state) {
  case 'station': return `On ${w.ssid}${w.ip ? ' as ' + w.ip : ''}${w.ap_on ? `, and its own network ${w.ap_ssid}` : ''}`;
  case 'joining': return 'Connecting to a saved network…';
  case 'ap': return `Own network ${w.ap_ssid} only`;
  default: return 'Off';
  }
}

async function statusPage() {
  let s = await api('GET', '/api/status');
  let clockNote = null;
  if (!s.time.valid) { /* no backup cell (D9): the phone's clock is better than none */
    await setClock(false);
    s = await api('GET', '/api/status');
    clockNote = h('p', { class: 'good small', text: 'The device had lost the time; it now has this phone\'s.' });
  }
  const off = Math.round(s.time.epoch - Date.now() / 1000);
  const b = s.battery, env = s.sensors;
  const shot = h('img', { class: 'screen', alt: 'What the device shows', src: '/api/screenshot.bmp?t=' + Date.now() });
  main.replaceChildren(
    h('h1', { text: 'Status' }),
    card('Screen', shot, actions(button('Refresh', () => statusPage().catch(() => {})))),
    card('Device', facts([
      ['Name', `${s.device.id}.local`],
      ['Firmware', s.device.firmware],
      ['Running for', duration(s.device.uptime_s)],
      ['Free memory', `${(s.device.free_heap / 1048576).toFixed(1)} MB`],
      ['Preset', `${s.preset.name}${s.preset.cycle ? ', cycling' : ''}`],
    ])),
    card('Time', facts([
      ['On the device', s.time.local.replace('T', ' ')],
      ['Time zone', s.time.tz_iana],
      ['Clock', Math.abs(off) <= 2 ? 'matches this phone' : `${Math.abs(off)} s ${off > 0 ? 'ahead of' : 'behind'} this phone`],
    ]), clockNote, actions(button('Set the clock from this phone', async (ev) => {
      await busy(ev.target.parentNode, null, async () => {
        await setClock(false);
        toast('Clock set');
        await statusPage();
      });
    }))),
    card('Battery and sensors', facts([
      ['Battery', b.percent !== undefined ? `${b.percent} % · ${(b.mv / 1000).toFixed(2)} V · ${b.state}` : 'no reading yet'],
      b.days_left !== null ? ['Lasts', `about ${b.days_left.toFixed(1)} days`] : null,
      ['Temperature', env.temp_c !== null ? `${env.temp_c.toFixed(1)} °C` : '—'],
      ['Humidity', env.hum_pct !== null ? `${env.hum_pct.toFixed(0)} %` : '—'],
      env.age_s !== undefined ? ['Measured', `${duration(env.age_s)} ago`] : null,
    ])),
    card('Wi-Fi', facts([['Now', wifiText(s.wifi)], s.wifi.ap_on ? ['On its network', `${s.wifi.ap_clients} device(s)`] : null])),
    card('Sync', facts(syncFacts(s.sync)), actions(h('a', { class: 'btn', href: '#sync' }, 'Sync settings'))),
    card('This page', passwordForm('Change password', null, async (password, old, note, form) => {
      await api('POST', '/api/auth/password', { old, password });
      form.reset();
      note.className = 'good';
      note.textContent = 'Password changed.';
    }, true), actions(button('Log out', async () => {
      await api('POST', '/api/auth/logout');
      start();
    }), button('Restart the device', async () => {
      if (!confirm('Restart the device? Wi-Fi comes back on by itself.')) return;
      await api('POST', '/api/reboot');
      waitForRestart('Restarting…');
    }))),
  );
}

/* ---- Sync (spec §9.3, D25) ---- */

const SYNC_STEPS = [['wifi', 'Wi-Fi'], ['time', 'Time'], ['weather', 'Weather'], ['air', 'Air quality']];
const SYNC_INTERVALS = [15, 30, 60, 120, 180, 360, 720, 1440];
const intervalLabel = (m) => (m < 60 ? `${m} min` : `${m / 60} h`);

function when(epoch) {
  if (!epoch) return '—';
  const d = new Date(epoch * 1000), now = new Date();
  const hm = d.toTimeString().slice(0, 5);
  const day = d.toDateString() === now.toDateString() ? 'today'
    : d.toDateString() === new Date(now.getTime() + 86400000).toDateString() ? 'tomorrow' : d.toISOString().slice(0, 10);
  return `${hm} ${day}`;
}

function syncFacts(sync) {
  if (!sync) return [];
  const last = sync.last;
  return [
    ['Last sync', sync.running ? `running: ${(SYNC_STEPS.find(([k]) => k === sync.step) || [0, '…'])[1]}`
      : !last ? 'not since the device started'
        : last.failed ? `${when(last.at)}: ${(SYNC_STEPS.find(([k]) => k === last.failed) || [0, last.failed])[1]} failed (${last.detail})`
          : `${when(last.at)}, all well`],
    ['Next', sync.next ? `${when(sync.next)}${sync.next_retry ? ', a retry' : ''}` : sync.mode === 'manual' ? 'when you ask' : '—'],
    sync.weather_at ? ['Weather from', when(sync.weather_at)] : null,
  ];
}

async function syncPage() {
  const [s, st] = await Promise.all([api('GET', '/api/settings'), api('GET', '/api/status')]);
  const sync = s.sync || {}, quiet = sync.quiet || {};
  const steps = h('dl', { class: 'facts' });
  const showSteps = (status) => {
    const last = status.sync.last;
    steps.replaceChildren(...SYNC_STEPS.flatMap(([k, name]) => [h('dt', { text: name }),
      h('dd', { class: !last ? '' : last.steps[k] === 'ok' ? 'good' : last.steps[k] === 'failed' ? 'bad' : 'muted',
                text: !last ? '—' : last.steps[k] === 'failed' && last.failed === k ? `failed: ${last.detail}` : last.steps[k] })]));
  };
  const summary = h('div');
  const rtc = h('p', { class: 'muted small' });
  const showStatus = (status) => {
    summary.replaceChildren(facts(syncFacts(status.sync)));
    showSteps(status);
    const r = status.time.rtc || {};
    rtc.textContent = `The clock chip's trim: ${r.trim_steps ?? 0} steps` +
      (r.drift_s_per_day !== undefined ? `; it drifted ${r.drift_s_per_day > 0 ? '+' : ''}${r.drift_s_per_day} s a day before the last sync.` : '.');
  };
  showStatus(st);
  const nowNote = h('p');
  const nowCard = card('Now', summary, steps, rtc, nowNote, actions(button('Sync now', () => busy(nowCard, nowNote, async () => {
    await api('POST', '/api/sync');
    nowNote.className = 'muted';
    nowNote.textContent = 'Syncing…';
    for (let i = 0; i < 60; i++) { /* a sync takes some seconds, 45 s at most (spec §9.3) */
      await sleep(1500);
      const now = await api('GET', '/api/status');
      showStatus(now);
      if (!now.sync.running) {
        nowNote.className = now.sync.last && !now.sync.last.failed ? 'good' : 'bad';
        nowNote.textContent = now.sync.last && !now.sync.last.failed ? 'Synced.' : 'The sync failed; see above.';
        return;
      }
    }
  }), 'primary')));

  const modes = [['times', 'At set times'], ['interval', 'Every interval'], ['always', 'Always on'], ['manual', 'Only when asked']];
  let mode = modes.some(([m]) => m === sync.mode) ? sync.mode : 'times';
  const radios = modes.map(([m, label]) => h('input', { type: 'radio', name: 'mode', value: m, checked: m === mode,
                                                       onchange: () => { mode = m; showMode(); } }));
  let times = Array.isArray(sync.times) && sync.times.length ? [...sync.times] : ['05:30'];
  const timeList = h('div');
  const showTimes = () => timeList.replaceChildren(...times.map((v, i) => h('div', { class: 'row' },
    h('input', { type: 'time', value: v, onchange: (ev) => { times[i] = ev.target.value; } }),
    times.length > 1 ? button('Remove', () => { times.splice(i, 1); showTimes(); }) : null)),
  times.length < 8 ? button('Add a time', () => { times.push('12:00'); showTimes(); }) : null);
  showTimes();
  const interval = h('select', {}, SYNC_INTERVALS.map((m) => h('option', { value: String(m), selected: m === (sync.interval_min ?? 60) },
    intervalLabel(m))));
  interval.value = String(SYNC_INTERVALS.includes(sync.interval_min) ? sync.interval_min : 60);
  const timesBox = h('div', {}, h('label', {}, 'Times'), timeList);
  const intervalBox = h('div', {}, field('Every', interval));
  const alwaysNote = h('p', { class: 'muted small', text: 'Wi-Fi stays on and the device stays awake: meant for USB ' +
    'power, as it drains the battery in days. This page stays reachable on your network.' });
  const manualNote = h('p', { class: 'muted small', text: 'No sync starts by itself; the weather shows its age.' });
  const showMode = () => {
    timesBox.hidden = mode !== 'times';
    intervalBox.hidden = mode !== 'interval';
    alwaysNote.hidden = mode !== 'always';
    manualNote.hidden = mode !== 'manual';
  };
  showMode();
  const shortcut = (label, m, set) => button(label, () => {
    mode = m;
    radios.forEach((r) => (r.checked = r.value === m));
    set();
    showTimes();
    showMode();
  });
  const quietOn = h('input', { type: 'checkbox', checked: !!quiet.enabled });
  const from = h('input', { type: 'time', value: quiet.from || '23:00' });
  const to = h('input', { type: 'time', value: quiet.to || '06:00' });
  const minutes = (v) => Number(v.slice(0, 2)) * 60 + Number(v.slice(3, 5));
  const inQuiet = (v) => {
    const a = minutes(from.value), b = minutes(to.value), m = minutes(v);
    return a < b ? m >= a && m < b : a > b ? m >= a || m < b : false;
  };
  const note = h('p');
  const schedCard = card('Schedule',
    h('div', { class: 'actions' }, shortcut('Battery saver', 'times', () => { times = ['05:30']; }),
      shortcut('Balanced', 'interval', () => { interval.value = '60'; }), shortcut('Always connected', 'always', () => {})),
    ...modes.map(([m, label], i) => h('label', { class: 'check' }, radios[i], label)),
    timesBox, intervalBox, alwaysNote, manualNote,
    h('h3', { text: 'Quiet hours' }),
    h('p', { class: 'muted small', text: 'No sync starts by itself in these hours; one runs as they end. In "Always on", ' +
      'Wi-Fi goes off for them.' }),
    h('label', { class: 'check' }, quietOn, 'Quiet hours'),
    h('div', { class: 'row' }, h('div', {}, field('From', from)), h('div', {}, field('To', to))),
    note,
    actions(button('Save', () => busy(schedCard, note, async () => {
      const valid = (v) => /^([01]\d|2[0-3]):[0-5]\d$/.test(v);
      const list = [...new Set(times.filter(valid))].sort();
      if (mode === 'times' && !list.length) throw new ApiError('Add a time.');
      if (quietOn.checked && (!valid(from.value) || !valid(to.value) || from.value === to.value)) {
        throw new ApiError('Quiet hours need a start and an end that differ.');
      }
      await api('PATCH', '/api/settings', { sync: { mode, times: list.length ? list : ['05:30'],
        interval_min: Number(interval.value), quiet: { enabled: quietOn.checked, from: from.value, to: to.value } } });
      times = list.length ? list : ['05:30'];
      showTimes();
      const moved = mode === 'times' && quietOn.checked ? list.filter(inQuiet) : [];
      note.className = moved.length ? 'muted' : 'good';
      note.textContent = moved.length ? `Saved. ${moved.join(', ')} falls in the quiet hours: it runs at ${to.value}.` : 'Saved.';
      toast('Saved');
    }), 'primary')));
  main.replaceChildren(h('h1', { text: 'Sync' }), nowCard, schedCard);
}

/* ---- Wi-Fi (spec §10.1, §10.2) ---- */

function bars(rssi) {
  const n = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  return '▮'.repeat(n) + '▯'.repeat(4 - n);
}

const TEST_TEXT = {
  'wrong password': 'The password is wrong.',
  'not found': "The device can't see this network. It uses 2.4 GHz only: check the name, and that the router is in reach.",
  failed: "It couldn't connect.",
  invalid: 'The name or password is not valid.',
};

/* The test runs on the device while this phone may drop off its network for a moment (spec §10.2). */
async function tryNetwork(ssid, password, note) {
  await api('POST', '/api/wifi/networks', { ssid, password });
  note.className = 'muted';
  note.textContent = `Trying ${ssid}…`;
  for (const until = Date.now() + 60000; Date.now() < until;) {
    await sleep(1000);
    let n;
    try {
      n = await api('GET', '/api/wifi/networks');
    } catch (e) {
      if (e.relogin) throw e;
      note.textContent = `Trying ${ssid}… This phone lost the device's network for a moment; waiting for it.`;
      continue;
    }
    if (n.test.ssid !== ssid || n.test.result === 'testing') continue;
    if (n.test.result === 'connected') return n;
    throw new ApiError(TEST_TEXT[n.test.result] || n.test.result, 409);
  }
  throw new ApiError('The device gave no answer in time.', 0);
}

async function wifiPage() {
  const n = await api('GET', '/api/wifi/networks');
  const ssid = h('input', { type: 'text', maxlength: 32, autocapitalize: 'off', autocomplete: 'off', required: true });
  const pass = h('input', { type: 'password', maxlength: 64, autocomplete: 'off' });
  const note = h('p');
  const found = h('ul', { class: 'list' });
  const scanNote = h('p', { class: 'muted small' });
  const scan = () => busy(found.parentNode, scanNote, async () => {
    scanNote.textContent = 'Looking…';
    const r = await api('GET', '/api/wifi/scan');
    scanNote.textContent = r.networks.length ? 'Tap one to use its name.' : 'No networks in sight.';
    found.replaceChildren(...r.networks.map((ap) => h('li', {},
      h('button', { class: 'link', type: 'button', onclick: () => { ssid.value = ap.ssid; pass.focus(); } }, ap.ssid),
      h('span', { class: 'muted small', text: ap.open ? 'open' : '' }),
      h('span', { class: 'bars', title: `${ap.rssi} dBm`, text: bars(ap.rssi) }))));
  });
  const valid = () => {
    if (bytes(ssid.value) < 1 || bytes(ssid.value) > 32) return 'The name needs 1 to 32 characters.';
    if (pass.value && (pass.value.length < 8 || pass.value.length > 63) && !/^[0-9a-fA-F]{64}$/.test(pass.value)) {
      return 'A Wi-Fi password has 8 to 63 characters.';
    }
    return null;
  };
  const form = h('form', {
    onsubmit: (ev) => {
      ev.preventDefault();
      const bad = valid();
      if (bad) {
        note.className = 'bad';
        note.textContent = bad;
        return;
      }
      busy(form, note, async () => {
        const r = await tryNetwork(ssid.value, pass.value, note);
        note.className = 'good';
        note.textContent = `Connected: the device is on ${r.ssid} as ${r.ip}. On that network, open ` +
          `http://${auth.host}.local or http://${r.ip}.`;
        pass.value = '';
        refreshLists(await api('GET', '/api/wifi/networks'));
      });
    },
  },
  field('Network name', ssid), field('Password', pass, 'Leave it empty for an open network.'), note,
  actions(h('button', { class: 'btn primary', type: 'submit' }, 'Connect'),
    button('Save without trying', () => {
      const bad = valid();
      if (bad) {
        note.className = 'bad';
        note.textContent = bad;
        return;
      }
      busy(form, note, async () => {
        await api('POST', '/api/wifi/networks', { ssid: ssid.value, password: pass.value, test: false });
        note.className = 'good';
        note.textContent = `Saved: the device tries ${ssid.value} the next time Wi-Fi comes on.`;
        pass.value = '';
        refreshLists(await api('GET', '/api/wifi/networks'));
      });
    })));
  const savedRows = (list) => list.saved.length ? list.saved.map((name) => h('li', {},
    h('span', { class: 'grow', text: name }),
    button('Forget', async (ev) => {
      if (!confirm(`Forget ${name}?`)) return;
      await busy(ev.target.parentNode, null, async () => {
        await api('DELETE', '/api/wifi/networks?ssid=' + encodeURIComponent(name));
        refreshLists(await api('GET', '/api/wifi/networks'));
      });
    }))) : [h('li', { class: 'muted', text: 'None yet.' })];
  const saved = h('ul', { class: 'list' }, ...savedRows(n));
  const now = h('div', {}, facts([['Wi-Fi', wifiText(Object.assign({ ap_ssid: auth.ap_ssid }, n))]]));
  const refreshLists = (list) => {
    saved.replaceChildren(...savedRows(list));
    now.replaceChildren(facts([['Wi-Fi', wifiText(Object.assign({ ap_ssid: auth.ap_ssid }, list))]]));
  };
  const lanNote = !auth.on_ap && n.state === 'station'
    ? h('p', { class: 'muted small', text: `This phone reaches the device over ${n.ssid}. Trying another network may ` +
      'cut this page off: "Save without trying" is safer from here.' }) : null;
  main.replaceChildren(
    h('h1', { text: 'Wi-Fi' }),
    card('Now', now),
    card('Saved networks', saved, h('p', { class: 'muted small', text: 'Tried in this order, at most 5.' })),
    card('Add a network', h('div', {}, found, scanNote, actions(button('Look for networks', scan))), form, lanNote),
  );
  scan();
}

/* ---- Location and time ---- */

async function placePage() {
  const s = await api('GET', '/api/settings');
  const loc = s.location || {}, time = s.time || {};
  const name = h('input', { type: 'text', maxlength: 31, value: loc.name || '' });
  const lat = h('input', { type: 'number', step: 'any', min: -90, max: 90, value: loc.lat ?? '' });
  const lon = h('input', { type: 'number', step: 'any', min: -180, max: 180, value: loc.lon ?? '' });
  const locNote = h('p');
  const query = h('input', { type: 'search', placeholder: 'A town or city' });
  const found = h('div');
  const search = () => busy(locCard, locNote, async () => { /* spec §10.3: through the device, which is online */
    if (!query.value.trim()) throw new ApiError('Type a place to look for.');
    const r = await api('GET', '/api/geocode?q=' + encodeURIComponent(query.value.trim()));
    found.replaceChildren(...(r.places.length ? r.places.map((pl) => button(
      `${pl.name}${pl.region ? ', ' + pl.region : ''}${pl.country ? ', ' + pl.country : ''}`, () => {
        name.value = pl.name.slice(0, 31);
        lat.value = String(pl.lat);
        lon.value = String(pl.lon);
        found.replaceChildren(h('p', { class: 'muted small', text: `${pl.name}: ${pl.lat}, ${pl.lon}. Save it below.` }));
      })) : [h('p', { class: 'muted small', text: 'Nothing found by that name.' })]));
  });
  const locCard = card('Location', h('p', { class: 'muted small', text: 'For sunrise, sunset and the weather. ' +
    'Decimal degrees: north and east are positive.' }),
  h('div', { class: 'row' }, h('div', {}, field('Find a place', query)), actions(button('Search', search))), found,
  field('Name', name), h('div', { class: 'row' }, h('div', {}, field('Latitude', lat)), h('div', {}, field('Longitude', lon))),
  locNote, actions(button('Save location', () => busy(locCard, locNote, async () => {
    const la = Number(lat.value), lo = Number(lon.value);
    if (!name.value.trim() || bytes(name.value) > 31) throw new ApiError('The name needs 1 to 31 bytes.');
    if (lat.value === '' || !(la >= -90 && la <= 90)) throw new ApiError('Latitude: -90 to 90.');
    if (lon.value === '' || !(lo >= -180 && lo <= 180)) throw new ApiError('Longitude: -180 to 180.');
    await api('PATCH', '/api/settings', { location: { name: name.value.trim(), lat: la, lon: lo } });
    toast('Location saved');
  }), 'primary')));

  const filter = h('input', { type: 'search', placeholder: 'Filter: Prague, New_York, UTC…' });
  const zone = h('select', { size: 8 });
  const fillZones = () => {
    const q = filter.value.trim().toLowerCase().replace(/ /g, '_');
    const names = Object.keys(ZONES).filter((z) => !q || z.toLowerCase().includes(q));
    if (time.tz_iana && !names.includes(time.tz_iana) && !q) names.unshift(time.tz_iana);
    zone.replaceChildren(...names.map((z) => h('option', { value: z, selected: z === time.tz_iana }, z.replace(/_/g, ' '))));
  };
  filter.oninput = fillZones;
  fillZones();
  const h24 = h('input', { type: 'checkbox', checked: time.clock_24h !== false });
  const tzNote = h('p');
  const mine = phoneZone();
  const tzCard = card('Time zone', field('Zone', filter), zone,
    mine && mine !== time.tz_iana ? h('p', { class: 'muted small' }, `This phone: ${mine.replace(/_/g, ' ')}. `,
      h('a', { href: '#', onclick: (ev) => { ev.preventDefault(); filter.value = ''; time.tz_iana = mine; fillZones(); } },
        'Pick it')) : null,
    h('label', { class: 'check' }, h24, '24-hour clock'), tzNote,
    actions(button('Save time zone', () => busy(tzCard, tzNote, async () => {
      const z = zone.value || time.tz_iana;
      if (!ZONES[z] && z !== time.tz_iana) throw new ApiError('Pick a zone from the list.');
      const t = { clock_24h: h24.checked };
      if (ZONES[z]) Object.assign(t, { tz_iana: z, tz_posix: ZONES[z] });
      await api('PATCH', '/api/settings', { time: t });
      time.tz_iana = z;
      toast('Time zone saved');
    }), 'primary')));

  const clockNote = h('p');
  const clockCard = card('Clock', h('p', { class: 'muted small', text: 'Sets the device from this phone\'s clock. ' +
    'Without a backup cell the device forgets the time when it is switched off.' }), clockNote,
  actions(button('Set the clock from this phone', () => busy(clockCard, clockNote, async () => {
    await setClock(false);
    toast('Clock set');
  }))));
  main.replaceChildren(h('h1', { text: 'Location and time' }), locCard, tzCard, clockCard);
}

/* ---- Device: the settings the menu also has (spec §5.7, D19) ---- */

const LANGUAGES = [['en', 'English'], ['cs', 'Čeština']];
const SENSOR_MIN = [1, 2, 5, 10, 15, 30];
const RATES = [0.25, 0.5, 1, 2, 4, 8];

async function devicePage() {
  const [s, st] = await Promise.all([api('GET', '/api/settings'), api('GET', '/api/status')]);
  const sensors = s.sensors || {}, display = s.display || {};
  const select = (pairs, current) => h('select', {}, pairs.map(([v, t]) => h('option', { value: v, selected: v === current }, t)));
  const language = select(LANGUAGES, s.language || 'en');
  const unit = select([['C', '°C'], ['F', '°F']], (s.units || {}).temp === 'F' ? 'F' : 'C');
  const temp = h('input', { type: 'number', step: 0.1, min: -10, max: 10, value: sensors.temp_offset_c ?? 0 });
  const hum = h('input', { type: 'number', step: 0.5, min: -20, max: 20, value: sensors.hum_offset_pct ?? 0 });
  const every = select(SENSOR_MIN.map((m) => [String(m), `${m} min`]), String(sensors.interval_min ?? 5));
  const update = select(Array.from({ length: 15 }, (_, i) => [String(i + 1), `${i + 1} min`]), String(display.update_min ?? 1));
  const rate = select(RATES.map((r) => [String(r), `${String(r)} Hz`]), String(display.lpm_hz ?? 1));
  const bat = s.battery || {};
  const learned = Array.isArray(bat.learned_mv); /* D21: a full discharge was learned */
  const levelFrom = select([['curve', 'The built-in Li-ion curve'], ['manual', 'My own voltages'],
    ...(learned ? [['learned', `Learned from a discharge (${new Date(bat.learned_at * 1000).toISOString().slice(0, 10)})`]] : [])],
    bat.level_from === 'manual' || (learned && bat.level_from === 'learned') ? bat.level_from : 'curve');
  const learn = (st.battery && st.battery.learn) || { state: 'off', hours: 0 };
  const learnNote = h('p', { class: 'muted small', text: {
    waiting: 'Learning: waiting for a full charge. Charge the battery full, then unplug it.',
    recording: `Learning: ${learn.hours} h of discharge so far. It ends when the battery is low.`,
    failed: 'The last discharge was too short to learn from; the next full charge tries again.',
  }[learn.state] || 'The device can learn this battery\'s own curve from one full discharge: charge it full, ' +
    'unplug it, and it learns until the battery is low, about a week on this board. It then uses that curve.' });
  const running = ['waiting', 'recording', 'failed'].includes(learn.state);
  const learnButton = button(running ? 'Stop learning' : 'Learn from the next full discharge', () => busy(batCard,
    learnNote, async () => {
      await api('POST', '/api/battery/learn', running ? { stop: true } : { start: true });
      await devicePage();
    }));
  const fullV = h('input', { type: 'number', step: 0.01, min: 3.6, max: 4.4, value: bat.full_v ?? 4.2 });
  const emptyV = h('input', { type: 'number', step: 0.01, min: 3, max: 4, value: bat.empty_v ?? 3.27 });
  const note = h('p');
  const batCard = card('Battery', field('Battery level from', levelFrom), field('Full, V', fullV), field('Empty, V', emptyV,
    'With your own voltages the level follows the Li-ion curve stretched between them. Read them on the ' +
    'Status page: full just after the charger stops, empty when the device shows its low-battery screen.'),
  learnNote, actions(learnButton));
  const form = h('div', {},
    card('Language and units', field('Language on the device', language), field('Temperature', unit)),
    card('Sensors', field('Temperature offset, °C', temp, 'Added to every reading. The board warms the sensor by ' +
      'about 2 °C, which the default −2.0 makes up for; trim it against a thermometer you trust.'),
    field('Humidity offset, %', hum), field('Measure every', every)),
    card('Screen', field('Update every', update, 'How often the dashboard is redrawn. Each update wakes the device.'),
      field('Refresh rate', rate, 'How often the panel repaints its image between updates. At 1 Hz it looked as ' +
        'good as the faster rates (D12).')),
    batCard,
    note,
    actions(button('Save', () => busy(form, note, async () => {
      const t = Number(temp.value), hu = Number(hum.value);
      if (temp.value === '' || !(t >= -10 && t <= 10)) throw new ApiError('Temperature offset: -10 to 10 °C.');
      if (hum.value === '' || !(hu >= -20 && hu <= 20)) throw new ApiError('Humidity offset: -20 to 20 %.');
      const battery = { level_from: levelFrom.value };
      if (levelFrom.value === 'manual') {
        const fv = Number(fullV.value), ev = Number(emptyV.value);
        if (emptyV.value === '' || !(ev >= 3 && ev <= 4)) throw new ApiError('Empty: 3.0 to 4.0 V.');
        if (fullV.value === '' || !(fv >= 3.6 && fv <= 4.4)) throw new ApiError('Full: 3.6 to 4.4 V.');
        if (Math.round((fv - ev) * 100) < 30) throw new ApiError('Full must be at least 0.3 V above empty.');
        Object.assign(battery, { empty_v: Math.round(ev * 100) / 100, full_v: Math.round(fv * 100) / 100 });
      }
      await api('PATCH', '/api/settings', {
        language: language.value,
        units: { temp: unit.value },
        sensors: { temp_offset_c: Math.round(t * 10) / 10, hum_offset_pct: Math.round(hu * 2) / 2,
                   interval_min: Number(every.value) },
        display: { update_min: Number(update.value), lpm_hz: Number(rate.value) },
        battery,
      });
      toast('Saved');
    }), 'primary')));
  main.replaceChildren(h('h1', { text: 'Device' }), form);
}

/* ---- Presets (spec §5.4) ---- */

const LAYOUT_NAMES = { classic: 'Classic', weather: 'Weather', grid: 'Grid', focus: 'Focus' };
const CYCLE_S = [10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];
const cycleLabel = (s) => (s < 60 ? `${s} s` : s < 3600 ? `${s / 60} min` : `${s / 3600} h`);
const DAYS = ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']; /* bit 0 is Monday */
let catalogue = null; /* GET /api/layouts, cached: it doesn't change while the page is open */

function uniqueId(doc, base) {
  const stem = (base.toLowerCase().normalize('NFD').replace(/[^a-z0-9_-]+/g, '-').replace(/^-+|-+$/g, '') || 'preset')
    .slice(0, 12);
  let id = stem;
  for (let i = 2; doc.presets.some((p) => p.id === id); i++) id = `${stem}-${i}`.slice(0, 15);
  return id;
}

async function presetsPage() {
  if (!catalogue) catalogue = await api('GET', '/api/layouts');
  const [doc, fieldList] = await Promise.all([api('GET', '/api/presets'), api('GET', '/api/fields')]);
  const ed = {
    doc, fields: fieldList.fields, dirty: false,
    sel: Math.max(0, doc.presets.findIndex((p) => p.id === doc.active)),
    img: h('img', { class: 'screen', alt: 'Preview' }), previewNote: h('p', { class: 'small' }),
    timer: null, url: null, saveNote: h('p'),
  };
  if (!ed.doc.schedule) ed.doc.schedule = { enabled: false, entries: [] };
  renderPresets(ed);
  preview(ed);
}

/* The preview, with each slot's name at its top right corner, as the slot fields below call them
 * (the renderer puts captions top left). */
function previewBox(ed, layout) {
  const pct = (v, of) => `${+(100 * v / of).toFixed(3)}%`;
  return h('div', { class: 'preview' }, ed.img, layout.slots.map((slot) => {
    const tag = h('span', { class: 'slot-tag', text: slot.id });
    tag.style.left = pct(slot.x + slot.w, catalogue.width);
    tag.style.top = pct(slot.y, catalogue.height);
    return tag;
  }));
}

function layoutOf(id) {
  return catalogue.layouts.find((l) => l.id === id) || catalogue.layouts[0];
}

/* The device renders the preset as it stands in the editor (POST /api/preview.bmp). */
function preview(ed) {
  clearTimeout(ed.timer);
  ed.timer = setTimeout(async () => {
    const p = ed.doc.presets[ed.sel];
    try {
      const bmp = await api('POST', '/api/preview.bmp?preset=' + encodeURIComponent(p.id), ed.doc);
      if (ed.url) URL.revokeObjectURL(ed.url);
      ed.url = URL.createObjectURL(bmp);
      ed.img.src = ed.url;
      ed.previewNote.textContent = '';
    } catch (e) {
      ed.previewNote.className = 'bad small';
      ed.previewNote.textContent = e.message;
    }
  }, 250);
}

function changed(ed, rerender) {
  ed.dirty = true;
  if (rerender) renderPresets(ed);
  else ed.saveBar && (ed.saveBar.hidden = false);
  preview(ed);
}

function renderPresets(ed) {
  const doc = ed.doc, p = doc.presets[ed.sel];
  const list = h('ul', { class: 'list' }, doc.presets.map((q, i) => h('li', { class: i === ed.sel ? 'sel' : null },
    h('input', { type: 'radio', name: 'active', title: 'Show this one now', checked: q.id === doc.active,
                 onchange: () => { doc.active = q.id; changed(ed, true); } }),
    h('button', { class: 'link', type: 'button', onclick: () => { ed.sel = i; renderPresets(ed); preview(ed); } },
      h('b', { text: q.name }), ' ', h('span', { class: 'muted small', text: LAYOUT_NAMES[q.layout] || q.layout })),
    h('label', { class: 'check small', title: 'KEY and the auto-cycle step through these' },
      h('input', { type: 'checkbox', checked: q.in_cycle, onchange: (ev) => { q.in_cycle = ev.target.checked; changed(ed, false); } }),
      'cycle'))));
  const listCard = card('Presets', list, h('p', { class: 'muted small', text: 'Tap a preset to edit it. The dot marks ' +
    'the one on the screen now; "cycle" puts a preset in the order KEY and the auto-cycle step through.' }), actions(
    button('New preset', () => {
      if (doc.presets.length >= 16) return toast('16 presets at most');
      const copy = JSON.parse(JSON.stringify(p));
      copy.name = `${p.name} copy`.slice(0, 23);
      copy.id = uniqueId(doc, copy.name);
      copy.in_cycle = true; /* a new preset shows up on KEY, whatever it was copied from */
      doc.presets.splice(ed.sel + 1, 0, copy);
      ed.sel++;
      changed(ed, true);
    })));

  const name = h('input', { type: 'text', maxlength: 23, value: p.name,
                            onchange: (ev) => { p.name = ev.target.value.trim() || p.id; changed(ed, true); } });
  const layout = h('select', { onchange: (ev) => {
    const old = p.slots;
    p.layout = ev.target.value;
    p.slots = {};
    for (const slot of layoutOf(p.layout).slots) { /* keep what still fits */
      const f = old[slot.id] && ed.fields.find((x) => x.id === old[slot.id]);
      if (f && slot.kinds.includes(f.kind)) p.slots[slot.id] = f.id;
    }
    changed(ed, true);
  } }, catalogue.layouts.map((l) => h('option', { value: l.id, selected: l.id === p.layout }, LAYOUT_NAMES[l.id] || l.id)));
  const slots = h('div', { class: 'slots' }, layoutOf(p.layout).slots.map((slot) => h('div', { class: 'slot' },
    h('label', { text: `${slot.id} · ${slot.size}` }),
    h('select', { onchange: (ev) => {
      if (ev.target.value) p.slots[slot.id] = ev.target.value;
      else delete p.slots[slot.id];
      changed(ed, false);
    } }, h('option', { value: '' }, '(empty)'),
    ed.fields.filter((f) => slot.kinds.includes(f.kind)).map((f) => h('option',
      { value: f.id, selected: p.slots[slot.id] === f.id }, `${f.label} — ${f.value || 'no data yet'}`))))));

  const o = p.options;
  const check = (key, text) => h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: !!o[key],
    onchange: (ev) => { o[key] = ev.target.checked; changed(ed, key === 'seconds'); } }), text);
  const clock = h('select', { onchange: (ev) => {
    if (ev.target.value === '') delete o.clock_24h;
    else o.clock_24h = ev.target.value === '24';
    changed(ed, false);
  } }, h('option', { value: '', selected: o.clock_24h === undefined }, 'As set for the device'),
  h('option', { value: '24', selected: o.clock_24h === true }, '24-hour'),
  h('option', { value: '12', selected: o.clock_24h === false }, '12-hour'));
  const stale = h('select', { onchange: (ev) => { o.stale_policy = ev.target.value; changed(ed, false); } },
    [['stale', 'Show it with its age'], ['placeholder', 'Show a dash'], ['hide', 'Leave the slot empty']]
      .map(([v, t]) => h('option', { value: v, selected: (o.stale_policy || 'stale') === v }, t)));
  const battery = new Set(o.status_battery || ['percent']);
  const part = (key, text) => h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: battery.has(key),
    onchange: (ev) => {
      ev.target.checked ? battery.add(key) : battery.delete(key);
      o.status_battery = ['percent', 'voltage', 'days'].filter((k) => battery.has(k));
      changed(ed, false);
    } }), text);
  const editCard = card(`Edit ${p.name}`, previewBox(ed, layoutOf(p.layout)), ed.previewNote,
    field('Name', name), field('Layout', layout), slots,
    field('Time format', clock), check('seconds', 'Show seconds'),
    o.seconds ? h('p', { class: 'bad small', text: 'Seconds wake the device every second: the battery lasts far less.' }) : null,
    check('invert', 'White on black'), field('Old or missing data', stale),
    h('label', {}, 'Status bar'), check('status_clock', 'A small clock'),
    part('percent', 'Battery level'), part('voltage', 'Battery voltage'), part('days', 'Days of battery left'),
    actions(
      button('Move up', () => { if (ed.sel > 0) { doc.presets.splice(ed.sel - 1, 0, doc.presets.splice(ed.sel, 1)[0]); ed.sel--; changed(ed, true); } }),
      button('Move down', () => { if (ed.sel < doc.presets.length - 1) { doc.presets.splice(ed.sel + 1, 0, doc.presets.splice(ed.sel, 1)[0]); ed.sel++; changed(ed, true); } }),
      button('Delete', () => {
        if (doc.presets.length < 2) return toast('The last preset stays');
        if (!confirm(`Delete ${p.name}?`)) return;
        doc.presets.splice(ed.sel, 1);
        doc.schedule.entries = doc.schedule.entries.filter((e) => e.action !== 'preset' || e.preset !== p.id);
        if (doc.active === p.id) doc.active = doc.presets[0].id;
        ed.sel = Math.min(ed.sel, doc.presets.length - 1);
        changed(ed, true);
      }, 'danger')));

  const intervals = CYCLE_S.includes(doc.cycle.interval_s) ? CYCLE_S : [...CYCLE_S, doc.cycle.interval_s].sort((a, b) => a - b);
  const cycleCard = card('Auto-cycle',
    h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: doc.cycle.enabled,
      onchange: (ev) => { doc.cycle.enabled = ev.target.checked; changed(ed, false); } }), 'Step through the presets by itself'),
    field('Every', h('select', { onchange: (ev) => { doc.cycle.interval_s = Number(ev.target.value); changed(ed, false); } },
      intervals.map((s) => h('option', { value: s, selected: s === doc.cycle.interval_s }, cycleLabel(s))))),
    h('p', { class: 'muted small', text: 'Each switch wakes the device: short intervals cost battery.' }));

  const sched = doc.schedule;
  const entry = (e, i) => {
    const at = h('input', { type: 'time', value: e.at, required: true, onchange: (ev) => { e.at = ev.target.value; changed(ed, false); } });
    const days = h('div', { class: 'days' }, DAYS.map((d, bit) => h('label', {}, h('input', { type: 'checkbox',
      checked: (e.days ?? 127) & (1 << bit), onchange: (ev) => {
        e.days = ((e.days ?? 127) & ~(1 << bit)) | (ev.target.checked ? 1 << bit : 0);
        changed(ed, false);
      } }), d)));
    const kind = h('select', { onchange: (ev) => {
      e.action = ev.target.value;
      if (e.action === 'night') { delete e.preset; e.until = e.until || '06:00'; }
      else { delete e.until; e.preset = e.preset || p.id; }
      changed(ed, true);
    } }, h('option', { value: 'preset', selected: e.action === 'preset' }, 'Switch to a preset'),
    h('option', { value: 'night', selected: e.action === 'night' }, 'Night: screen off until'));
    const what = e.action === 'night'
      ? h('input', { type: 'time', value: e.until, onchange: (ev) => { e.until = ev.target.value; changed(ed, false); } })
      : h('select', { onchange: (ev) => { e.preset = ev.target.value; changed(ed, false); } },
        doc.presets.map((q) => h('option', { value: q.id, selected: q.id === e.preset }, q.name)));
    return h('div', { class: 'entry' }, h('div', { class: 'row' }, at, kind, what), days,
      actions(button('Remove', () => { sched.entries.splice(i, 1); changed(ed, true); })));
  };
  const schedCard = card('Schedule',
    h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: sched.enabled,
      onchange: (ev) => { sched.enabled = ev.target.checked; changed(ed, false); } }), 'Run the schedule'),
    h('p', { class: 'muted small', text: 'At each time, on the ticked days, the device switches preset, or turns the ' +
      'screen off for the night. A press on KEY or BOOT shows the screen for a minute.' }),
    sched.entries.map(entry),
    actions(button('Add a time', () => {
      if (sched.entries.length >= 8) return toast('8 times at most');
      sched.entries.push({ at: '22:00', days: 127, action: 'preset', preset: p.id });
      changed(ed, true);
    })));

  ed.saveBar = card(null, ed.saveNote, actions(
    button('Save', () => busy(ed.saveBar, ed.saveNote, async () => {
      ed.doc = await api('PUT', '/api/presets', ed.doc);
      if (!ed.doc.schedule) ed.doc.schedule = { enabled: false, entries: [] };
      ed.dirty = false;
      toast('Presets saved');
      renderPresets(ed);
    }), 'primary'),
    button('Undo changes', () => {
      if (!confirm('Drop the changes that aren\'t saved?')) return; /* the bar floats over other buttons */
      presetsPage().catch(() => {});
    })));
  ed.saveBar.hidden = !ed.dirty;
  ed.saveBar.style.position = 'sticky';
  ed.saveBar.style.bottom = '8px';
  const y = window.scrollY; /* a re-render keeps the place on the page */
  main.replaceChildren(h('h1', { text: 'Presets' }), listCard, editCard, cycleCard, schedCard, ed.saveBar);
  window.scrollTo(0, y);
}

/* ---- Firmware (spec §10.5) ---- */

function waitForRestart(text) {
  document.getElementById('nav').hidden = true;
  document.getElementById('done').hidden = true;
  const note = h('p', { class: 'muted', text: 'Waiting for the device…' });
  main.replaceChildren(card(text, h('p', { text: 'The device restarts and turns Wi-Fi on again. If this phone ' +
    'doesn\'t rejoin its network by itself, join it again.' }), note));
  (async () => {
    await sleep(8000);
    for (const until = Date.now() + 120000; Date.now() < until; await sleep(2000)) {
      try {
        await api('GET', '/api/auth');
        location.reload();
        return;
      } catch (e) { /* not back yet */ }
    }
    note.textContent = 'No answer yet. Once the device shows its Wi-Fi screen, reload this page.';
  })();
}

function upload(file, progress) {
  return new Promise((resolve, reject) => {
    const x = new XMLHttpRequest();
    x.open('POST', '/api/ota');
    x.setRequestHeader('Content-Type', 'application/octet-stream');
    x.upload.onprogress = (e) => e.lengthComputable && (progress.value = e.loaded / e.total);
    x.onload = () => {
      let d = {};
      try { d = JSON.parse(x.responseText); } catch (e) { /* not JSON */ }
      if (x.status === 200) resolve(d);
      else reject(new ApiError(sentence(d.error || x.statusText), x.status));
    };
    x.onerror = () => reject(new ApiError('The upload broke off.', 0));
    x.send(file);
  });
}

async function firmwarePage() {
  const o = await api('GET', '/api/ota/status');
  const file = h('input', { type: 'file', accept: '.bin,application/octet-stream' });
  const progress = h('progress', { max: 1, value: 0, hidden: true });
  const note = h('p');
  const upCard = card('Update', h('p', { class: 'muted small', text: 'Pick reflbo.bin from a build. The device ' +
    'checks that it is reflbo firmware for this chip, writes it next to the running one and restarts into it. ' +
    'If the new firmware doesn\'t run properly for a minute, the device goes back to this one.' }),
  file, progress, note, actions(button('Update', () => busy(upCard, note, async () => {
    const f = file.files[0];
    if (!f) throw new ApiError('Pick a file first.');
    const head = new Uint8Array(await f.slice(0, 1).arrayBuffer());
    if (f.size < 65536 || head[0] !== 0xe9) throw new ApiError('That is not an ESP32 firmware image.');
    progress.hidden = false;
    const r = await upload(f, progress);
    waitForRestart(`Updated to ${r.version}`);
  }), 'primary')));
  main.replaceChildren(h('h1', { text: 'Firmware' }),
    card('Running', facts([
      ['Version', o.version], ['Built', o.built], ['Slot', o.partition],
      o.pending ? ['State', 'New: it proves itself during its first minute'] : null,
    ]), o.rolled_back_from ? h('p', { class: 'bad', text: `An update in ${o.rolled_back_from} didn't work, so the ` +
      'device went back to this version.' }) : null),
    upCard);
}

/* ---- Backup (spec §14.4) ---- */

async function backupPage() {
  const note = h('p');
  const file = h('input', { type: 'file', accept: '.json,application/json' });
  const saveCard = card('Back up', h('p', { class: 'muted small', text: 'Settings and presets in one file. Wi-Fi ' +
    'passwords and the web password are not in it.' }), actions(button('Download backup', async () => {
    const backup = await api('GET', '/api/backup'); /* parsed, as it is JSON: a file again */
    download(new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' }),
      `${auth.host}-${new Date().toISOString().slice(0, 10)}.json`);
  }, 'primary')));
  const restoreCard = card('Restore', file, note, actions(button('Restore', () => busy(restoreCard, note, async () => {
    const f = file.files[0];
    if (!f) throw new ApiError('Pick a backup file first.');
    let bundle;
    try { bundle = JSON.parse(await f.text()); } catch (e) { throw new ApiError('That file is not JSON.'); }
    const r = await api('POST', '/api/restore', bundle);
    note.className = 'good';
    note.textContent = 'Restored.' + (r.skipped.length ? ` Left out, for a later firmware: ${r.skipped.join(', ')}.` : '');
  }))));
  const resetCard = card('Factory reset', h('p', { class: 'muted small', text: 'Erases the settings, presets, saved ' +
    'Wi-Fi networks and the web password, then restarts. The device then starts like new.' }),
  actions(button('Factory reset…', async () => {
    if (!confirm('Erase everything on the device and restart it?')) return;
    await api('POST', '/api/factory-reset');
    document.getElementById('nav').hidden = true;
    document.getElementById('done').hidden = true;
    main.replaceChildren(card('Erased', h('p', { text: 'The device restarts with its first-run screen.' })));
  }, 'danger')));
  main.replaceChildren(h('h1', { text: 'Backup' }), saveCard, restoreCard, resetCard);
}

start();
