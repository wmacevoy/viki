# viki reader — a Chrome extension that only reads

Reads your own Facebook notifications and Discord mentions into your local
viki. It is the `observe` rung of VIKIVERSE_V1 §2.2c, and it exists because
roughly half of Warren's notification channels have no API — so the only honest
way to see them is the way he already sees them: rendered in his own
authenticated browser session.

## Install

1. `chrome://extensions` → enable **Developer mode**
2. **Load unpacked** → choose this directory (`edge/chrome`)
3. Start viki: `viki serve` (it listens on `127.0.0.1:8080`)
4. Open `facebook.com/notifications` or a Discord channel, then click the
   extension icon to see what it can and cannot read.

## What it does, and what it deliberately cannot

| | |
|---|---|
| reads | Facebook `/notifications` rows; Discord messages **that mention you**, in the channel already on screen |
| never | clicks, submits, types, posts, replies, scrolls, or opens anything |
| sends to | `http://127.0.0.1:8080` and nowhere else — hardcoded, no setting |
| stores | a fingerprint of what it has already captured, so nothing repeats |

`build/reader-probe.sh` enforces those claims (9 assertions) rather than
trusting this file: no `.click(`, `.submit(`, `dispatchEvent`, `execCommand`,
`innerHTML =` or `document.write` anywhere in the extension; no non-loopback
destination; and no `tabs`/`scripting`/`cookies`/`history` permission.

## The design decision worth knowing

**A scraper must be able to tell "nothing there" from "I cannot see any more."**

Facebook and Discord ship obfuscated markup that is rebuilt constantly, so these
selectors *will* break. A scraper that silently returns zero items when that
happens is indistinguishable from a quiet day — it would launder breakage into
false calm, which is exactly the coverage lie §2.5 forbids and the opposite of
what the product is for.

So every extractor reports a status: `ok` (anchor found, *n* items, and 0 is a
real answer), `blind` (anchor gone — markup changed), or `loggedout`. The popup
shows it per source, and it is meant to reach the ledger's coverage line so a
morning brief can say *"Facebook: blind since 09:14"* rather than quietly
omitting Facebook.

Anchors are chosen from `role` and `aria` attributes rather than class names:
class names on these sites are per-build hashes, while the screenreader contract
changes rarely because real people depend on it.

## What loading it in Chrome actually broke

Three bugs, all the same shape, all found on first contact (2026-08-24) and all
in the **signed-out** path — which is exactly where a reader must not go quiet.

1. **Facebook redirects `/notifications` → `/login.php` when signed out.** The
   first version guarded on *being at* `/notifications`, so it went **silent**.
   Silence is indistinguishable from "no notifications" — false calm, the one
   failure this thing exists to prevent.
2. **Discord redirects `/channels/@me` → `/login`,** and the manifest matched
   only `/channels/*`, so the content script never ran at all. Same false calm,
   different cause.
3. **A mid-load SPA looks exactly like broken markup.** Measured: three seconds
   after navigating to `/channels/@me`, the message list did not exist *and* the
   login form had not rendered. The first version called that `blind` — so it
   would have cried wolf on every single page load.

Fixes: login URLs are checked **first** (a redirect is immediate; the form is
rendered by JS and arrives late), both content scripts now match the whole site,
and `blind` requires **three consecutive misses** — a first miss is "not ready",
only a persistent one is "I cannot see any more". `ok` and `loggedout` still
report immediately, since those are positive findings.

All three are regression fixtures now (`test/fixtures.mjs` F8, D8, D9, D10).

### Then, signed in, a fourth — and it was the anchor itself

**Facebook has no `[role="main"]` and no `<main>` on `/notifications`.** Its
landmark roles are `banner`, `navigation`, `grid`. The extractor required a main
region *before* looking for rows, so it reported `blind` while **thirty perfectly
good `[role="listitem"]` rows sat right there.**

The lesson generalises past this one selector: **scope to the narrowest anchor
that actually identifies the content, and do not gate it behind a broader one
you merely expect to exist.** Searching the whole document costs nothing here and
removes a failure mode.

Measured on a live logged-in account: **30 rows found, 28 captured, 2 dropped**
as navigation chrome ("New", "All"). Every row is prefixed `Unread ` — that is
*state*, not content, so it is stripped; otherwise the same notification would
fingerprint differently once read and be captured twice.

### Discord, signed in: one latent bug, and the message path still unproven

`/channels/@me` is the **Friends page**, not a conversation. The fallback
`main [role="list"]` matched the **friends roster** — 16 rows reading *"Bob
Kramer Idle Message More"* — and the extractor then hunted mentions inside it.
It captured nothing only because no friend's name happened to contain an `@`.
That is luck, not design.

Fixed two ways: `/channels/@me` with no channel id now returns silently (nothing
is broken — Warren simply is not in a channel, and this is the one place in the
file where silence is correct), and the fallback is scoped to a region that must
contain messages.

**Still unverified:** `[data-list-id="chat-messages"]`, the message rows, and
mention detection. Navigating directly to a DM URL bounced to `/login` — the
logged-out detection fired correctly, which is something, but the message path
has never met a real channel. Expect it to be wrong.

### D2L: the surface was wrong, and shadow DOM hid everything

Two lessons, in order of importance.

**The wrong surface.** The first D2L extractor read course *announcements* —
things Warren **sent**. He uses D2L almost exclusively for assignment
management, so the promise surface is **Quick Eval**: learner submissions
awaiting his evaluation. Work he **owes**, each with a date. Rewritten to target
`/d2l/le/<orgUnit>/quickeval/`; one row reads
`<learner> repo CSCI365-001-21618 Data Mining 8/20/2026 9:38 AM`.
**Verified live: 20 rows found, 20 captured.**

**Shadow DOM, in two separate ways, both silent.** D2L renders through 63 open
shadow roots.

1. *Finding* elements needs a piercing walk — a flat `querySelectorAll('a[href]')`
   finds 19 links where a piercing one finds 84, and `document.body.innerText`
   returns 364 characters for a full page.
2. *Reading* them needs a piercing text function too, which is easy to miss
   because step 1 appears to work. Every `d2l-activity-name` was located
   correctly and every one returned `""` from `innerText`, because its text is
   inside its own shadow root. And `deepText` must skip `STYLE` — the first
   attempt extracted `":host { display: block; }…"` as a notification.

Then a third, found only by running it: **`deepAll` must descend into the
root's own shadow root.** Given `deepAll('tbody tr', table)` where `table` is a
custom element, the light-DOM query returns nothing, the `*` walk returns
nothing, and the function reports **0 rows from a table holding 20**. All three
failures report an *empty page* rather than an error, which is exactly the class
this reader is built to make visible.

Fixtures S1–S4 pin them.

## Limits, stated rather than discovered

- **Discord reads only the channel you have open.** It does not enumerate
  guilds, switch channels, scroll, or open DMs. Walking channels to harvest them
  would be a bot, which Discord's terms prohibit — and which is a different
  thing morally as well as contractually.
- **It only reports Discord messages that mention you.** An unfiltered channel
  is chatter, and §2.3 says noise reduction *is* the product; forwarding
  everything would move the noise rather than reduce it.
- **Facebook: notifications only, never the feed.** The feed is an infinite
  ranked surface; reading it would be building a second timeline rather than
  catching promises.
- **It polls every 90 seconds** rather than using a MutationObserver, which on
  these pages fires hundreds of times a second and would turn a reader into a
  load generator on your own machine.
- **Terms of service are yours to weigh.** Reading your own screen in your own
  session is a very different act from automated posting, and this extension
  cannot do the second. That asymmetry is the reason it stops at `observe`.
