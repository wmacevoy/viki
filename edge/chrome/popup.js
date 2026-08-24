/* The popup exists to answer one question: WHAT CAN THIS SEE RIGHT NOW.
 *
 * Not a settings panel. A source that has gone `blind` -- markup changed, so
 * nothing is being read -- must be visible at a glance, because the failure it
 * causes downstream is a ledger that looks calm while a channel is dark. That
 * is the coverage lie 2.5 forbids, and this is where it surfaces first. */

const SOURCES = ['facebook', 'discord'];

function row(name, s) {
  const d = document.createElement('div');
  d.className = 'src';
  const dot = document.createElement('span');
  dot.className = 'dot ' + (!s ? 'off' : s.status === 'ok' ? 'ok' : 'blind');
  const nm = document.createElement('span');
  nm.className = 'name';
  nm.textContent = name;
  const note = document.createElement('span');
  note.className = 'note';
  if (!s) {
    note.textContent = 'not seen yet — open the site';
  } else if (s.status === 'ok') {
    note.textContent = (s.note || 'reading') + ' · ' + s.at.slice(11, 16);
  } else if (s.status === 'loggedout') {
    note.textContent = 'signed out — nothing readable';
  } else {
    note.textContent = 'BLIND: ' + (s.note || 'page changed') + ' · ' + s.at.slice(11, 16);
  }
  d.append(dot, nm, note);
  return d;
}

async function draw() {
  const d = await chrome.storage.local.get(['sources', 'queue', 'enabled']);
  const sources = d.sources || {};
  const host = document.getElementById('sources');
  host.textContent = '';
  for (const s of SOURCES) host.appendChild(row(s, sources[s]));

  const n = (d.queue || []).length;
  document.getElementById('queue').textContent =
    n ? n + ' waiting for viki' : 'nothing waiting';

  const on = d.enabled !== false;
  document.getElementById('toggle').textContent = on ? 'Pause reading' : 'Resume reading';
}

document.getElementById('toggle').onclick = async () => {
  const d = await chrome.storage.local.get('enabled');
  await chrome.storage.local.set({ enabled: d.enabled === false });
  draw();
};

draw();
