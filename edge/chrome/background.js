/* viki reader -- the only component that talks to viki.
 *
 * THE DESTINATION IS HARDCODED TO LOOPBACK AND THERE IS NO SETTING FOR IT.
 * Everything observed is a private message or notification addressed to
 * Warren; a configurable endpoint is one mis-typed field away from posting his
 * correspondence to a stranger's server, and no convenience is worth that. If
 * viki is not running, observations queue locally and nothing leaves the
 * machine.
 *
 * WHAT THIS DOES NOT DO, by construction: it never writes to any page, never
 * clicks, never sends on Warren's behalf. VIKIVERSE_V1 2.2c calls that rung
 * `observe`, and it is the one that needs no policy decided first.
 *
 * COVERAGE IS STATE, NOT A LOG LINE. Per-source status is kept so the ledger
 * can say "Facebook: blind since 09:14" instead of quietly omitting Facebook.
 * A reader that breaks silently would turn a scraping failure into false calm,
 * which is the exact coverage lie 2.5 exists to prevent.
 */

const VIKI_URL = 'http://127.0.0.1:8080';
const SEEN_MAX = 500;          /* fingerprints remembered, FIFO */
const QUEUE_MAX = 200;

async function state() {
  const d = await chrome.storage.local.get(['seen', 'queue', 'sources', 'enabled']);
  return {
    seen: d.seen || [],
    queue: d.queue || [],
    sources: d.sources || {},
    enabled: d.enabled !== false        /* default ON; the popup can pause it */
  };
}

/* POST to viki's capture route. The X-Viki-Local header is viki's cross-origin
 * guard (a guard, not authentication -- see CLAUDE.md), and an extension is
 * exactly the caller it was meant to admit. */
async function capture(text, channel) {
  /* THE CHANNEL IS DECLARED, NOT ENCODED IN THE TEXT.
   *
   * This used to prefix '[source] ' onto the text and let `viki coverage`
   * parse it back out. That could not be told from a bracket a person typed,
   * so `viki capture "[TODO] fix the gate latch"` invented a channel named
   * TODO that the morning brief listed as freshly covered -- a coverage lie
   * in the mechanism built to prevent one.
   *
   * The prefix is KEPT as well, because it is provenance a human reads in the
   * note body and an agent structuring the note later needs: a promise from
   * Discord reads differently from one Warren typed. What changed is that
   * coverage no longer has to guess it. */
  const url = VIKI_URL + '/api/capture?text=' + encodeURIComponent(text) +
              (channel ? '&channel=' + encodeURIComponent(channel) : '');
  const r = await fetch(url, { method: 'POST', headers: { 'X-Viki-Local': '1' } });
  if (!r.ok) throw new Error('viki answered HTTP ' + r.status);
  return r.json().catch(() => ({ ok: true }));
}

async function drain() {
  const s = await state();
  if (!s.enabled || s.queue.length === 0) return;
  const rest = s.queue.slice();
  let sent = 0;
  while (rest.length) {
    const item = rest[0];
    try {
      await capture(item.text, item.channel);
      rest.shift();
      sent++;
    } catch (_) {
      /* viki is down. Keep the queue and stop -- retrying the rest would just
       * fail too, and losing an observation is worse than delivering it late. */
      break;
    }
  }
  if (sent) await chrome.storage.local.set({ queue: rest, lastSent: new Date().toISOString() });
}

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (!msg || msg.kind !== 'viki-observation') return;
  (async () => {
    const s = await state();

    /* Status is recorded even when there is nothing to send -- `blind` with
     * zero items is the case that must never look like a quiet day.
     *
     * `lastOk` is kept SEPARATELY from `at`, and that separation is the point:
     * under a morning-login model a channel goes stale simply because the
     * session expired, and stale is not broken. `at` says when the reader last
     * looked; `lastOk` says when it last actually SAW anything. A brief that
     * reports "Teams: last read Tuesday" is honest; one that reports only
     * "Teams: loggedout" tells Warren nothing about how much he is missing. */
    const prev = s.sources[msg.source] || {};
    s.sources[msg.source] = {
      status: msg.status,
      at: msg.at,
      note: msg.note,
      lastOk: msg.status === 'ok' ? msg.at : (prev.lastOk || null)
    };

    let queue = s.queue;
    if (s.enabled && msg.status === 'ok') {
      const seen = new Set(s.seen);
      for (const it of msg.items) {
        if (seen.has(it.fp)) continue;
        seen.add(it.fp);
        /* Provenance in the text itself: viki captures plain text, and a
         * promise from Discord read differently from one Warren typed. The
         * agent structuring it later needs to know which. */
        queue.push({ fp: it.fp, text: '[' + msg.source + '] ' + it.text,
                     channel: msg.source });
        if (queue.length > QUEUE_MAX) queue.shift();
      }
      s.seen = Array.from(seen).slice(-SEEN_MAX);
    }

    await chrome.storage.local.set({ seen: s.seen, queue, sources: s.sources });
    await drain();
    sendResponse && sendResponse({ ok: true });
  })();
  return true;
});

/* Retry the queue on a slow clock, so observations made while viki was down
 * arrive once it is up rather than waiting for the next Facebook visit. */
chrome.alarms.create('viki-drain', { periodInMinutes: 2 });
chrome.alarms.onAlarm.addListener(a => { if (a.name === 'viki-drain') drain(); });
