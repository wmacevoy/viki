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
