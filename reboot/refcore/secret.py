"""The travelling secret: a pepper over the copy that leaves the device.

An identity's private key is sealed twice -- once under a key the owner's own
device derives in one kdf, and once under a key whose nonce carries `b` random
bits that are never stored. Only the second copy syncs.

    nonce         = random, stored
    pepper        = b uniform random bits, NEVER STORED
    k_local       = kdf(password, nonce)
    k_travel      = kdf(password, nonce XOR pepper)
    local_secret  = encrypt(secret, k_local)
    travel_secret = encrypt(secret, k_travel)

WHAT THIS BUYS: the owner pays the 2^(b-1) expected search ONCE, at enrolment,
and never again -- everyday unlock uses `local_secret` and one kdf. An attacker
pays it on every password guess. A kdf slow enough to impose the same per-guess
cost would be paid by the owner every single time (N-12).

WHY NOT A TIME-LOCK PUZZLE, since it is the obvious alternative and was the
first choice here. RSW-style repeated squaring makes each attempt serial, which
sounds like it defeats parallel attack and does not: cracking parallelizes
across GUESSES, not within one, so serial work is a per-guess multiplier exactly
like this search is. Given that, the pepper wins three ways (N-13):

  - THE DEFENDER MAY PARALLELIZE ITS SEARCH and the attacker's advantage is
    unchanged, so for a fixed enrolment wall-clock the attacker's per-guess work
    is larger by the defender's core count. Sequential work forbids the defender
    that and gains nothing for it.
  - The whole search is kdf evaluations, so MEMORY-HARDNESS MULTIPLIES through
    all 2^b of them. Modular squaring is low-memory and ASIC-friendly, which is
    the opposite of what constrains cracking hardware.
  - No dependency. The time-lock needs bignum arithmetic; this needs none.

And no structural break: a pepper is entropy, with no factoring assumption, no
trapdoor to destroy, and no quantum endgame beyond Grover halving b (N-15).
"""


def seal(secret: bytes, password: str, nonce: bytes, b: int) -> tuple:
    """N-10, N-11. Returns (local_secret, travel_secret, verifier).

    Generates `b` random bits, uses them, and DISCARDS them. The pepper must not
    survive this call in any form -- not returned, not logged, not left in a
    buffer a later allocation can read. A retained pepper collapses the travel
    seal to plain kdf strength, silently, and unprovably (K4).
    """
    raise NotImplementedError("N-10, N-11")


def unlock_local(local_secret: bytes, password: str, nonce: bytes) -> bytes:
    """The everyday path: one kdf, no search. Milliseconds.

    This is what makes N-12's asymmetry available at all -- if the owner had to
    search on every unlock, the cost would have to be small enough to be useless.
    """
    raise NotImplementedError("N-10")


def unlock_travel(travel_secret: bytes, password: str, nonce: bytes, b: int,
                  verifier: bytes, workers: int = 1) -> bytes:
    """The enrolment path: search all 2^b peppers, expected 2^(b-1).

    `workers` is the defender's parallelism and it is the point (N-13): the
    attacker's per-guess work is unchanged by it, so every core the owner brings
    to enrolment buys a proportional increase in what an attacker must spend per
    guess for the same enrolment wall-clock.

    Checks `verifier` FIRST (N-16a). Without that a wrong password is
    indistinguishable from a pepper not yet found, so a typo costs the full 2^b
    -- terrible to use, and a denial of service against yourself.
    """
    raise NotImplementedError("N-10, N-13, N-16a")


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

    Exposed because the cost is PROBABILISTIC and enrolment time varies by up to
    a factor of two. A progress indicator that assumes a fixed cost will lie.
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
