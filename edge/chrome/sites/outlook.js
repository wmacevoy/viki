/* Outlook on the web, observe only.  WARREN'S SITE, NOT VIKI'S -- see
 * sites/README.md for why that distinction is enforced by the directory.
 *
 * Reached through the campus redirector `cmu_redir/outlook`, which lands on
 * `outlook.cloud.microsoft/mail/`. The redirector is matched too, so an SSO
 * bounce is reported rather than silent.
 *
 * THE ANCHOR IS THE ACCESSIBLE NAME, not a class or a data attribute. Measured
 * live 2026-08-24: the message list is `[aria-label="Message list"]`, and
 * Outlook's generated class names are meaningless. The accessible name is a
 * contract Microsoft keeps because screenreader users depend on it.
 *
 * WHAT WENT WRONG WHILE FINDING IT, recorded because the same trap will recur:
 * the page has 23 `div[draggable="true"]` elements, which look exactly like
 * message rows and are the FOLDER TREE -- they sit under `div[role="tree"]`,
 * not under the message list. An extractor anchored on "draggable" would have
 * silently reported folders as mail. Scope to the labelled list, then read
 * inside it; never pattern-match on a behaviour attribute.
 *
 * ROWS WERE NOT OBSERVED. The message list was empty at probe time, so the row
 * selector below is inferred from the list's own accessible structure rather
 * than measured. It is a GUESS, and it is marked as one: if it is wrong this
 * reports `blind` in the popup instead of a quiet inbox, which is the whole
 * point of that machinery.
 */

(function () {
  const SOURCE = 'outlook';
  const LOGIN_URLS = ['/login', '/signin', '/oauth2', 'cmu_redir', 'login.microsoftonline'];

  function loggedOut() {
    return VIKI.atLoginUrl(LOGIN_URLS)
        || !!VIKI.deepOne('input[type="password"], input[name="loginfmt"]');
  }

  function scan() {
    if (loggedOut()) {
      VIKI.report(SOURCE, 'loggedout', [], 'sign-in at ' + location.pathname);
      return;
    }
    if (location.pathname.indexOf('/mail') < 0) return;   /* calendar/people: not this reader */

    const list = VIKI.deepOne('[aria-label="Message list"]')
              || VIKI.deepOne('[role="listbox"][aria-label*="essage" i]');
    if (!list) {
      if (VIKI.settle(SOURCE, false)) {
        VIKI.report(SOURCE, 'blind', [], 'no [aria-label="Message list"] -- markup changed');
      }
      return;
    }
    VIKI.settle(SOURCE, true);

    /* UNVERIFIED: the message list was empty when this was written, so these
     * two selectors are inferred from the listbox role rather than observed.
     * Deliberately scoped INSIDE `list` so the folder tree cannot leak in. */
    let rows = VIKI.deepAll('[role="option"]', list);
    if (rows.length === 0) rows = VIKI.deepAll('[role="listitem"]', list);

    const seen = new Set();
    const items = [];
    for (const r of rows) {
      /* The accessible name of a row carries sender, subject and time in one
       * string, which is exactly what a capture wants. Falling back to text
       * because the label is not guaranteed. */
      const t = (r.getAttribute && r.getAttribute('aria-label')) || VIKI.deepText(r);
      if (!t || t.length < 20 || seen.has(t)) continue;
      seen.add(t);
      items.push(t.slice(0, 300));
      if (items.length >= 30) break;
    }

    /* An empty inbox is a real and good answer; it must not read as `blind`. */
    VIKI.report(SOURCE, 'ok', items,
      rows.length + ' row(s) in the message list, ' + items.length + ' captured');
  }

  VIKI.every(120000, scan);
})();
