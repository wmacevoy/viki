/* Fixture tests for the reader's EXTRACTION LOGIC.
 *
 * WHAT THIS PROVES AND WHAT IT CANNOT. It proves the status machine is right:
 * that a missing anchor yields `blind` rather than zero items, that a sign-in
 * wall is distinguished from broken markup, that an empty-but-healthy page
 * yields `ok` with zero items, and that fingerprints dedupe. Those are the
 * properties that make a broken scraper visible instead of quietly calm, and
 * they are worth testing precisely because they only matter on the day
 * something breaks.
 *
 * It CANNOT prove the selectors match real Facebook or Discord. Nothing short
 * of a logged-in browser can, which is what edge/chrome/diagnose.js is for.
 * A fixture I wrote from my own guess about their markup would test my guess
 * against itself -- the exact vacuous-test failure this project keeps catching
 * elsewhere -- so the fixtures below are deliberately SHAPED like the real
 * thing rather than claimed to BE it.
 *
 * Run: node edge/chrome/test/fixtures.mjs
 */

/* ---- a DOM small enough to read, big enough for these extractors ---- */
class El {
  constructor(tag, attrs = {}, text = '', kids = []) {
    this.tag = tag; this.attrs = attrs; this._text = text; this.kids = kids;
  }
  get textContent() {
    return this._text + this.kids.map(k => ' ' + k.textContent).join('');
  }
  get innerText() { return this.textContent; }
  matches(sel) {
    // supports: tag, [attr], [attr="v"], [attr^="v"], [attr*="v"], tag[attr...]
    const m = sel.match(/^([a-z]*)(?:\[([a-zA-Z-]+)(?:([\^*]?=)"([^"]*)")?\])?$/);
    if (!m) return false;
    const [, tag, attr, op, val] = m;
    if (tag && tag !== this.tag) return false;
    if (!attr) return !!tag;
    const have = this.attrs[attr];
    if (have === undefined) return false;
    if (!op) return true;
    if (op === '=') return have === val;
    if (op === '^=') return have.startsWith(val);
    if (op === '*=') return have.includes(val);
    return false;
  }
  querySelectorAll(sel) {
    const out = [];
    for (const s of sel.split(',').map(x => x.trim())) {
      const walk = n => { if (n !== this && n.matches(s)) out.push(n); n.kids.forEach(walk); };
      walk(this);
    }
    return out;
  }
  querySelector(sel) { return this.querySelectorAll(sel)[0] || null; }
  /* attachShadow, small enough to reason about: a shadow root is just another
   * El whose children are hidden from the light-DOM query above. */
  attachShadow(kids) { this.shadowRoot = new El('#shadow', {}, '', kids); return this.shadowRoot; }
  /* deepText walks childNodes looking for nodeType 3, so this shim's own text
   * has to appear as one -- otherwise every element reads as empty and the
   * test fails for a reason that has nothing to do with the code under test. */
  get childNodes() {
    return this._text ? [{ nodeType: 3, nodeValue: this._text }, ...this.kids] : this.kids;
  }
  get nodeType() { return 1; }
  get tagName() { return this.tag.toUpperCase(); }
}

function mount(root, pathname = '/channels/@me/1098287202703790190') {
  globalThis.document = {
    querySelector: s => root.querySelector(s),
    querySelectorAll: s => root.querySelectorAll(s)
  };
  globalThis.location = { pathname, search: '' };
  const sent = [];
  globalThis.chrome = { runtime: { sendMessage: m => sent.push(m) } };
  return sent;
}

/* the extension's own helpers, inlined so the test exercises the real logic */
const VIKI = {
  findAll(root, sel) { try { return Array.from((root || document).querySelectorAll(sel)); } catch { return []; } },
  text(el) { return el ? (el.innerText || '').replace(/\s+/g, ' ').trim() : ''; },
  fingerprint(source, text) {
    let h = 5381; const s = source + ' ' + text;
    for (let i = 0; i < s.length; i++) h = ((h << 5) + h + s.charCodeAt(i)) | 0;
    return source + ':' + (h >>> 0).toString(36);
  },
  report(source, status, items, note) {
    chrome.runtime.sendMessage({
      kind: 'viki-observation', source, status, note: note || '',
      items: (items || []).filter(Boolean).map(t => ({ fp: VIKI.fingerprint(source, t), text: t })),
      at: '2026-08-24T00:00:00Z'
    });
  }
};
VIKI._misses = {};
VIKI.settle = (source, found, threshold) => {
  if (found) { VIKI._misses[source] = 0; return true; }
  VIKI._misses[source] = (VIKI._misses[source] || 0) + 1;
  return VIKI._misses[source] >= (threshold || 3);
};
globalThis.VIKI = VIKI;

/* ---- the extractors, as the real files define them ---- */
const FB_LOGIN = ['/login','/checkpoint','/recover'];
const atLoginUrl = pats => { const u = location.pathname + (location.search||'');
  return pats.some(p => u.indexOf(p) >= 0); };
function facebookScan() {
  const ours = location.pathname.indexOf('/notifications') === 0 || atLoginUrl(FB_LOGIN);
  if (!ours) return;
  if (atLoginUrl(FB_LOGIN) || document.querySelector('form[action*="login"], input[name="pass"]')) {
    return VIKI.report('facebook', 'loggedout', [], 'signed out -- redirected to ' + location.pathname);
  }
  let rows = VIKI.findAll(document, '[role="listitem"]');
  if (rows.length === 0) rows = VIKI.findAll(document, 'a[href*="/notifications/"]');
  if (rows.length === 0) {
    if (VIKI.settle('facebook', false)) VIKI.report('facebook','blind',[],'no [role=listitem] rows on /notifications -- markup probably changed');
    return;
  }
  VIKI.settle('facebook', true);
  const items = rows.map(r => VIKI.text(r))
    .map(t => t.replace(/^Unread\s+/, ''))
    .filter(t => t && t.length > 20 && t.split(' ').length > 3).slice(0, 50);
  VIKI.report('facebook', 'ok', items, items.length + ' row(s) on /notifications');
}

const DC_LOGIN = ['/login','/register'];
function discordScan() {
  if (atLoginUrl(DC_LOGIN) || document.querySelector('input[name="email"], input[name="password"]')) {
    return VIKI.report('discord', 'loggedout', [], 'signed out -- at ' + location.pathname);
  }
  if (location.pathname.indexOf('/channels/') !== 0) return;
  if (/^\/channels\/@me\/?$/.test(location.pathname)) return;
  const list = document.querySelector('[data-list-id="chat-messages"]')
            || document.querySelector('[data-list-id^="chat-messages"]')
            || document.querySelector('main [role="list"][aria-label*="essages" i]');
  if (!list) { if (VIKI.settle('discord', false)) VIKI.report('discord','blind',[],'no chat-messages list -- markup probably changed'); return; }
  let rows = VIKI.findAll(list, 'li[id^="chat-messages-"]');
  if (rows.length === 0) rows = VIKI.findAll(list, '[role="listitem"]');
  if (rows.length === 0) { if (VIKI.settle('discord', false)) VIKI.report('discord','blind',[],'message list found but no rows'); return; }
  VIKI.settle('discord', true);
  const mine = rows.filter(r => r.querySelector('[class*="mention"]') || /\B@\w/.test(VIKI.text(r).slice(0, 400)));
  const items = mine.map(r => VIKI.text(r)).filter(t => t && t.length > 12).slice(0, 30);
  VIKI.report('discord', 'ok', items, rows.length + ' message(s) on screen, ' + mine.length + ' mentioning you');
}

/* ---- assertions ---- */
let pass = 0, fail = 0;
const t = (name, cond) => { cond ? pass++ : fail++; console.log(`  ${cond ? 'PASS' : 'FAIL'}  ${name}`); };

console.log('== facebook ==');
{
  /* SHAPED FROM THE LIVE PAGE, 2026-08-24: rows sit in a plain DIV with NO
   * [role=main] and NO <main> anywhere, each prefixed "Unread". Measured on a
   * logged-in account: 30 rows, 28 real, 2 chrome. */
  const root = new El('body', {}, '', [
    new El('div', {}, '', [
      new El('div', { role: 'listitem' }, 'New'),                    // chrome
      new El('div', { role: 'listitem' }, 'Unread Karl commented on your post about the fence line. 4d'),
      new El('div', { role: 'listitem' }, 'Unread Sara invited you to the roundup on Saturday. 1d'),
      new El('div', { role: 'listitem' }, 'All')                     // chrome
    ])
  ]);
  const sent = mount(root, '/notifications');
  VIKI._misses = {};
  facebookScan();
  const m = sent[0];
  t('F1 a healthy page reports ok WITHOUT any [role=main]', m.status === 'ok');
  t('F2 real rows are captured, nav chrome is dropped', m.items.length === 2);
  t('F2b the "Unread" state prefix is stripped, so a read row still matches',
    m.items.every(i => !/^Unread/.test(i.text)));
  t('F3 fingerprints are distinct per item', m.items[0].fp !== m.items[1].fp);
}
{
  const sent = mount(new El('body', {}, '', [new El('div', {})]), '/notifications');
  VIKI._misses = {};
  facebookScan(); facebookScan(); facebookScan();
  t('F4 page present but NO listitem rows is BLIND, not zero items', sent[0] && sent[0].status === 'blind');
}
{
  const sent = mount(new El('body', {}, '', [new El('div', { id: 'x' })]), '/notifications');
  VIKI._misses = {};
  facebookScan(); facebookScan(); facebookScan();
  t('F5 anchor gone entirely is BLIND', sent[0] && sent[0].status === 'blind');
}
{
  const sent = mount(new El('body', {}, '', [new El('input', { name: 'pass' })]), '/notifications');
  facebookScan();
  t('F6 a sign-in wall is LOGGEDOUT, distinguished from blind', sent[0].status === 'loggedout');
}
{
  const sent = mount(new El('body', {}, '', [new El('div', { role: 'main' })]), '/feed');
  VIKI._misses = {};
  facebookScan();
  t('F7 CONTROL: off the notifications page it stays silent', sent.length === 0);
}

console.log('== discord ==');
{
  const root = new El('body', {}, '', [
    new El('div', { 'data-list-id': 'chat-messages' }, '', [
      new El('li', { id: 'chat-messages-1' }, 'hey @warren can you bring the trailer Saturday'),
      new El('li', { id: 'chat-messages-2' }, 'unrelated chatter about the weather today'),
      new El('li', { id: 'chat-messages-3' }, 'and another thing entirely', [
        new El('span', { class: 'mention-3f2' }, '@warren')
      ])
    ])
  ]);
  const sent = mount(root);
  discordScan();
  const m = sent[0];
  t('D1 a healthy channel reports ok', m.status === 'ok');
  t('D2 ONLY messages mentioning you are captured', m.items.length === 2);
  t('D3 CONTROL: unrelated chatter is not captured',
    !m.items.some(i => i.text.includes('weather')));
  t('D4 a mention span counts, not just a literal @', m.items.some(i => i.text.includes('another thing')));
}
{
  const root = new El('body', {}, '', [
    new El('div', { 'data-list-id': 'chat-messages' }, '', [
      new El('li', { id: 'chat-messages-1' }, 'nobody is talking to you right now at all')
    ])
  ]);
  const sent = mount(root);
  discordScan();
  t('D5 a quiet channel is ok-with-zero, NOT blind', sent[0].status === 'ok' && sent[0].items.length === 0);
}
{
  const sent = mount(new El('body', {}, '', [new El('div', { id: 'x' })]));
  VIKI._misses = {};
  discordScan(); discordScan(); discordScan();
  t('D6 anchor gone is BLIND, and that is the whole point', sent[0] && sent[0].status === 'blind');
}
{
  const sent = mount(new El('body', {}, '', [new El('input', { name: 'email' })]));
  discordScan();
  t('D7 a sign-in wall is LOGGEDOUT', sent[0].status === 'loggedout');
}

console.log('== the redirect cases, found by loading it in Chrome ==');
{
  /* MEASURED 2026-08-24: a signed-out visit to facebook.com/notifications
   * redirects to /login.php. The first version guarded on being AT
   * /notifications, so it went silent -- false calm, the one failure this
   * reader exists to prevent. */
  const sent = mount(new El('body', {}, '', [new El('div', { id: 'x' })]), '/login.php');
  facebookScan();
  t('F8 a facebook LOGIN REDIRECT reports loggedout, not silence',
    sent.length === 1 && sent[0].status === 'loggedout');
}
{
  /* MEASURED: /channels/@me redirects to /login when signed out, and the
   * manifest originally matched only /channels/*, so the script never ran. */
  const sent = mount(new El('body', {}, '', [new El('div', { id: 'x' })]), '/login');
  discordScan();
  t('D8 a discord LOGIN REDIRECT reports loggedout, not silence',
    sent.length === 1 && sent[0].status === 'loggedout');
}
{
  /* MEASURED: three seconds after navigating into Discord the message list did
   * not exist AND the login form had not rendered. The first version called
   * that `blind` on every single page load. Uses a real CHANNEL path -- on
   * /channels/@me (Friends) silence is correct for a different reason and the
   * test would pass vacuously. */
  const sent = mount(new El('body', {}, '', [new El('div', { id: 'app-mount' })]),
                     '/channels/@me/1098287202703790190');
  VIKI._misses = {};
  discordScan();
  t('D9 a mid-load transient is NOT blind on the first miss', sent.length === 0);
  discordScan(); discordScan();
  t('D10 ...but a PERSISTENT miss still reports blind',
    sent.length === 1 && sent[0].status === 'blind');
}

{
  /* MEASURED LIVE 2026-08-24: /channels/@me is the FRIENDS page. The old
   * fallback `main [role="list"]` matched the friends roster -- 16 rows of
   * "Bob Kramer Idle Message More" -- and hunted mentions in it. It captured
   * nothing only because no friend's name contained an "@". */
  const root = new El('body', {}, '', [
    new El('main', {}, '', [
      new El('div', { role: 'list' }, '', [
        new El('div', { role: 'listitem' }, 'Bob Kramer Idle Message More'),
        new El('div', { role: 'listitem' }, '@danny Online Message More')
      ])
    ])
  ]);
  const sent = mount(root, '/channels/@me');       /* no channel id: Friends */
  VIKI._misses = {};
  discordScan(); discordScan(); discordScan();
  t('D11 the FRIENDS page is silence, not a message list and not blind', sent.length === 0);
}

console.log('== shadow DOM (D2L) ==');
{
  /* Both bugs below were found live on d2l.coloradomesa.edu, 2026-08-24, and
   * both are SILENT: each one reports an empty page rather than an error. */
  const deepAll = (sel, root, out, seen) => {
    out = out || []; seen = seen || new Set(); root = root || document;
    if (seen.has(root)) return out; seen.add(root);
    if (root.shadowRoot) deepAll(sel, root.shadowRoot, out, seen);
    try { root.querySelectorAll(sel).forEach(e => out.push(e)); } catch (_) {}
    let all = []; try { all = root.querySelectorAll('*'); } catch (_) {}
    for (const e of all) if (e.shadowRoot) deepAll(sel, e.shadowRoot, out, seen);
    return out;
  };
  const deepText = (node, depth) => {
    depth = depth || 0;
    if (!node || depth > 12) return '';
    if (node.nodeType === 3) return node.nodeValue || '';
    if (node.nodeType !== 1) return '';
    const tag = node.tagName;
    if (tag === 'STYLE' || tag === 'SCRIPT' || tag === 'TEMPLATE') return '';
    let s = '';
    if (node.shadowRoot) for (const c of node.shadowRoot.childNodes) s += ' ' + deepText(c, depth + 1);
    for (const c of node.childNodes) s += ' ' + deepText(c, depth + 1);
    return s.replace(/\s+/g, ' ').trim();
  };

  /* A table whose rows live in its OWN shadow root -- exactly D2L's
   * d2l-quick-eval-submissions-table. The pre-fix deepAll found 0 of 20. */
  const table = new El('d2l-quick-eval-submissions-table');
  table.attachShadow([
    new El('table', {}, '', [ new El('tbody', {}, '', [
      new El('tr', {}, 'Noah Foli repo CSCI365-001-21618 Data Mining 8/20/2026 9:38 AM'),
      new El('tr', {}, 'Ethan Talbert repo CSCI365-001-21618 Data Mining 8/20/2026 9:41 AM')
    ])])
  ]);
  const root = new El('body', {}, '', [table]);
  mount(root, '/d2l/le/12904/quickeval/');
  /* 'tr' rather than 'tbody tr': this shim's matches() handles one simple
   * selector, not combinators. The property under test is the shadow descent,
   * not the selector engine. */
  const found = deepAll('tr', table);
  t('S1 deepAll descends into the ROOT element\'s own shadow root', found.length === 2);
  t('S2 rows carry learner, activity, course and date',
    /CSCI365.*8\/20\/2026/.test(deepText(found[0])));

  /* A component whose text is inside its own shadow root AND whose shadow root
   * begins with a <style> block -- both true of d2l-activity-name. The first
   * attempt extracted ":host { display: block; }..." as a notification. */
  const name = new El('d2l-activity-name');
  name.attachShadow([
    new El('style', {}, ':host { display: block; } .icon { color: red; }'),
    new El('span', {}, 'repo')
  ]);
  t('S3 deepText reads through a shadow root', deepText(name) === 'repo');
  t('S4 ...and does NOT return the component CSS', !/host|display/.test(deepText(name)));
}

console.log('== dedupe ==');
{
  const a = VIKI.fingerprint('discord', 'bring the trailer');
  const b = VIKI.fingerprint('discord', 'bring the trailer');
  const c = VIKI.fingerprint('facebook', 'bring the trailer');
  t('X1 the same text on the same source fingerprints identically', a === b);
  t('X2 the same text on a DIFFERENT source does not collide', a !== c);
}

console.log(`\nPASS=${pass} FAIL=${fail}`);
process.exit(fail ? 1 : 0);
