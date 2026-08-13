# viki — Architecture

*(Renamed from "fossil-app". The encrypted Fossil build is "fossil-see";
the semantic memory layer is specified in VIKI_DESIGN.md.)*

Governing principle: **respect the focus of the user** (see USER_STORIES.md).
Governing topology: **hub-and-spoke**. Nothing reaches into anything else;
every party syncs with one hub when it can.

```
                        ┌─────────────────────────────┐
                        │   Cloud host (small VPS)     │
                        │                              │
                        │  fossil server ── HTTPS ───┐ │
                        │   • sync endpoint          │ │
                        │   • web UI (browser users) │ │
                        │   • accounts + capabilities│ │
                        │                            │ │
                        │  [v2] MCP server ──────────┘ │
                        │   structured verbs over the  │
                        │   same repo (query calendar, │
                        │   file note, create request) │
                        └──────────────┬───────────────┘
                 sync (fossil protocol over HTTPS), when online
        ┌──────────────┬───────────────┼────────────────┬─────────────┐
        ▼              ▼               ▼                ▼             ▼
   iPhone/iPad     Android        Desktop (Mac)     Claude/agents   Browser-only
   Flutter app     Flutter app    Flutter or CLI    fossil clone    collaborators
   full clone      full clone     full clone        in agent's      (no install —
   FFI fossil      FFI or         plain fossil      workspace;      server web UI:
   offline-first   subprocess     binary            wakes, pulls,   wiki, timeline,
                                                    acts, pushes    reports)
```

## Why hub-and-spoke

- **Trust:** no peer has access to another's device — including agents. An
  agent is just a Fossil user account with scoped capabilities and a revocable
  token. Everything it does is a diff in the timeline.
- **Offline:** every spoke holds a *complete* clone. Any spoke (or the hub)
  can die without data loss; the hub is also the backup.
- **Simplicity:** for v1 there is no custom protocol anywhere. Fossil's sync
  protocol is the transport; Fossil accounts are the auth; the repo layout +
  ticket conventions are the API.

## Agent access path

1. Agent wakes (schedule, or a poke from Warren via chat).
2. `fossil pull` from the hub → full current state.
3. Reads `AGENTS.md` + its work queue (tickets assigned to it / inbox paths).
4. Acts; commits; `fossil push`. Conflicts → branch + ticket, never overwrite.
5. Disappears. All state is in the repo (US-2b).

The [v2] MCP server on the hub host adds *convenience*, not capability: it
exposes structured verbs (query the calendar projection, file a note, open a
request) so chat sessions and the voice path don't shell out to fossil. It
reads/writes the same repo through the same account model.

## Voice layers (D-7)

| Layer | Where it runs | Offline? | Portability |
|-------|---------------|----------|-------------|
| Dictation (speech→text) | On device (SFSpeechRecognizer / Android recognizer via Flutter `speech_to_text`) | **Yes** | Cross-platform (mobile; desktop limited) |
| Understanding (NL query/command) | Agent, via request artifact through the hub (or MCP verb) | No — queues as capture when offline | Platform-independent |
| Wake phrase ("Hey Siri…") | iOS App Intents / Android App Actions | n/a | Per-platform shims, zero logic |

## Data layers on each device

1. **Checkout** — documents as plain files (Files-app visible on iOS).
2. **Repo clone** — one SQLite file; artifacts incl. wiki + ticket/event deltas.
3. **Projections** — local-only SQLite caches rebuilt from artifacts
   (`cal_event`, `cal_instance`, focus/open-loop queries). Disposable, never synced.

## v1 / v2 split

- **v1:** hub server + accounts; repo layout + `AGENTS.md`; Flutter app
  (documents, calendar views, wiki reader, capture incl. voice dictation,
  sync); agents via plain fossil.
- **v2:** MCP server on hub; NL voice; wake-phrase shims; notifications/
  background sync polish; ticket "agenda" reports on server UI.
