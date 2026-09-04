"""The travelling secret: a salt with its low bits dropped in transit.

There is no separate pepper value. The hardening IS the salt, minus however many
low bits the row gives up when it travels.

    salt           = 32 random bytes, REDRAWN if its low `b` bits are all zero
    key            = kdf(password, salt)
    secret_wrapped = encrypt(secret, key)

The identity assertion carries the salt with its low `b` bits zeroed. Anyone who
wants the secret searches those 2^b values, recovers the original salt, and keeps
it locally -- after which that device never searches again.

WHAT THIS BUYS: the creator pays nothing, having drawn the salt. Each other
device pays the 2^(b-1) expected search ONCE. An attacker pays it on every
password guess, forever. A kdf slow enough to impose the same per-guess cost
would be paid by the owner on every unlock, which is exactly why it is not an
alternative (N-12a).

TWO DETAILS THAT LOOK SMALL AND ARE NOT.

The redraw (N-11): without it, one identity in 2^b ships a salt identical to its
real one and has no hardening whatsoever -- silently, and indistinguishably from
every other identity.

The recovered salt never syncs (N-12): it is the ANSWER to the search, so a peer
that shared it would broadcast the answer and the whole construction would
evaporate. Each device searches for itself.

WHY NOT A TIME-LOCK PUZZLE, since it was the first choice here. RSW-style
repeated squaring makes each attempt serial, which sounds like it defeats
parallel attack and does not: cracking parallelizes across GUESSES, not within
one. Given that, the truncated salt wins three ways (N-13): the DEFENDER may
parallelize its search while the attacker's advantage is unchanged; the whole
search is kdf evaluations, so memory-hardness multiplies through all 2^b of them,
where modular squaring is low-memory and ASIC-friendly; and it needs no bignum
dependency.
"""


def new_salt(b: int) -> bytes:
    """N-11. 32 random bytes, redrawn while its low `b` bits are all zero.

    The redraw is not fastidiousness. Without it one identity in 2^b ships a
    salt identical to its real one -- no hardening at all, silently, and looking
    exactly like every other identity.
    """
    raise NotImplementedError("N-11")


def truncate(salt: bytes, b: int) -> bytes:
    """N-10. The travelling form: the salt with its low `b` bits zeroed.

    This is what the identity assertion carries, permanently and everywhere
    (N-12b). The full salt is never in an assertion.
    """
    raise NotImplementedError("N-10")


def derive(password: str, salt: bytes) -> bytes:
    """N-14. The key. One kdf, and it must be MEMORY-HARD.

    Load-bearing rather than hygiene: an attacker evaluates this 2^(b-1) times
    per password guess, so its resistance to parallel hardware multiplies
    through the entire search. It is the property the rejected time-lock could
    not offer.
    """
    raise NotImplementedError("N-14")


def seal(secret: bytes, password: str, b: int) -> tuple:
    """N-10, N-11, N-16a. Returns (salt, truncated_salt, secret_wrapped, verifier).

    `salt` comes back for the creator to store locally (N-12) -- it drew the
    salt, so it never searches. `truncated_salt` is what goes in the assertion.
    """
    raise NotImplementedError("N-10, N-11")


def unlock(secret_wrapped: bytes, password: str, salt: bytes) -> bytes:
    """The everyday path: the full salt is already in `salt_local`, so one kdf."""
    raise NotImplementedError("N-10")


def enrol(secret_wrapped: bytes, password: str, truncated_salt: bytes, b: int,
          verifier: bytes, workers: int = 1) -> tuple:
    """N-12, N-12a, N-13, N-16a. The new-device path. Returns (secret, salt).

    Searches the 2^b values of the dropped bits and RETURNS THE RECOVERED SALT,
    so the caller can reset its own row and never search again.

    `workers` is the defender's parallelism and it is the point (N-13): the
    attacker's per-guess work is unchanged by it, so every core the owner brings
    to enrolment raises what an attacker must spend per guess for the same
    enrolment wall-clock.

    Checks `verifier` FIRST (N-16a), or a typo costs the full 2^b and is
    indistinguishable from a salt not yet found.
    """
    raise NotImplementedError("N-12, N-12a, N-13")


def remember_salt(store, identity_id: str, salt: bytes) -> None:
    """N-12, N-12a. Write the recovered salt to `salt_local`. NEVER syncs.

    It is the answer to the search: a peer that shared it would broadcast the
    answer and the hardening would evaporate.
    """
    raise NotImplementedError("N-12")


def local_salt(store, identity_id: str) -> bytes | None:
    """The full salt if this device has it, else None -- meaning search first."""
    raise NotImplementedError("N-12")


def verify_password(verifier: bytes, password: str, truncated_salt: bytes) -> bool:
    """N-16a. One kdf. Fails a typo fast, before any search begins."""
    raise NotImplementedError("N-16a")


def parameters(store, pubkey: str) -> dict:
    """N-16. `b` and the kdf parameters, carried WITH the identity.

    A peer that cannot tell how many bits were dropped cannot search for them,
    and raising `b` later must not orphan identities sealed under the old value.
    """
    raise NotImplementedError("N-16")


def expected_cost(b: int) -> tuple:
    """N-16b. (expected, worst_case) kdf evaluations: 2^(b-1) and 2^b.

    Exposed because the cost is PROBABILISTIC and enrolment varies by up to a
    factor of two. A progress indicator assuming a fixed cost will lie.
    """
    raise NotImplementedError("N-16b")


def rekey(store, new_roots: tuple) -> str:
    """N-17. Device removal: a re-key, not a delete.

    Unwrapping a recipient does not un-tell them a key they already hold, so
    removal generates a new database key, re-wraps it to the remaining roots and
    re-encrypts. The removed device keeps everything it had and learns nothing
    after -- forward-only, like every other revocation here.
    """
    raise NotImplementedError("N-17")
