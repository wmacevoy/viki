# Encryption at rest — verified design (D-9)

**Constraint (Warren, 2026-08-13):** repo files on the host must be encrypted
at rest. **Approach:** the [`fossil-see`](https://github.com/wmacevoy/fossil-sqlcipher-libressl)
build — Fossil 2.28 + SQLCipher 4.16.0 (via wmacevoy/sqlcipher-libressl) +
LibreSSL 4.2.1, with the mode-aware key patch on Fossil's SEE scaffolding.
`fossil-see` was itself factored out of
[pizza-party-vote-fossil](https://github.com/wmacevoy/pizza-party-vote-fossil)
(where this recipe originated) into its own standalone project, specifically
so viki and pizza-party-vote-fossil can both vendor one shared build instead
of each duplicating it. This is a *dependency*, not a fork: viki should vendor
`fossil-see` directly (git submodule), the same way pizza-party-vote-fossil
now does — not re-derive the patches from pizza-party-vote-fossil by hand.

## Verified today (all on the reproduced build)

1. **The build recipe reproduces.** `build/build-fossil.sh` at the pinned refs
   → fossil 2.28 [52445a27f1] with SQLCipher inside. Not a skeleton anymore.
2. **Repos are really encrypted.** `*.efossil` files have ciphertext headers
   (vs `SQLite format 3` plaintext); wrong key → HMAC failure, no partial reads.
3. **Encrypted hub serves standard clients.** `fossil-ppv server pm.efossil`
   (after one bug fix, below) syncs with *stock* fossil clients — encryption is
   a per-peer storage decision, invisible to the sync protocol and to
   collaborators who don't opt in.
4. **Per-device keys.** Two clones of the same hub, each under a different
   SQLCipher key, both sync. Hub key ≠ phone key ≠ laptop key; compromising
   one file discloses nothing about the others.
5. **The full iOS-constraint harness passes on this build.** In-process
   (no fork/exec, exit trapped) clone→open→add→commit→autosync-push, with the
   local repo encrypted (`FOSSIL_SEE_KEY` env → key, renamed from
   `FOSSIL_PPV_KEY` when the patch moved into fossil-see). The FFI_RISK.md
   embed patch (`db_clear_delete_on_failure`) applies cleanly to 2.28.

## Bug found & fixed: `fossil server` skipped every key source

`cmd_webserver` pre-allocates a zeroed secure page for the key so forked
request-children can inherit it — but the key-obtain path checks only that the
saved-key pointer is non-NULL, sees the zero page as "already have a key", and
applies an empty key: every open fails SQLITE_NOTADB. Stock Fossil 2.29 has the
identical shape. Fix: consult Fossil's own `db_have_saved_encryption_key()`
validator — `fossil-see`'s `build/patches/fossil-server-key-validator.patch`
(applied automatically by `fossil-see`'s own build; the copy that used to live
at `server/patches/` here predates the fossil-see extraction and is kept only
as a historical draft — see `server/upstream/fossil-forum-post.md`). Verified
end-to-end. Full write-up, drafted and ready to file with fossil-scm.org:
`vendor/fossil-see/docs/upstream-report-fossil-server-see-key.md` (once viki
vendors fossil-see).

## Threat model (honest version)

| Protects against | How |
|---|---|
| VPS disk/image theft, provider snapshots, discarded drives | repo files are SQLCipher ciphertext |
| Backup leakage | backups of `.efossil` files stay encrypted (copy of ciphertext); off-box rsync needs no extra crypto layer |
| One stolen device exposing other peers | per-device keys |

| Does NOT protect against | Mitigation |
|---|---|
| Live compromise of the running server (key is in process memory / systemd credential) | mlock'd key page (Fossil's SEE machinery), minimal attack surface, standard hardening; accept residual risk |
| Plaintext *checkout files* on devices (documents are ordinary files) | server holds no checkouts (repos only — full coverage there); iOS Data Protection / Android FBE covers app storage; desktop = user's disk-encryption responsibility |
| The unencrypted `~/.config/fossil.db` global config | contains no repo content; can hold saved URLs — keep passwords out (use per-device tokens) |

## Key management

- **Hub:** one key for the hub's `.efossil` files, provisioned to the service
  as a systemd credential (`LoadCredential=` → exported as `FOSSIL_SEE_KEY` in
  a wrapper; never in the unit file or shell history). Rotating = `fossil
  rekey` per repo (SEE machinery) or re-clone into a fresh key.
- **Devices:** each device generates its own random key at first clone, stored
  in the platform keystore (iOS Keychain / Android Keystore). Never synced.
- **Escrow:** because every peer is a full replica, key loss on one device is
  a non-event (re-clone). Hub key loss is recoverable from any clone
  (re-init hub from a device clone) — but escrow the hub key anyway (Warren's
  password manager + the gpg-wrapped `keys/master.key.asc` pattern from ppv).

## Version pin (important)

Fossil 2.29 requires SQLite ≥ 3.54.0; SQLCipher's current baseline is 3.53.1.
**viki pins Fossil 2.28 everywhere** (hub, devices, FFI build) until
SQLCipher rebases. All FFI_RISK.md findings re-verified on 2.28. Track
SQLCipher releases; bump both together — via `vendor/fossil-see`'s own pin,
once viki vendors it (see KICKOFF.md).

## Deployment deltas (to fold into setup-hub.sh)

1. Install `fossil-see` (built binary, produced by `vendor/fossil-see/build/build.sh`) instead of apt fossil.
2. Repos named `*.efossil` (the glob is the encryption trigger).
3. Key via systemd `LoadCredential` + wrapper exporting `FOSSIL_SEE_KEY`.
4. No separate patch-apply step needed here — `fossil-see`'s own build already applies `fossil-server-key-validator.patch`.
5. Backup cron unchanged — `fossil backup` output remains encrypted.
