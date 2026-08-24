/* viki reader -- shared extraction contract.
 *
 * OBSERVE ONLY, AND BY CONSTRUCTION RATHER THAN BY PROMISE. Nothing in this
 * extension calls .click(), .submit(), dispatches an input event, or writes to
 * any page. It reads the DOM of pages Warren is already logged into and already
 * looking at. That is VIKIVERSE_V1 2.2c's lowest rung -- `observe` -- and the
 * only rung that needs no policy settled first, because rendering your own
 * screen is what a browser already does on your behalf.
 *
 * A SCRAPER MUST BE ABLE TO TELL "NOTHING THERE" FROM "I CANNOT SEE ANY MORE".
 * That is the whole design. Facebook and Discord ship obfuscated, frequently
 * rebuilt markup, so these selectors WILL break -- and a scraper that silently
 * returns zero items is indistinguishable from a quiet day. That failure would
 * launder breakage into false calm, which is exactly the coverage lie 2.5
 * forbids and the precise opposite of what the product is for.
 *
 * So every extractor reports a STATUS, not just items:
 *   ok         the anchor was found; n items read (n may legitimately be 0)
 *   blind      the anchor was NOT found -- markup changed, or wrong page
 *   loggedout  a sign-in wall
 * `blind` propagates all the way to the ledger's coverage line.
 */

const VIKI = {
  /* Anchors come from ROLE and ARIA, never class names. Class names on these
   * sites are generated per build and change weekly; the screenreader contract
   * changes rarely, because real people depend on it. */
  findAll(root, selector) {
    try { return Array.from((root || document).querySelectorAll(selector)); }
    catch (_) { return []; }
  },

  /* SHADOW-DOM-PIERCING SEARCH, and D2L cannot be read without it.
   * Measured live on d2l.coloradomesa.edu, 2026-08-24: 63 OPEN shadow roots,
   * `document.body.innerText` yielding 364 characters for a full page, and a
   * flat `querySelectorAll('a[href]')` finding 19 links where a piercing walk
   * finds 84. A scraper that does not descend sees a page that looks almost
   * empty -- and would report it as a quiet day.
   *
   * Open roots only. A CLOSED root is genuinely unreachable from script, and
   * the honest response to one is `blind`, not a workaround.
   *
   * The `seen` set guards against a cycle through slotted content, which would
   * otherwise recurse until the stack gives out. */
  deepAll(selector, root, out, seen) {
    out = out || []; seen = seen || new Set(); root = root || document;
    if (seen.has(root)) return out;
    seen.add(root);
    /* THE ROOT'S OWN SHADOW ROOT COUNTS. Missing this is subtle and silent:
     * given `deepAll('tbody tr', table)` where `table` is a custom element
     * whose content lives in its own shadow root, the light-DOM query returns
     * nothing, the `*` walk below also returns nothing (there are no light
     * children to walk), and the function reports zero rows from a table full
     * of them. Measured on D2L Quick Eval: 0 rows found where 20 exist. */
    if (root.shadowRoot) VIKI.deepAll(selector, root.shadowRoot, out, seen);
    try { root.querySelectorAll(selector).forEach(e => out.push(e)); } catch (_) {}
    let all = [];
    try { all = root.querySelectorAll('*'); } catch (_) {}
    for (const e of all) if (e.shadowRoot) VIKI.deepAll(selector, e.shadowRoot, out, seen);
    return out;
  },

  deepOne(selector, root) {
    return VIKI.deepAll(selector, root)[0] || null;
  },

  /* Text that descends into an element's OWN shadow root.
   *
   * deepAll finds the element; this is what reads it, and the distinction is
   * easy to miss because the first half appears to work. Measured on D2L:
   * every `d2l-activity-name` was located correctly and every one returned ""
   * from innerText, because its text lives inside its own shadow root.
   *
   * STYLE/SCRIPT/TEMPLATE are skipped or the "text" comes back as the
   * component's CSS -- the first attempt extracted
   * ":host { display: block; } .d2l-activity-name-icon {..." as a notification. */
  deepText(node, depth) {
    depth = depth || 0;
    if (!node || depth > 12) return '';
    if (node.nodeType === 3) return node.nodeValue || '';
    if (node.nodeType !== 1) return '';
    const tag = node.tagName;
    if (tag === 'STYLE' || tag === 'SCRIPT' || tag === 'TEMPLATE') return '';
    let s = '';
    if (node.shadowRoot) {
      for (const c of node.shadowRoot.childNodes) s += ' ' + VIKI.deepText(c, depth + 1);
    }
    for (const c of node.childNodes) s += ' ' + VIKI.deepText(c, depth + 1);
    return s.replace(/\s+/g, ' ').trim();
  },

  text(el) {
    if (!el) return '';
    return (el.innerText || el.textContent || '').replace(/\s+/g, ' ').trim();
  },

  /* Stable identity for an item, so the same notification is not captured on
   * every poll. Content hash rather than a DOM id: the id changes across page
   * loads and the content does not. */
  fingerprint(source, text) {
    let h = 5381;
    const s = source + ' ' + text;
    for (let i = 0; i < s.length; i++) h = ((h << 5) + h + s.charCodeAt(i)) | 0;
    return source + ':' + (h >>> 0).toString(36);
  },

  report(source, status, items, note) {
    try {
      chrome.runtime.sendMessage({
        kind: 'viki-observation',
        source, status, note: note || '',
        items: (items || []).filter(Boolean).map(t => ({
          fp: VIKI.fingerprint(source, t),
          text: t
        })),
        at: new Date().toISOString()
      });
    } catch (_) { /* the worker may be asleep; the next poll will retry */ }
  },

  /* AN SPA THAT HAS NOT RENDERED YET LOOKS EXACTLY LIKE BROKEN MARKUP.
   * Measured on a live Discord load: three seconds after navigation the URL
   * was still /channels/@me, the message list did not exist, and the login
   * form had not rendered either -- a state that reports `blind` and would cry
   * wolf on every single page load.
   *
   * So `blind` requires CONSECUTIVE misses. A first miss is "not ready"; only
   * a persistent one is "I cannot see any more". `ok` and `loggedout` report
   * immediately, because those are positive findings and waiting on them would
   * just delay good news. */
  _misses: {},
  settle(source, found, threshold) {
    if (found) { VIKI._misses[source] = 0; return true; }
    VIKI._misses[source] = (VIKI._misses[source] || 0) + 1;
    return VIKI._misses[source] >= (threshold || 3);
  },

  /* A redirect to a sign-in URL IS the signal, and is more reliable than
   * sniffing for a form: the form is rendered by JS and arrives late, while
   * the URL changes at once. Checked FIRST for that reason. */
  atLoginUrl(patterns) {
    const u = location.pathname + location.search;
    return patterns.some(p => u.indexOf(p) >= 0);
  },

  /* Poll rather than MutationObserver. An observer fires hundreds of times a
   * second on these pages and would turn a reader into a load generator on
   * Warren's own machine; a slow poll is enough for something whose output is
   * read once a morning. */
  every(ms, fn) {
    const run = () => { try { fn(); } catch (e) { console.warn('viki reader:', e); } };
    run();
    setInterval(run, ms);
  }
};
