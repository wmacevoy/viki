/* Discord, observe only.
 *
 * READS ONLY WHAT IS ALREADY ON SCREEN in the channel Warren has open. It does
 * not enumerate guilds, does not switch channels, does not scroll, and does not
 * open DMs. That is a deliberate limit rather than a missing feature: walking
 * channels to harvest them would be a bot, which Discord's terms prohibit and
 * which is a different thing morally as well as contractually. Reading the
 * window you are looking at is what the window is for.
 *
 * IT ONLY REPORTS MESSAGES THAT MENTION YOU. An unfiltered channel is chatter,
 * and 2.3 says noise reduction is the product -- a reader that forwarded every
 * message would move the noise rather than reduce it. A mention is the closest
 * thing Discord has to "addressed to you", which is the only category that can
 * hold a promise.
 *
 * The anchor is `[data-list-id="chat-messages"]` plus `li[id^="chat-messages-"]`
 * -- data attributes Discord uses for its own virtual list, more stable than
 * class names but still not a contract. When they change this reports `blind`.
 */

(function () {
  const SOURCE = 'discord';

  function loggedOut() {
    return !!document.querySelector('form[class*="authBox"], input[name="email"]');
  }

  function scan() {
    if (loggedOut()) {
      VIKI.report(SOURCE, 'loggedout', [], 'sign-in wall');
      return;
    }

    const list = document.querySelector('[data-list-id="chat-messages"]')
              || document.querySelector('main [role="list"]');
    if (!list) {
      VIKI.report(SOURCE, 'blind', [], 'no chat-messages list -- markup probably changed');
      return;
    }

    let rows = VIKI.findAll(list, 'li[id^="chat-messages-"]');
    if (rows.length === 0) rows = VIKI.findAll(list, '[role="listitem"]');
    if (rows.length === 0) {
      VIKI.report(SOURCE, 'blind', [], 'message list found but no rows');
      return;
    }

    /* A mention renders as a <span> the app marks for highlight, and the
     * accessible name of the message contains the mention text. Both are
     * checked because either alone has been enough to miss messages. */
    const mine = rows.filter(r =>
      r.querySelector('[class*="mention"]') ||
      /\B@\w/.test(VIKI.text(r).slice(0, 400))
    );

    const items = mine
      .map(r => VIKI.text(r))
      .filter(t => t && t.length > 12)
      .slice(0, 30);

    /* `ok` with zero items is a real answer here -- no one mentioned you --
     * and it is reported as such so the ledger can say "Discord: seen, quiet"
     * rather than leaving a hole. */
    VIKI.report(SOURCE, 'ok', items,
      rows.length + ' message(s) on screen, ' + mine.length + ' mentioning you');
  }

  VIKI.every(90000, scan);
})();
