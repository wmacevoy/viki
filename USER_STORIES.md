# viki — User Stories

*(Project renamed from "fossil-app" 2026-08-13. viki = the system and the
verb: everyone — humans, apps, agents — asks viki.)*

A personal/team management tool built on Fossil SCM: one cloud-hosted Fossil
repository as the hub, full local clones on every device and agent,
offline-first, encrypted at rest, with a shared semantic memory
(see VIKI_DESIGN.md).

Status key: `draft` → `agreed` → `building` → `done`

## Design principle 0: Respect the focus of the user

*(Warren's phrasing, 2026-08-13 — the app's constitution.)*
Every surface must earn its place by reducing what the user looks at, not
adding to it. Show less; filter upstream; never manufacture urgency; capture
must cost seconds and zero decisions. When a feature conflicts with this
principle, the principle wins.

---

## US-1: Access documents, calendar, and wiki; sync when online — `draft`

> As a **human**, I want to access my documents, calendars, and wiki, and have
> them sync with a backup and other collaborators once/whenever I'm online.

### Breakdown

**US-1a — Documents (offline)**
As a human, I can browse, read, and edit my documents on any of my devices with
no network. Documents are markdown + small files living as ordinary files in a
Fossil checkout — visible as a normal folder (iOS Files app / Android folder /
desktop directory).

*Acceptance:*
- Airplane mode for a week: full read/write access, nothing lost.
- Edits become local Fossil commits (auto-commit or explicit — see OQ-2).
- A document is a plain file; any external editor can touch it.

**US-1b — Wiki**
As a human, I can read and edit the project wiki. The wiki lives in Fossil's
built-in wiki (wiki artifacts), so collaborators with only a browser get the
server's native wiki UI.

*Acceptance:*
- Wiki pages sync as artifacts like everything else; editable offline from the app.
- Server web UI shows the same wiki with zero extra code.
- Note: built-in wiki pages are *not* files in the checkout; the app needs a
  wiki view that reads wiki artifacts from the local clone and renders the
  markup. (Decision D-1.)

**US-1c — Calendar (queryable)**
As a human, I can see my events in day/week/month views and query them, offline.
Events are synced artifacts (Fossil ticket pattern); each device projects them
into a local SQLite table for fast queries. See `CALENDAR_DESIGN.md`.

*Acceptance:*
- Create/edit/delete events offline on two devices; both sets of changes
  survive sync (field-level merge, no lost updates).
- Recurring events (RRULE) render correctly in a month view, offline.
- "What's on Thursday?" is a local SQL query — no network, no scan of files.

**US-1d — Sync once online**
As a human, I don't think about syncing. When the device has connectivity, it
syncs opportunistically (on app open, after a commit, on push notification /
background refresh). When it doesn't, everything still works and sync happens
later.

*Acceptance:*
- No user action required for the common case; a manual "sync now" exists.
- Sync failure is silent-but-visible (status indicator), never a modal error,
  never data loss.
- The cloud repo *is* the backup: every clone holds full history, so any
  single device (or the server) can be lost without losing data.

**US-1e — Collaborators**
As a human, I share the repo with team members and agents. Each has a Fossil
account on the cloud server with capability-based permissions. Browser-only
collaborators use the server web UI (timeline, wiki, reports).

*Acceptance:*
- Adding a collaborator = creating a server account; no app install required
  for read/wiki/ticket access via browser.
- An agent (e.g. Claude) can clone, read, commit, and push with its own
  account/token, and its changes appear in the timeline attributed to it.

---

## Decisions log

| # | Decision | Rationale |
|---|----------|-----------|
| D-1 | Wiki uses **Fossil's built-in wiki artifacts** (not markdown files in checkout). | Warren's call 2026-08-13. Server UI for free; app renders wiki artifacts from the local clone. |
| D-2 | Calendar is **queryable, database-backed**: events as synced ticket-style artifacts + local SQLite projection (`events` + `instances` tables). | Fossil syncs artifacts, not tables; the ticket pattern gives field-level offline merge + SQL reports on the server. RFC 5545 fields, Android-CalendarProvider-style instances cache. |
| D-3 | Documents v1 = **markdown + small files**; big binaries out of scope for v1. | Fossil history is immutable; keeps phone clones light. |
| D-4 | Hub = cloud `fossil server` over HTTPS; agents are ordinary Fossil user accounts. | From architecture discussion 2026-08-12. |
| D-5 | Mobile = Flutter; Fossil linked as a C library via dart:ffi (client ops only, no embedded web server on device). Android may use subprocess interim. | iOS forbids fork/exec; client ops don't need it. |
| D-6 | **Hub-and-spoke topology**: the cloud Fossil server is the only rendezvous. No peer reaches into another's device; agents access the system only as Fossil users syncing with the hub. See `ARCHITECTURE.md`. | Simple trust model; offline-safe; agents need no special transport for v1 (Fossil protocol *is* the agent API). MCP layer on the hub host is a v2 convenience. |
| D-7 | Voice: dictation on-device (cross-platform, offline-capable); NL understanding is online + agent-backed via request artifacts; wake-phrases are thin per-platform shims. | Keeps offline story intact; no logic in platform glue. |
| D-8 | Hub hosting: bare systemd + Caddy on Warren's Hetzner VPS; no containers/PaaS; the idempotent setup script *is* the infrastructure. | See SERVER_SETUP.md. |
| D-9 | **Encryption at rest** everywhere via the fossil-see build (Fossil 2.28 + SQLCipher/LibreSSL; refactored out of wmacevoy/pizza-party-vote-fossil). Per-device keys; hub key via systemd credential. Pins Fossil 2.28 until SQLCipher ≥ SQLite 3.54. | Verified end-to-end incl. the iOS in-process harness; see ENCRYPTION.md. |
| D-10 | **Vectors are projections; viki is a protocol, not an index.** Embeddings are derived, rebuildable, never source of truth; any peer may hold its own index; the query verb is uniform. | See VIKI_DESIGN.md. |
| D-11 | **One pinned rung-2 embedding model (quantized MiniLM-class ONNX, ~25 MB) is universal** across devices and agents, making embeddings deterministic shared data: computed once by whoever sees content first, no re-indexing. Model changes are explicit epoch bumps via viki-manifest. | FTS5 (rung 0) stays as free floor; hybrid query is default. |
| D-12 | **Embedding caches and the pinned model distribute as Fossil unversioned files** (`fossil uv`) through the same hub/auth — latest-wins, no history bloat, self-contained system (no runtime third-party downloads). sqlite-ndvss (MIT, single-file, statically linked) is the scan engine. | Swap to sqlite-vec later only if corpus outgrows brute force. |

## Open questions

- **OQ-1:** Calendar events as literal Fossil *tickets* (reuse `fossil ticket`
  machinery + server reports) vs. a parallel artifact type with our own
  projection code? Tickets = far less code; slightly clunky semantics.
- **OQ-2:** Documents: auto-commit on save (Dropbox feel) vs. explicit
  commit (SCM feel)? Could be per-folder policy.
- **OQ-3:** Do humans get private areas (per-user repo vs. one shared repo vs.
  repo-per-project with a portfolio view in the app)?
- **OQ-4:** Notifications when a collaborator/agent changes something you watch?

---

## US-2: Agent stories — `draft`
*(written by Claude, from the agent's chair)*

**US-2a — Identity and least privilege.**
As an agent, I want my own Fossil account and token with capabilities scoped to
what I'm trusted to do (commit to agreed areas, edit tickets/wiki; no admin, no
user management), so that my mistakes are bounded and my access is revocable
without touching any human's.

*Acceptance:* timeline shows `author: claude` on everything I do; revoking my
token affects only me.

**US-2b — Ephemeral, pull-based operation.**
As an agent, I don't sit online. I wake (on schedule or on a poke), `fossil
pull`, read state, act, commit, push, and disappear. Nothing in the system may
assume an agent is currently running.

*Acceptance:* a cold start from a fresh clone produces correct behavior —
all state I need lives in the repo, none in my head.

**US-2c — The repo is my memory and my manual.**
As an agent, I want an `AGENTS.md` (conventions, layout, formats, standing
instructions) versioned *in the repo*, so a brand-new agent session can orient
from repo contents alone — and so changing my instructions is itself a synced,
audited commit anyone can review or revert.

**US-2d — Explicit work queue, idempotent processing.**
As an agent, I want requests addressed to me to be explicit artifacts (tickets
assigned to `claude`, or notes in an agreed inbox path) with a lifecycle
(open → claimed → done, linked to the commit that resolves it), so that if I'm
interrupted mid-task, the next wake-up resumes cleanly instead of
double-processing.

**US-2e — Safe writes, never silent conflict resolution.**
As an agent, when my change collides with a human's, I commit mine to a branch
and open a ticket rather than auto-merging over a person. Everything I do must
be one `fossil revert`/merge away from undone.

**US-2f — Full auditability.**
As an agent, I *want* to be watched: every action visible as a diff in the
server timeline, reviewable from a phone browser. Trust in agents is built on
cheap auditing, and Fossil gives it away free.

---

## US-3 … US-8: Warren-side stories — `draft`
*(role-played by Claude from Warren's profile; Warren to veto/edit)*

**US-3 — Offline field capture (the killer story).**
As Warren at the Monument Rock bait sites with zero cell coverage, I want to
log an observation in under 15 seconds — site, horses seen, hay level, photo,
auto timestamp + GPS — and have it sync on the drive back when signal returns.

*Acceptance:* zero bars the whole time; capture is pocket-to-logged < 15s;
nothing to remember to do later — sync is automatic when coverage returns.

**US-4 — Capture without filing.**
As Warren, when a thought arrives I want exactly one place to put it and *zero
decisions* about where it goes — filing decisions kill capture. An agent files,
tags, and links it within a day, and I can see where it landed.

**US-5 — "What needs me" view, never a wall.**
As Warren, opening the app shows: today's events (all calendars), at most three
focus items, and open loops going stale. Nothing else. The app's job is to show
me *less*, powered by the calendar projection and ticket queries underneath.

**US-6 — Delegate by writing a note.**
As Warren, I want to drop a note like "draft the follow-up to Issac about the
Monument Rock trailer" and find, by next morning, a committed draft waiting for
my edit/approval — with the request→result link visible in the timeline.

**US-7 — Nothing is ever lost, everything is undoable.**
As Warren, any change by anyone — including agents — is reversible; delete is a
tombstone, not destruction; and backup is not a chore I can forget, because
every device and the server each hold complete history.

**US-8 — Open loops resurface gently.**
As Warren, things I'm waiting on (e.g. Issac/BLM on the trailer) come back to
me after N quiet days inside the focus view — no notification spam, no shame,
just "this went quiet, poke it?"

---

## US-9: Voice — talk instead of tapping — `draft`

**US-9a — Voice capture (works offline).**
As Warren, I hold a button and speak — "site two, six horses, hay half gone" —
and it becomes a captured note/observation. Dictation runs **on-device**
(iOS SFSpeechRecognizer / Android recognizer via Flutter `speech_to_text`), so
this works at Monument Rock with zero bars. Upgrades US-3: no cold-thumb typing.

*Acceptance:* airplane mode, pocket-to-captured < 10s by voice alone; raw audio
optionally kept alongside the transcript.

**US-9b — Voice query/command (online, agent-backed).**
As Warren, when online I can ask in natural language — "what's Thursday look
like?", "find the note about the trailer", "draft the reply to Issac" — and an
agent with the repo answers or files the request. The app sends the utterance
as a request artifact; an agent picks it up; the result syncs back.

*Acceptance:* degrades gracefully offline → falls back to US-9a capture
("I'll handle this when we're back online" — queued as a request artifact).

**US-9c — Wake-phrase entry points (thin platform shims).**
"Hey Siri, log an observation" via iOS App Intents/Shortcuts; Android App
Actions equivalent. Per-platform glue only — no logic lives there.

---

## Story backlog (not yet worked)

- As a **team member with only a browser**, I want to…
- As a **course/ICPC collaborator**, I want project areas separated so my
  access doesn't include Warren's personal space… (ties to OQ-3)
