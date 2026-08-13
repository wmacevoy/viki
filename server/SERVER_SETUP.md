# Hub setup — Hetzner VPS

**Stack decision (D-8):** one Fossil binary serving `/srv/fossil/repos/` in
`--repolist` mode behind **Caddy** (automatic Let's Encrypt TLS), managed by
**systemd**, firewalled to ssh+80+443, with nightly `fossil backup` snapshots.

```
Internet ── 443 ──> Caddy (auto-TLS) ── localhost:8080 ──> fossil server --repolist
                                                             /srv/fossil/repos/*.fossil
```

## Why this shape

- **`--repolist` mode**: one service, many repos — `pm.fossil` now;
  `monument-rock.fossil`, `icpc.fossil` later, each with independent users
  and permissions, all at `https://DOMAIN/<name>`.
- **Caddy over nginx/certbot**: TLS certificates appear and renew with zero
  maintenance from a 3-line config. Fossil itself never faces the internet;
  it listens on localhost only (`--localhost`).
- **systemd hardening**: dedicated no-login `fossil` user, `ProtectSystem=strict`,
  writable only under `/srv/fossil`.
- **Backups despite DVCS**: clones back up *artifacts*, but the server also
  holds users, passwords, capabilities, and ticket/report config. Nightly
  `fossil backup` (SQLite-consistent, safe while live) keeps 7 rotating
  day-named snapshots; optionally rsync them to a Hetzner Storage Box.
- **Sizing**: the smallest Hetzner cloud instance is ample. Fossil serves its
  own project site (docs+forum+wiki) for ~thousands of users from one binary.

## Install

1. Point a DNS A/AAAA record (e.g. `fossil.<your-domain>`) at the VPS.
2. Copy `setup-hub.sh` to the VPS and run:
   `sudo DOMAIN=fossil.<your-domain> ADMIN_USER=warren ./setup-hub.sh`
3. Note the initial admin password it prints; change it in the web UI.

Re-running the script is safe (idempotent); it won't touch existing repos.

## Accounts & capabilities (US-1e / US-2a)

Create in the web UI (Admin → Users), per repo:

| Account | Capabilities | Notes |
|---------|--------------|-------|
| warren  | s (setup)    | you |
| humans  | e.g. `chi o r w` + wiki/ticket caps as needed | per-person, revocable |
| claude  | `c h i o r w` — commit, hyperlinks, check-out, read, write; **no** a/s | password = the token you hand to an agent session; rotate/revoke freely |
| anonymous / nobody | none (or read-only `h o r` if you want public browse) | default-deny |

Agent onboarding is then literally:
`fossil clone https://claude:TOKEN@fossil.<domain>/pm pm.fossil`

## Distribution-version note

`apt` installs Debian/Ubuntu's packaged Fossil, which can lag upstream. Fine
to start. When we begin relying on version-sensitive behavior (check-in locks,
JSON API details — the app will pin a Fossil version via FFI anyway), switch
the server to the static binary from fossil-scm.org/home/uv/download.html:
drop it at /usr/local/bin/fossil, restart the service. The sync protocol is
strongly backward-compatible, so hub and clients don't need matching versions.

## `viki serve` (search UI + agent API), on the same hub

`viki serve` (AGENTS.md) binds to `127.0.0.1` only, by design — it is
never internet-facing on its own (see FINDINGS.md's design-decision entry
on why it doesn't implement its own TLS/auth). To put it on the internet,
it reuses this hub's existing Caddy instance rather than inventing a
second TLS story:

```
Internet ── 443 ──> Caddy (auto-TLS) ── /viki/* ── Basic Auth ──> localhost:$VIKI_PORT ──> viki serve
                                    └── (else)  ──> localhost:$FOSSIL_PORT ──> fossil server --repolist
```

Run `server/setup-viki-serve.sh` **after** `setup-hub.sh` has already set
up Caddy + TLS for `$DOMAIN`:

```sh
sudo DOMAIN=fossil.<your-domain> \
     VIKI_REPO_CHECKOUT=/srv/viki/checkout \
     VIKI_MODEL_DIR=/srv/viki/model \
     VIKI_BIN=/usr/local/bin/viki \
     ./setup-viki-serve.sh
```

`VIKI_REPO_CHECKOUT` must be an **open** fossil-see checkout (`viki
index`/`ask`/`serve` all run relative to cwd) of whichever repo you want
searchable; `VIKI_MODEL_DIR` is your local `build/dist/model` copied to
the VPS; `VIKI_BIN` is the `viki` binary **plus every `libonnxruntime.*`
file next to it** (see FINDINGS.md's dyld install-name finding — copying
just one name variant breaks at runtime).

It's idempotent (marker-delimited insert into the existing Caddyfile;
safe to re-run) and:

- Runs `viki serve` under a dedicated no-login `sysviki` system user via
  systemd, `--host 127.0.0.1` explicitly (never rely on the flag's
  default matching intent — state it).
- Generates a random Basic Auth password on first run (printed once;
  saved as a bcrypt hash at `/etc/caddy/viki.htpasswd` via `caddy
  hash-password`) and inserts a `handle_path /viki/*` block with
  `basic_auth` into the Caddyfile's existing `$DOMAIN { ... }` block —
  Caddy checks the password and terminates TLS before anything reaches
  `viki serve`.

**Open item this script does NOT solve:** periodic re-indexing. The
cache db only reflects whatever `viki index` last saw when run by hand;
wire up a cron job (e.g. triggered after `fossil sync`) to keep it
current. Also not yet solved: the search page's `source` field is plain
text, not a link into this same hub's Fossil web UI for that wiki
page/ticket/file (see AGENTS.md's Not-yet-built list).

**Verification status, stated honestly:** the Caddyfile-editing logic
(marker-delimited insert/replace, idempotent re-run) was tested against
a scratch Caddyfile with the real `sed` commands the script runs;
`useradd`/`chown`/`systemctl`/`caddy hash-password` were stubbed for a
full dry run of the script's control flow. **Not yet run against a real
VPS with a real Caddy + systemd.** Same caveat setup-hub.sh already
carries implicitly (it's provisioning tooling for a future deployment,
not something exercised in this dev environment) -- verify on the actual
hub before relying on it, same as any infra script.

## Open items

- OQ-3 (private areas): repo-per-project maps cleanly onto repolist mode —
  separate user tables per repo answer the ICPC-collaborator isolation story.
- v2: MCP convenience server co-hosted on the VPS (ARCHITECTURE.md), reading
  the same repos through the same account model.
- `viki serve`: periodic re-indexing (cron/hook) and linking search
  results back to the Fossil web UI (see above).
