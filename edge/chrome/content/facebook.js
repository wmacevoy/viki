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

  function onNotificationsPage() {
    return location.pathname.indexOf('/notifications') === 0;
  }

  function loggedOut() {
    /* A sign-in wall renders a login form and no main region. Distinguished
     * from `blind` because the fix is different: log in, versus fix a
     * selector. */
    return !!document.querySelector('form[action*="login"], input[name="pass"]');
  }

  function scan() {
    if (!onNotificationsPage()) return;      /* silent: not our page, not a failure */

    if (loggedOut()) {
      VIKI.report(SOURCE, 'loggedout', [], 'sign-in wall on /notifications');
      return;
    }

    const main = document.querySelector('[role="main"]');
    if (!main) {
      VIKI.report(SOURCE, 'blind', [], 'no [role=main] on /notifications');
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
      VIKI.report(SOURCE, 'blind', [],
        'main region found but no listitem rows -- markup probably changed');
      return;
    }

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
