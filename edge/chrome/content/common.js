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
