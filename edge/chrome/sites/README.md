# sites/ — yours, not viki's

> *"remember this is how i will use viki — these are not what viki is."*
> — Warren, 2026-08-24

That distinction was getting lost, so it is now enforced by the directory
layout rather than by remembering.

## The line

| | |
|---|---|
| **viki's** | `content/common.js`, `background.js`, `popup.*`, and the observe-only guarantees `build/reader-probe.sh` enforces — no clicking, loopback-only delivery, and the `ok` / `blind` / `loggedout` status contract |
| **yours** | everything in this directory, plus `sites.json` |

`manifest.json` is **generated** from `sites.json` by `build-manifest.sh`. It
used to be checked in with `d2l.coloradomesa.edu` written into it, which made a
Colorado Mesa hostname look like part of viki. It is not. Nobody else's install
should inherit Warren's campus.

## Adding a site

1. Write `sites/<name>.js`. It gets `VIKI` from `content/common.js` and must end
   in exactly one `VIKI.report(source, status, items, note)` per scan.
2. Add it to `sites.json`.
3. `sh build-manifest.sh`, then reload the extension.

Nothing in `content/` or `background.js` changes. If adding a site requires
touching the framework, that is a sign the framework is missing something —
fix it there, generically, rather than special-casing.

## The contract every site file must keep

- **`ok` with zero items ≠ `blind`.** "Nothing is waiting" and "I cannot see any
  more" are different answers, and conflating them turns a broken scraper into
  false calm. That is the failure this whole component exists to prevent.
- **Report `loggedout` on a redirect**, checking the URL *before* looking for a
  sign-in form: the redirect is immediate, the form is rendered by JS and
  arrives late enough to miss.
- **Never act.** No `.click()`, `.submit()`, `dispatchEvent`, `innerHTML =`.
  The probe fails the build if any appears.
- **Silence is only correct for "not my page."** Everywhere else, say something.

## What each site cost to get right

Kept because the mistakes generalise, and the next site will hit at least one.

| site | the trap |
|---|---|
| facebook | no `[role="main"]` and no `<main>` at all; gating rows behind it reported `blind` while 30 rows sat there. Rows carry an `Unread ` state prefix that must be stripped or the same item fingerprints twice |
| discord | `/channels/@me` is the **Friends** page, and the fallback matched the friends *roster*; signed-out redirects to `/login`, which the original match pattern excluded entirely |
| d2l | shadow DOM three ways — finding, reading, and descending into the **root's own** shadow root. And the first version read announcements, which are what Warren *sent*; the promise surface is Quick Eval, which is what he *owes* |
| outlook | 23 `div[draggable="true"]` that look exactly like message rows and are the **folder tree**. Anchor on the accessible name, never on a behaviour attribute |
