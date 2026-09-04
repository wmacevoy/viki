"""The travelling secret: one seal, and a pepper nobody keeps.

An identity's private key is sealed ONCE. The nonce in the identity assertion is
the PEPPERED one; the true kdf input is `nonce XOR pepper`, and the pepper was
discarded the moment it was used.

    pepper         = b uniform random bits, used once and DISCARDED
    key            = kdf(password, nonce XOR pepper)
    secret_wrapped = encrypt(secret, key)

NOBODY HOLDS THE PEPPER, which is the whole economy of it: there is nothing to
protect, leak, sync by accident, or fail to erase. The secret is seasoned in
transit rather than guarded at rest. A device caches the derived KEY -- which it
either just computed, at creation, or found by searching, at enrolment -- and
that cache is local and never syncs.

Two seals under two keys was the first draft and is strictly worse: two
ciphertexts to keep consistent, for an asymmetry that needs neither (N-10).

WHAT THIS BUYS: the creator pays nothing (it has the key already), a new device
pays the 2^(b-1) expected search once and then caches, and an attacker pays it on
every password guess forever. A kdf slow enough to impose the same per-guess cost
would be paid by the owner on every unlock (N-12a).

WHY NOT A TIME-LOCK PUZZLE, since it was the first choice here. RSW-style
repeated squaring makes each attempt serial, which sounds like it defeats
parallel attack and does not: cracking parallelizes across GUESSES, not within
one. Given that, the pepper wins three ways (N-13):

  - THE DEFENDER MAY PARALLELIZE ITS SEARCH and the attacker's advantage is
    unchanged, so for a fixed enrolment wall-clock the attacker's per-guess work
    is larger by the defender's core count. Sequential work forbids the defender
    that and gains nothing for it.
  - The whole search is kdf evaluations, so MEMORY-HARDNESS MULTIPLIES through
    all 2^b of them. Modular squaring is low-memory and ASIC-friendly.
  - No dependency. The time-lock needs bignum arithmetic; this needs none.

And no structural break: a pepper is entropy, with no factoring assumption, no
trapdoor, and no quantum endgame beyond Grover halving b (N-15).
"""


def derive(password: str, nonce: bytes, pepper: bytes) -> bytes:
    """N-10, N-14. The key. ONE derivation, the same everywhere.

    `kdf` must be memory-hard, and here that is load-bearing rather than hygiene:
    an attacker evaluates it 2^(b-1) times per password guess, so its resistance
    to parallel hardware multiplies through the entire search.
    """
    raise NotImplementedError("N-10, N-14")


def seal(secret: bytes, password: str, b: int) -> tuple:
    """N-10, N-11, N-16a. Returns (peppered_nonce, secret_wrapped, verifier, key).

    Generates the pepper, uses it, and DISCARDS it -- it is not returned and must
    not survive this call. `key` comes back only so the creating device can cache
    what it has already computed (N-12); it is not stored and does not sync.

    The verifier lets a wrong password fail in one kdf rather than after
    exhausting the search.
    """
    raise NotImplementedError("N-10, N-11")


def unlock(secret_wrapped: bytes, key: bytes) -> bytes:
    """The everyday path: the cached key is already here. No kdf, no search.

    A device caches the KEY, never the pepper -- so even a stolen cache reveals
    nothing about the pepper, and losing the cache costs exactly what a new
    device costs, which is the correct behaviour.
    """
    raise NotImplementedError("N-12")


def enrol(secret_wrapped: bytes, password: str, nonce: bytes, b: int,
          verifier: bytes, workers: int = 1) -> tuple:
    """N-12, N-12a, N-13, N-16a. The new-device path. Returns (secret, key).

    Searches all 2^b peppers, expected 2^(b-1), and returns the derived KEY so
    the caller can cache it -- after which this device never searches again.
    Enrolment is the only time anyone pays.

    `workers` is the defender's parallelism and it is the point (N-13): the
    attacker's per-guess work is unchanged by it, so every core the owner brings
    here raises what an attacker must spend per guess for the same enrolment
    wall-clock.

    Checks `verifier` FIRST (N-16a), or a typo costs the full 2^b and is
    indistinguishable from a pepper not yet found.
    """
    raise NotImplementedError("N-12, N-12a, N-13")


def verify_password(verifier: bytes, password: str, nonce: bytes) -> bool:
    """N-16a. One kdf. Fails a typo fast, before any search begins."""
    raise NotImplementedError("N-16a")


def parameters(store, pubkey: str) -> dict:
    """N-16. `b` and the kdf parameters, recorded WITH the identity.

    A peer that cannot tell how large the search is cannot perform it, and
    raising `b` later must not orphan identities sealed under the old value.
    """
    raise NotImplementedError("N-16")


def expected_cost(b: int) -> tuple:
    """N-16b. (expected, worst_case) kdf evaluations: 2^(b-1) and 2^b.

    Exposed because the cost is PROBABILISTIC and enrolment varies by up to a
    factor of two. A progress indicator that assumes a fixed cost will lie.
    """
    raise NotImplementedError("N-16b")


def rekey(store, new_roots: tuple) -> str:
    """N-17. Device removal: a re-key, not a delete.

    Unwrapping a recipient does not un-tell them a key they already hold. So
    removal generates a new database key, re-wraps it to the remaining roots,
    and re-encrypts. The removed device keeps everything it had and learns
    nothing after -- forward-only, like every other revocation here.

    Shredding a diary and shredding its keys are the same operation, which is
    what makes this tractable rather than impossible.
    """
    raise NotImplementedError("N-17")
