"""test_store.py: viki_store through its C interface, against a separate model.

The Python here computes ids and resolves chains on its own, from the
requirements, so a framing or resolution bug has to be made twice to pass.
Histories are random but seeded. Prints the lines gate.py reads.
"""
import ctypes as C
import hashlib
import os
import random
import sys

LIB = C.CDLL(os.environ["VIKI_STORE_LIB"])
SQL = C.CDLL("libsqlite3.so.0")
OK, EXISTS, DONE, NOTFOUND, FORKED = 0, 1, 2, 3, 4
ALL = 15
Id = C.c_ubyte * 32


class Bytes(C.Structure):
    _fields_ = [("p", C.c_char_p), ("n", C.c_uint32)]


class Assertion(C.Structure):
    _fields_ = [("author", Bytes), ("kind", Bytes), ("akey", Bytes), ("ts", Bytes), ("reference", Bytes),
                ("rank", C.c_ubyte * 16), ("supersedes", C.POINTER(Id)), ("nSupersedes", C.c_uint32),
                ("derivedFrom", C.POINTER(Id)), ("nDerivedFrom", C.c_uint32), ("body", Bytes),
                ("erased", C.c_int), ("bodyHash", C.c_ubyte * 32)]


class Grant(C.Structure):
    _fields_ = [("principal", Bytes), ("rights", C.c_int)]


class Row(C.Structure):
    _fields_ = [("id", Id), ("body", Bytes), ("erased", C.c_int), ("nArms", C.c_int), ("pSt", C.c_void_p)]


class Report(C.Structure):
    _fields_ = [("added", C.c_int64), ("present", C.c_int64), ("quarantined", C.c_int64), ("incomplete", C.c_int)]


class Search(C.Structure):
    _fields_ = [("p", C.c_void_p), ("m", C.c_void_p), ("pSt", C.c_void_p)]


LIB.vk_open.argtypes = [C.c_void_p, Bytes, C.POINTER(Grant), C.c_uint32, C.POINTER(C.c_void_p)]
LIB.vk_close.argtypes = [C.c_void_p]
LIB.vk_ingest.argtypes = [C.c_void_p, C.POINTER(Assertion), C.POINTER(Id), C.POINTER(Report)]
LIB.vk_merge.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(Report)]
LIB.vk_digest.argtypes = [C.c_void_p, C.POINTER(Id)]
LIB.vk_verify.argtypes = [C.c_void_p, C.POINTER(C.c_int64)]
LIB.vk_resolve.argtypes = [C.c_void_p, Bytes, C.POINTER(Row)]
LIB.vk_row_close.argtypes = [C.POINTER(Row)]
LIB.vk_search_open.argtypes = [C.c_void_p, C.c_void_p, Bytes, C.POINTER(Search)]
LIB.vk_search_next.argtypes = [C.POINTER(Search), C.POINTER(Id)]
LIB.vk_search_close.argtypes = [C.POINTER(Search)]
SQL.sqlite3_open.argtypes = [C.c_char_p, C.POINTER(C.c_void_p)]
SQL.sqlite3_close.argtypes = [C.c_void_p]
GREP = C.addressof(C.c_void_p.in_dll(LIB, "vkGrep"))
ENTITLED = {b"alice", b"bob"}


def b(v):
    return Bytes(v, len(v))


def field(v):
    return len(v).to_bytes(4, "big") + v


def frame_id(r):
    """The id, computed from REQUIREMENTS_v3's rule and not from the C."""
    links = b"".join(len(ls).to_bytes(4, "big") + b"".join(sorted(ls)) for ls in (r["sup"], r["der"]))
    return hashlib.sha256(b"viki.assertion.v3\0" + field(r["author"]) + field(r["kind"]) + field(r["akey"])
                          + field(r["ts"]) + r["rank"] + field(r["ref"]) + links
                          + hashlib.sha256(r["body"]).digest()).digest()


class Peer:
    def __init__(self):
        self.db, self.st = C.c_void_p(), C.c_void_p()
        assert SQL.sqlite3_open(b":memory:", C.byref(self.db)) == 0
        self.names = [b"alice", b"bob"]
        self.grants = (Grant * 2)(*[Grant(b(n), ALL) for n in self.names])
        assert LIB.vk_open(self.db, b(b"alice"), self.grants, 2, C.byref(self.st)) == OK

    def close(self):
        LIB.vk_close(self.st)
        assert SQL.sqlite3_close(self.db) == 0

    def ingest(self, r):
        sup = (Id * max(1, len(r["sup"])))(*[Id(*i) for i in r["sup"]])
        der = (Id * max(1, len(r["der"])))(*[Id(*i) for i in r["der"]])
        a = Assertion(author=b(r["author"]), kind=b(r["kind"]), akey=b(r["akey"]), ts=b(r["ts"]), reference=b(r["ref"]),
                      rank=(C.c_ubyte * 16)(*r["rank"]), body=b(r["body"]),
                      supersedes=C.cast(sup, C.POINTER(Id)), nSupersedes=len(r["sup"]),
                      derivedFrom=C.cast(der, C.POINTER(Id)), nDerivedFrom=len(r["der"]))
        rep = Report()
        assert LIB.vk_ingest(self.st, C.byref(a), C.byref(Id(*r["id"])), C.byref(rep)) == OK
        return rep

    def digest(self):
        d = Id()
        assert LIB.vk_digest(self.st, C.byref(d)) == OK
        return bytes(d)

    def bad_ids(self):
        n = C.c_int64(-1)
        assert LIB.vk_verify(self.st, C.byref(n)) == OK
        return n.value

    def resolve(self, akey):
        row = Row()
        rc = LIB.vk_resolve(self.st, b(akey), C.byref(row))
        if rc == NOTFOUND:
            return None
        assert rc == OK
        out = (bytes(row.id), bool(row.erased), row.nArms)
        LIB.vk_row_close(C.byref(row))
        return out

    def grep(self, q):
        s, hit, found = Search(), Id(), set()
        assert LIB.vk_search_open(self.st, GREP, b(q), C.byref(s)) == OK
        while LIB.vk_search_next(C.byref(s), C.byref(hit)) == OK:
            found.add(bytes(hit))
        LIB.vk_search_close(C.byref(s))
        return found


class Model:
    """What the requirements say the union of these rows means."""

    def __init__(self, rows):
        self.rows = {r["id"]: r for r in rows}

    def valid(self, r):
        return r["author"] in ENTITLED

    def is_tomb(self, r):
        return r["kind"] == b"tombstone"

    def erased(self, r):
        if self.is_tomb(r):
            return False
        return any(self.is_tomb(t) and self.valid(t) and t["akey"] == r["id"] for t in self.rows.values())

    def superseded(self):
        return {p for r in self.rows.values() if self.valid(r) for p in r["sup"]}

    def resolve(self, akey):
        gone = self.superseded()
        arms = [r for r in self.rows.values() if r["akey"] == akey and self.valid(r) and r["id"] not in gone]
        if not arms:
            return None
        top = max(arms, key=lambda r: (r["rank"], r["id"]))
        return (top["id"], self.erased(top), len(arms))

    def grep(self, q):
        return {r["id"] for r in self.rows.values()
                if self.valid(r) and not self.erased(r) and q in r["body"]}


def new_row(rng, n, known):
    author = rng.choice([b"alice", b"bob", b"mallory"])
    akey = rng.choice([b"a", b"b", b"c"])
    same = [i for i, k in known if k == akey]
    k = rng.randint(0, min(3, len(same)))
    r = dict(author=author, kind=b"note", akey=akey, ts=b"t%d" % n, ref=b"",
             rank=bytes(15) + bytes([rng.randint(0, 3)]), sup=rng.sample(same, k), der=[],
             body=(b"tok%d \0\xff " % rng.randint(0, 5)) + b"x" * rng.randint(0, 9))
    r["id"] = frame_id(r)
    return r


def tombstone(rng, n, target):
    r = dict(author=rng.choice([b"alice", b"mallory"]), kind=b"tombstone", akey=target, ts=b"e%d" % n, ref=b"",
             rank=bytes(16), sup=[], der=[], body=target)
    r["id"] = frame_id(r)
    return r


def one_history(seed):
    rng = random.Random(seed)
    peers = [Peer() for _ in range(3)]
    rows, known = [], []
    for n in range(40):
        r = tombstone(rng, n, rng.choice(known)[0]) if known and rng.random() < 0.15 else new_row(rng, n, known)
        rows.append(r)
        if r["kind"] == b"note":
            known.append((r["id"], r["akey"]))
        peers[rng.randrange(3)].ingest(r)
        if rng.random() < 0.3:
            x, y = rng.sample(peers, 2)
            assert LIB.vk_merge(x.st, y.st, C.byref(Report())) == OK
    for _ in range(2):
        for x in peers:
            for y in peers:
                assert LIB.vk_merge(x.st, y.st, C.byref(Report())) == OK
    model = Model(rows)
    assert len({p.digest() for p in peers}) == 1, "peers did not converge"
    for p in peers:
        assert p.bad_ids() == 0, "a stored id does not recompute"
        for akey in (b"a", b"b", b"c"):
            assert p.resolve(akey) == model.resolve(akey), f"resolve {akey} seed {seed}"
        for t in range(6):
            q = b"tok%d" % t
            assert p.grep(q) == model.grep(q), f"grep {q} seed {seed}"
    for p in peers:
        p.close()


def main():
    try:
        for seed in range(200):
            one_history(seed)
    except AssertionError as e:
        print(f"  {e}")
        print("FAIL M-1 random histories converge and match the model")
        print("FAIL R-3 random histories resolve as the model does")
        return 1
    print("PASS M-1 200 random histories converge and match the model")
    print("PASS R-3 200 random histories resolve as the model does")
    return 0


if __name__ == "__main__":
    sys.exit(main())
