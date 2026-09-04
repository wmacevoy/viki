"""The travelling secret: a time-lock puzzle over the copy that leaves the device.

Rivest-Shamir-Wagner 1996, adapted. An identity's private key is sealed twice --
once under a key the owner's own device can derive in milliseconds, and once
under a key that costs `t` sequential squarings to reach. Only the second copy
syncs.

    nonce         = p * q            -- p and q DESTROYED, never stored
    k_local       = kdf(password, nonce)
    k_travel      = k_local^(2^t) mod nonce
    local_secret  = encrypt(secret, k_local)
    travel_secret = encrypt(secret, k_travel)

WHAT THIS ACTUALLY BUYS, stated precisely because the tempting claim is wrong.
It is NOT that sequential work defeats parallel attack: password cracking
parallelizes across GUESSES, not within one, so an attacker with N cores runs N
candidate passwords each with its own chain, and `t` behaves like a KDF cost
parameter. What it buys is WHEN THE DEFENDER PAYS -- once, at enrolment, rather
than on every unlock. A KDF slow enough to impose the same per-guess cost would
be paid by the owner every single time they open the diary. That asymmetry is
the whole construction (N-13).

TWO HAZARDS, both structural:
  - the factors are the trapdoor, so a retained p or q voids everything
    SILENTLY and unprovably (N-12);
  - modular squaring is low-memory and ASIC-friendly, the opposite of a
    memory-hard KDF, so this hardens against TIME and not against AREA and
    `kdf` must be memory-hard on its own account (N-15).

Bignum arithmetic comes from libbf (via libbfxx). This is the one dependency the
construction adds, and it is deliberate: the alternative is hand-rolled modular
exponentiation, which is the last thing anybody should hand-roll.
"""


def make_nonce(bits: int) -> int:
    """N-11, N-12. Generate p and q, return p*q, and DESTROY the factors.

    The factors must not survive this call in any form -- not returned, not
    logged, not left in a scratch buffer a later allocation can read. With phi(n)
    an attacker computes 2^t mod phi(n) and collapses the whole chain into one
    exponentiation, so holding a factor is equivalent to not having built this.

    Nobody may hold them, INCLUDING THE OWNER. There is no legitimate use for
    the shortcut: the owner already has `local_secret`.
    """
    raise NotImplementedError("N-11, N-12")


def k_local(password: str, nonce: int) -> bytes:
    """N-15. The fast key. A MEMORY-HARD kdf, on its own account.

    The time-lock hardens against time; a memory-hard kdf hardens against area.
    They defend different axes and neither substitutes for the other, so this
    being slow-and-memory-hungry is not redundant with `t`.
    """
    raise NotImplementedError("N-15")


def k_travel(k: bytes, nonce: int, t: int) -> bytes:
    """N-11. `k` squared `t` times mod `nonce`. Sequential by construction.

    There is no way to reach the t-th square without passing through the
    previous t-1 -- unless you know the factors, which is why N-12 destroys them.
    """
    raise NotImplementedError("N-11")


def seal(secret: bytes, password: str, nonce: int, t: int) -> tuple:
    """N-10. Returns (local_secret, travel_secret).

    Only the second one syncs.
    """
    raise NotImplementedError("N-10")


def unlock_local(local_secret: bytes, password: str, nonce: int) -> bytes:
    """The everyday path: one memory-hard kdf, no squaring. Milliseconds."""
    raise NotImplementedError("N-10")


def unlock_travel(travel_secret: bytes, password: str, nonce: int,
                  t: int) -> bytes:
    """The enrolment path: the kdf plus `t` sequential squarings.

    Paid once, by the owner, on a new device. An attacker pays it on every
    guess -- which is the entire point (N-13).
    """
    raise NotImplementedError("N-10, N-13")


def parameters(store, pubkey: str) -> dict:
    """N-16. The `t` and nonce size recorded WITH the identity.

    A peer that cannot tell how much work `travel_secret` requires cannot unlock
    it, and raising `t` later must not orphan identities sealed under the old
    value.
    """
    raise NotImplementedError("N-16")


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
