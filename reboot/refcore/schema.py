"""The tables. The DDL is real; everything that executes it is not.

TRUTH IS THREE TABLES AND THEY ARE DELIBERATELY SEPARATE. In a 2P-Set the
add-set and the remove-set are different objects, and the immune class is
larger than "tombstones": it is EVERYTHING AUTHORITY IS DERIVED FROM. v1
immunized only tombstones, so erasing a grant made the sweep order-dependent --
the same set reaching two terminal states depending on sweep order
(FINDINGS.md C-2). Nothing deletes from `tombstone` or from `grant`.
"""

DDL = """
-- The add-set.
CREATE TABLE IF NOT EXISTS assertion (
  id           BLOB PRIMARY KEY,
  author       BLOB NOT NULL,      -- public key, inside the frame (I-1)
  kind         BLOB NOT NULL,
  akey         BLOB NOT NULL,
  arank        BLOB NOT NULL,      -- EXACTLY 16 bytes (A-2a); framed (A-2)
  ts           BLOB NOT NULL,
  ref_system   BLOB,               -- X-1. the reference is framed;
  ref_ident    BLOB,               --      the content is not
  ref_version  BLOB,
  supersedes   BLOB,
  body         BLOB NOT NULL
) WITHOUT ROWID;

-- (akey, arank, id) -- `id` is in the index because R-2 breaks rank ties by id,
-- and an index stopping at arank leaves the tiebreak to the query plan.
CREATE INDEX IF NOT EXISTS assertion_resolve ON assertion(akey, arank, id);
CREATE INDEX IF NOT EXISTS assertion_super   ON assertion(supersedes);
CREATE INDEX IF NOT EXISTS assertion_ref     ON assertion(ref_system, ref_ident);

-- The remove-set. NOTHING DELETES FROM THIS TABLE (W-2).
CREATE TABLE IF NOT EXISTS tombstone (
  id           BLOB PRIMARY KEY,   -- framed under its own tag (A-8)
  target       BLOB NOT NULL,
  target_kind  BLOB NOT NULL,      -- 'assertion' | 'reference' (W-12)
  tier         BLOB NOT NULL,
  reason       BLOB NOT NULL,      -- a CODE. Free text stays local (W-9)
  deadline     BLOB,               -- absolute, from this tombstone's ts (C-5)
  author       BLOB NOT NULL,
  ts           BLOB NOT NULL
) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS tombstone_target ON tombstone(target);

-- Authority. IMMUNE, for the same reason tombstones are (W-2).
CREATE TABLE IF NOT EXISTS grant (
  id         BLOB PRIMARY KEY,
  principal  BLOB NOT NULL,
  diary      BLOB NOT NULL,
  rights     INTEGER NOT NULL,     -- r|s|x (D-3)
  expires    BLOB,                 -- I-6
  author     BLOB NOT NULL,
  ts         BLOB NOT NULL,
  supersedes BLOB                  -- revocation is a superseding grant
) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS grant_who ON grant(diary, principal);

-- Grow-only on (id, signer). Signatures are rows, not a column: riding on the
-- assertion, an unsigned copy would shadow a signed one under insert-or-ignore.
-- `purpose` is stored because a signature is over (purpose || id) (A-8).
CREATE TABLE IF NOT EXISTS signature (
  id       BLOB NOT NULL,
  signer   BLOB NOT NULL,
  purpose  BLOB NOT NULL,
  sig      BLOB NOT NULL,
  PRIMARY KEY (id, signer, purpose)
) WITHOUT ROWID;

-- V-1. Derivation edges, queryable both ways, so erasing a source can reach the
-- summary computed from it (W-12, W-13).
CREATE TABLE IF NOT EXISTS derived (
  id      BLOB NOT NULL,        -- the derived assertion
  source  BLOB NOT NULL,        -- an assertion id or a reference key
  PRIMARY KEY (id, source)
) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS derived_source ON derived(source);

-- W-1a. The undo log and the heap. A superseded or redacted VALUE moves here
-- rather than being destroyed in place, so the default destructive act is
-- recoverable and true destruction is purging the heap -- which makes the heap,
-- not `assertion`, what a retention deadline applies to.
CREATE TABLE IF NOT EXISTS heap (
  id        BLOB PRIMARY KEY,
  tier      BLOB NOT NULL,
  wrapped   BLOB,               -- the content key, wrapped to a custodian
  bytes     BLOB,               -- opaque
  deadline  BLOB
) WITHOUT ROWID;

-- X-4. A PROJECTION: bounded, evictable, NEVER merged. Each device caches
-- independently, which is what lets the phone hold every assertion and almost
-- no content.
CREATE TABLE IF NOT EXISTS cache (
  ref_system  TEXT NOT NULL,
  ref_ident   TEXT NOT NULL,
  ref_version TEXT NOT NULL,
  bytes       BLOB NOT NULL,
  pinned      INTEGER NOT NULL DEFAULT 0,   -- X-6. exempt from eviction
  fetched_at  TEXT NOT NULL,
  PRIMARY KEY (ref_system, ref_ident, ref_version)
) WITHOUT ROWID;

-- X-2. What we know about a referent independent of whether we hold bytes.
-- GONE is recorded, because an absent cache row means AWAY and the two must
-- never be conflated (X-3).
CREATE TABLE IF NOT EXISTS referent (
  ref_system  TEXT NOT NULL,
  ref_ident   TEXT NOT NULL,
  state       TEXT NOT NULL,     -- 'away' | 'gone'
  seen_at     TEXT NOT NULL,
  PRIMARY KEY (ref_system, ref_ident)
) WITHOUT ROWID;

-- D-1, D-6. A store that cannot say where it sends its contents is not usable.
CREATE TABLE IF NOT EXISTS diary_policy (
  diary     TEXT PRIMARY KEY,
  policy    TEXT NOT NULL,       -- 'never' | 'domain' | 'published'
  endpoint  TEXT
) WITHOUT ROWID;

-- V-3. The reviewable record of what crossed the boundary.
CREATE TABLE IF NOT EXISTS publication (
  id        BLOB PRIMARY KEY,
  source    BLOB NOT NULL,
  dest      BLOB NOT NULL,
  ts        BLOB NOT NULL
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS publication_item (
  publication BLOB NOT NULL,
  assertion   BLOB NOT NULL,
  PRIMARY KEY (publication, assertion)
) WITHOUT ROWID;

-- Local, never merged. `seq` orders MY receipts, not their writes (C-2), and
-- it is what S-4's sender-side watermark counts.
CREATE TABLE IF NOT EXISTS arrival (
  seq  INTEGER PRIMARY KEY AUTOINCREMENT,
  id   TEXT NOT NULL UNIQUE
);

-- S-4. PER PEER, because a watermark is sender-side and means nothing to
-- anyone else: "I have given P everything up to MY n". Also S-7's resume
-- point and S-11's verified high-water mark, which is why they live together
-- -- all three answer "how far did I get with this peer".
CREATE TABLE IF NOT EXISTS peer (
  peer        TEXT PRIMARY KEY,
  given_thru  INTEGER NOT NULL DEFAULT 0,   -- S-4, S-6: rounded DOWN
  cursor      TEXT,                         -- S-7: resume token
  last_sync   TEXT                          -- D-6
) WITHOUT ROWID;

-- S-11. Rows this store has already verified, so incremental transfer does not
-- cost a full verification. Local and never merged: a peer's claim to have
-- verified something is not evidence.
CREATE TABLE IF NOT EXISTS verified (
  id  TEXT PRIMARY KEY
) WITHOUT ROWID;
"""


def install(conn) -> None:
    """Create the schema on an open connection.

    Does not open, key or locate anything: the host opened this connection and
    has therefore already chosen the key, the file and the platform's rules.

    NOTE what is absent: no authorizer and no filtered view. v1 had both, to
    enforce per-assertion permissions that v2 deletes -- and both were measured
    broken anyway (FINDINGS.md F-1, C-6). Access control is per diary now
    (D-3), which is enforced where a diary is opened, not inside SQL.
    """
    raise NotImplementedError("schema")
