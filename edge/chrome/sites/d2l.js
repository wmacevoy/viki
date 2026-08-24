/* D2L / Brightspace, observe only.
 *
 * TARGETS QUICK EVAL, NOT ANNOUNCEMENTS. Warren: "I almost exclusively use d2l
 * for assignment management in classes." The first version of this file read
 * course announcements, which is the wrong surface entirely -- announcements are
 * things he SENT. Quick Eval is the list of learner submissions awaiting his
 * evaluation, i.e. work he OWES, with a date attached. That is a promise in
 * VIKIVERSE_V1 2.1's sense, and it is the reason to read D2L at all.
 *
 * SHADOW DOM IS THE WHOLE DIFFICULTY, in two separate ways, both measured live
 * on d2l.coloradomesa.edu (2026-08-24):
 *
 *  1. FINDING elements needs a piercing walk. D2L renders through 63 open
 *     shadow roots; a flat querySelectorAll('a[href]') finds 19 links where a
 *     piercing walk finds 84, and document.body.innerText returns 364
 *     characters for a full page.
 *  2. READING them needs a piercing text function too, which is easy to miss
 *     because step 1 appears to work. `d2l-activity-name` elements were found
 *     correctly and every one returned "" from innerText, because their text
 *     lives inside their OWN shadow root. VIKI.deepText descends into it -- and
 *     skips STYLE/SCRIPT, or the extracted "text" is the component's CSS.
 *
 * THE ROW SHAPE, evidence rather than assumption. Climbing from an activity
 * name gives:
 *   d2l-quick-eval > d2l-quick-eval-submissions > d2l-quick-eval-submissions-table
 *     > d2l-table-wrapper > table > tbody > tr > td
 * and one `tr` reads:
 *   "Noah Foli repo CSCI365-001-21618 Data Mining 8/20/2026 9:38 AM"
 * i.e. learner, activity, course code, course name, submitted-at.
 *
 * THESE ROWS CONTAIN STUDENT NAMES. They go to 127.0.0.1 and nowhere else --
 * see background.js, where the destination is hardcoded with no setting. That
 * constraint was written before this extractor existed and is the reason this
 * one is safe to add.
 */

(function () {
  const SOURCE = 'd2l';
  const LOGIN_URLS = ['/adfs/', '/login', '/saml', '/sso', 'cmu_redir'];

  function loggedOut() {
    return VIKI.atLoginUrl(LOGIN_URLS)
        || !!VIKI.deepOne('input[type="password"], form[action*="login" i]');
  }

  function onD2L() {
    return location.hostname.indexOf('d2l.') === 0
        || location.pathname.indexOf('/d2l/') === 0;
  }

  function scan() {
    if (loggedOut()) {
      VIKI.report(SOURCE, 'loggedout', [], 'SSO wall at ' + location.pathname);
      return;
    }
    if (!onD2L()) return;

    /* Quick Eval lives at /d2l/le/<orgUnit>/quickeval/. Anywhere else in D2L is
     * not this extractor's business -- reporting from a course page would be
     * noise, and silence is correct for "you are somewhere else in the app". */
    if (location.pathname.indexOf('/quickeval') < 0) return;

    const table = VIKI.deepOne('d2l-quick-eval-submissions-table');
    if (!table) {
      /* The app is slow: seven seconds was needed before the table existed on a
       * live load, so a first miss is "not ready" rather than "gone". */
      if (VIKI.settle(SOURCE, false)) {
        VIKI.report(SOURCE, 'blind', [],
          'no d2l-quick-eval-submissions-table -- not loaded, or markup changed');
      }
      return;
    }
    VIKI.settle(SOURCE, true);

    const rows = VIKI.deepAll('tbody tr', table);
    const seen = new Set();
    const items = [];
    for (const r of rows) {
      const t = VIKI.deepText(r);
      /* A header or spacer row has almost no text; a real one carries a name,
       * an activity, a course and a timestamp. */
      if (!t || t.length < 20) continue;
      if (seen.has(t)) continue;
      seen.add(t);
      items.push('awaiting evaluation: ' + t.slice(0, 300));
      if (items.length >= 40) break;
    }

    /* Zero here is a REAL answer -- an empty Quick Eval means nothing is
     * waiting to be graded, which is the best possible morning. It must stay
     * distinguishable from `blind`. */
    VIKI.report(SOURCE, 'ok', items,
      rows.length + ' row(s), ' + items.length + ' awaiting evaluation');
  }

  VIKI.every(120000, scan);
})();
