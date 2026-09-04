"""Opening a store, and the principal bound to it.

THE CONSTRUCTOR IS INERT ON PURPOSE. It holds references and does nothing else:
no I/O, no schema, no clock read, no refusal. v1's raised NotImplementedError,
which meant 81% of the suite died here and never reached the function it named,
while a verification counting exception types reported success (FINDINGS.md
C-1). A skeleton whose constructor fails is a skeleton whose tests specify
nothing.
"""


class Store:
    """One SQLite database, one diary, one principal for as long as it is open.

    P-7's rationale survives v2 even though the permission apparatus did not:
    the principal is bound here and a different principal gets a different
    Store. What is gone is the per-assertion filter, the view and the
    authorizer -- access is per diary now (D-3), decided where a diary is
    opened rather than inside SQL.
    """

    def __init__(self, conn, *, diary, principal, clock,
                 signer=None, verifier=None, custodian=None, fetcher=None,
                 kinds=(), grants=(), policy=None, endpoint=None):
        self.conn = conn
        self.diary = diary
        self.principal = principal
        self.clock = clock
        self.signer = signer
        self.verifier = verifier
        self.custodian = custodian
        self.fetcher = fetcher
        self.kinds = {k.name: k for k in kinds}
        self.grants = tuple(grants)
        self.policy = policy
        self.endpoint = endpoint

    def kind(self, name):
        """R-6. The registered KindSpec, or None. Wired, not merely declared."""
        return self.kinds.get(name)

    def savepoint(self, name: str):
        """A context manager. SAVEPOINT, never BEGIN/COMMIT: the caller owns
        this connection and may be inside its own transaction, and a bare COMMIT
        from a library commits the host's work at a point of the library's
        choosing."""
        raise NotImplementedError("store")
