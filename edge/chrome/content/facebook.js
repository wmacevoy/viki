/* Facebook notifications, observe only.
 *
 * READS ONE PAGE: /notifications. It does not crawl, does not open posts, does
 * not follow links, and does not touch the feed -- the feed is an infinite
 * ranked surface and reading it would be building a second timeline rather than
 * catching promises. Notifications are the bounded, addressed-to-you subset,
 * which is the only part that can contain something owed.
 *
 * THE ANCHOR IS `[role="main"]` PLUS `[role="listitem"]`, not a class name.
 * Facebook's class names are per-build hashes; the role attributes exist for
 * screenreaders and therefore change far more slowly. When they do change this
 * reports `blind` rather than zero -- see common.js.
 */

(function () {
  const SOURCE = 'facebook';

  /* A signed-out visit to /notifications REDIRECTS to /login.php -- measured
   * live, 2026-08-24. The first version guarded on being at /notifications and
   * therefore went SILENT in exactly that case, which is false calm: the one
   * failure this reader exists to prevent. Login URLs are checked first and
   * count as ours. */
  const LOGIN_URLS = ['/login', '/checkpoint', '/recover'];

  function ourPage() {
    return location.pathname.indexOf('/notifications') === 0
        || VIKI.atLoginUrl(LOGIN_URLS);
  }

  function loggedOut() {
    /* URL first: a redirect is immediate, while the form is rendered by JS and
     * arrives late enough to be missed. */
    return VIKI.atLoginUrl(LOGIN_URLS)
        || !!document.querySelector('form[action*="login"], input[name="pass"]');
  }

  function scan() {
    if (!ourPage()) return;      /* silent: not our page, and not a failure */

    if (loggedOut()) {
      VIKI.report(SOURCE, 'loggedout', [], 'signed out -- redirected to ' + location.pathname);
      return;
    }

    const main = document.querySelector('[role="main"]');
    if (!main) {
      /* Not ready and not-there look identical for the first few polls. */
      if (VIKI.settle(SOURCE, false)) {
        VIKI.report(SOURCE, 'blind', [], 'no [role=main] on /notifications');
      }
      return;
    }

    let rows = VIKI.findAll(main, '[role="listitem"]');
    if (rows.length === 0) {
      /* Fall back once to any link-bearing row, then give up LOUDLY. Two
       * anchors rather than one because a single selector is a single point of
       * silent failure; more than two would be guessing. */
      rows = VIKI.findAll(main, 'a[href*="/notifications/"]');
    }
    if (rows.length === 0) {
      if (VIKI.settle(SOURCE, false)) {
        VIKI.report(SOURCE, 'blind', [],
          'main region found but no listitem rows -- markup probably changed');
      }
      return;
    }
    VIKI.settle(SOURCE, true);

    const items = rows
      .map(r => VIKI.text(r))
      /* Drop chrome: single words, timestamps alone, and anything too short to
       * carry a commitment. */
      .filter(t => t && t.length > 12 && t.split(' ').length > 2)
      .slice(0, 50);

    VIKI.report(SOURCE, 'ok', items, items.length + ' row(s) on /notifications');
  }

  VIKI.every(90000, scan);
})();
