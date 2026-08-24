/* The popup exists to answer one question: WHAT CAN THIS SEE RIGHT NOW.
 *
 * Not a settings panel. A source that has gone `blind` -- markup changed, so
 * nothing is being read -- must be visible at a glance, because the failure it
 * causes downstream is a ledger that looks calm while a channel is dark. That
 * is the coverage lie 2.5 forbids, and this is where it surfaces first. */

/* Read from the manifest rather than hardcoded, so adding a site to sites.json
** and regenerating is the ONLY step. A hardcoded list here was a second place
** the framework knew Warren's sites -- see sites/README.md. */
const SOURCES = (chrome.runtime.getManifest().content_scripts || [])
  .map(cs => (cs.js || []).find(f => f.startsWith('sites/')))
  .filter(Boolean)
  .map(f => f.replace('sites/', '').replace('.js', ''));

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
  /* FRESHNESS, not just status. Under a morning-login model the useful question
   * is not "is it working" but "how long since it actually saw anything" --
   * a channel signed out three days ago is three days of missed promises, and
   * one signed out an hour ago is nothing. */
  const ago = iso => {
    if (!iso) return 'never';
    const mins = Math.round((Date.now() - Date.parse(iso)) / 60000);
    if (mins < 2) return 'just now';
    if (mins < 60) return mins + 'm ago';
    if (mins < 60 * 24) return Math.round(mins / 60) + 'h ago';
    return Math.round(mins / 1440) + 'd ago';
  };
  if (!s) {
    note.textContent = 'never read — sign in and open it';
  } else if (s.status === 'ok') {
    note.textContent = (s.note || 'reading') + ' · ' + ago(s.at);
  } else if (s.status === 'loggedout') {
    note.textContent = 'signed out · last read ' + ago(s.lastOk);
  } else {
    note.textContent = 'BLIND: ' + (s.note || 'page changed') + ' · last read ' + ago(s.lastOk);
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
  /* "These need you to sign in" is itself a promise -- an owner, a due time,
   * and a cost if skipped. Naming the stale ones turns the morning login from
   * an open-ended chore into a bounded list that shrinks on a good day. */
  const stale = SOURCES.filter(name => {
    const s = sources[name];
    if (!s) return true;
    if (s.status === 'ok') return false;
    return !s.lastOk || (Date.now() - Date.parse(s.lastOk)) > 12 * 3600 * 1000;
  });
  document.getElementById('queue').textContent =
    stale.length ? 'sign in: ' + stale.join(', ')
                 : (n ? n + ' waiting for viki' : 'all channels fresh');

  const on = d.enabled !== false;
  document.getElementById('toggle').textContent = on ? 'Pause reading' : 'Resume reading';
}

document.getElementById('toggle').onclick = async () => {
  const d = await chrome.storage.local.get('enabled');
  await chrome.storage.local.set({ enabled: d.enabled === false });
  draw();
};

draw();
