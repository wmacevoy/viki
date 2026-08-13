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

## Open items

- OQ-3 (private areas): repo-per-project maps cleanly onto repolist mode —
  separate user tables per repo answer the ICPC-collaborator isolation story.
- v2: MCP convenience server co-hosted on the VPS (ARCHITECTURE.md), reading
  the same repos through the same account model.
