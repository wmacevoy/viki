"""vkstore.py -- core/include/viki_store.h for Python hosts, through ctypes.

The structures mirror the header field for field; a change there is a change
here. The SQLite calls come from the same library handle, so the host and the
store always share one SQLite (a SQLCipher build supplies sqlite3_key too).
"""
import ctypes as C
import json
import os

OK, EXISTS, DONE, NOTFOUND, FORKED, BLIND, EINVAL, EDENIED, ENOMEM, ESQL = range(10)
ERASE, READ, SUPERSEDE, ORIGINATE = 1, 2, 4, 8
Id = C.c_ubyte * 32
MAX_LINKS, MAX_SKIP = 16, 32


class Bytes(C.Structure):
    # A raw pointer, not c_char_p: ctypes would hand a c_char_p back as a
    # Python bytes cut at the first NUL, and a body is bytes (A-4a).
    _fields_ = [("p", C.c_void_p), ("n", C.c_uint32)]


class Assertion(C.Structure):
    _fields_ = [("author", Bytes), ("kind", Bytes), ("akey", Bytes), ("ts", Bytes), ("reference", Bytes),
                ("rank", C.c_ubyte * 16), ("supersedes", C.POINTER(Id)), ("nSupersedes", C.c_uint32),
                ("derivedFrom", C.POINTER(Id)), ("nDerivedFrom", C.c_uint32), ("body", Bytes),
                ("erased", C.c_int), ("bodyHash", C.c_ubyte * 32),
                ("log", Bytes), ("pos", C.c_uint64), ("skip", C.POINTER(Id)), ("nSkip", C.c_uint32)]


class Grant(C.Structure):
    _fields_ = [("principal", Bytes), ("rights", C.c_int)]


class Row(C.Structure):
    _fields_ = [("id", Id), ("body", Bytes), ("erased", C.c_int), ("nArms", C.c_int), ("pSt", C.c_void_p)]


class Cursor(C.Structure):
    _fields_ = [("p", C.c_void_p), ("pRow", C.c_void_p), ("pEdge", C.c_void_p), ("a", Assertion),
                ("id", Id), ("seq", C.c_int64), ("links", (Id * MAX_LINKS) * 2), ("skip", Id * MAX_SKIP)]


class Search(C.Structure):
    _fields_ = [("p", C.c_void_p), ("m", C.c_void_p), ("pSt", C.c_void_p), ("pNode", C.c_void_p)]


class Coverage(C.Structure):
    _fields_ = [("zMethod", C.c_char_p), ("bExhaustive", C.c_int), ("nSearched", C.c_int64),
                ("nWhole", C.c_int64)]


class Hit(C.Structure):
    _fields_ = [("bNode", C.c_int), ("lo", C.c_int64), ("hi", C.c_int64), ("depth", C.c_int), ("path", Bytes),
                ("bRange", C.c_int)]


def _default_lib():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "..", "core", "build-store", "libviki_store.so")


LIB = C.CDLL(os.environ.get("VIKI_STORE_LIB") or _default_lib())
P = C.c_void_p
for name, args, res in [
    ("vk_open", [P, Bytes, C.POINTER(Grant), C.c_uint32, C.POINTER(P)], C.c_int),
    ("vk_close", [P], C.c_int),
    ("vk_errmsg", [P], C.c_char_p),
    ("vk_put", [P, C.POINTER(Assertion), C.POINTER(Id)], C.c_int),
    ("vk_put_chain", [P, C.POINTER(Assertion), C.POINTER(Id)], C.c_int),
    ("vk_append", [P, C.POINTER(Assertion), C.POINTER(Id)], C.c_int),
    ("vk_forget", [P, Bytes, Bytes, C.c_uint64, C.c_uint64, Bytes, C.POINTER(C.c_ubyte * 16), C.POINTER(Id)], C.c_int),
    ("vk_log_span", [P, Bytes, Bytes, C.POINTER(C.c_uint64), C.POINTER(C.c_uint64)], C.c_int),
    ("vk_current", [P, Bytes, C.POINTER(Row)], C.c_int),
    ("vk_row_close", [C.POINTER(Row)], None),
    ("vk_get", [P, C.POINTER(Id), C.POINTER(Cursor)], C.c_int),
    ("vk_cursor_open", [P, C.c_int64, C.POINTER(Cursor)], C.c_int),
    ("vk_cursor_next", [C.POINTER(Cursor)], C.c_int),
    ("vk_cursor_close", [C.POINTER(Cursor)], None),
    ("vk_standing", [P, Bytes, C.POINTER(Id), C.POINTER(C.c_int), C.POINTER(C.c_int)], C.c_int),
    ("vk_bm25_enable", [P], C.c_int),
    ("vk_search_open", [P, P, Bytes, C.POINTER(Search)], C.c_int),
    ("vk_search_next", [C.POINTER(Search), C.POINTER(Id)], C.c_int),
    ("vk_search_hit", [C.POINTER(Search), C.POINTER(Hit)], C.c_int),
    ("vk_use_outlines", [P, Bytes], None),
    ("vk_use_vectors", [P, Bytes], None),
    ("vk_vector_drop", [P, C.POINTER(Id), Bytes], C.c_int),
    ("vk_vector_put", [P, C.POINTER(Id), Bytes, C.c_uint32, C.c_uint32, C.POINTER(C.c_float), C.c_uint32], C.c_int),
    ("vk_outline_key", [C.POINTER(Id), Bytes, C.POINTER(Id)], None),
    ("vk_search_coverage", [C.POINTER(Search), C.POINTER(Coverage)], C.c_int),
    ("vk_search_close", [C.POINTER(Search)], None),
    ("sqlite3_open", [C.c_char_p, C.POINTER(P)], C.c_int),
    ("sqlite3_close", [P], C.c_int),
    ("sqlite3_exec", [P, C.c_char_p, P, P, P], C.c_int),
    ("sqlite3_key", [P, P, C.c_int], C.c_int),
]:
    fn = getattr(LIB, name)
    fn.argtypes, fn.restype = args, res
GREP = C.addressof(C.c_void_p.in_dll(LIB, "vkGrep"))
BM25 = C.addressof(C.c_void_p.in_dll(LIB, "vkBm25"))
REGEX = C.addressof(C.c_void_p.in_dll(LIB, "vkRegex"))
VECTOR = C.addressof(C.c_void_p.in_dll(LIB, "vkVector"))


def outline_key(target, author):
    """OL-1: the akey of author's outlines of target."""
    out = Id()
    LIB.vk_outline_key(C.byref(Id(*target)), _b(author), C.byref(out))
    return bytes(out)


class VkError(Exception):
    pass


def _b(v):
    """Borrows v's buffer: the caller keeps v alive while the store reads it."""
    return Bytes(C.cast(C.c_char_p(v), C.c_void_p), len(v))


class Record:
    """A row read back by id (Q-8): plain Python values, safe to keep."""

    def __init__(self, c):
        a = c.a

        def get(f):
            return C.string_at(f.p, f.n) if f.n else b""
        self.id = bytes(c.id)
        self.author, self.kind, self.akey = get(a.author), get(a.kind), get(a.akey)
        self.ts, self.reference, self.body = get(a.ts), get(a.reference), get(a.body)
        self.erased = bool(a.erased)
        self.log, self.pos = get(a.log), a.pos   # LOG-1: empty and 0 when not in a log
        self.supersedes = [bytes(a.supersedes[i]) for i in range(a.nSupersedes)]
        self.derived_from = [bytes(a.derivedFrom[i]) for i in range(a.nDerivedFrom)]


class Store:
    """One diary, opened by the host.

    key is a bytearray holding SQLCipher's raw-key form, x'<64 hex>' (N-7: the
    database key is full-entropy and used with no KDF), or None for an unkeyed
    diary. It is zeroed here once applied, so the caller's copy does not outlive
    the open."""

    def __init__(self, path, principal, rights, key=None, others=()):
        """others: further (principal, rights) the host entitles, so their rows count (R-4a)."""
        self.db, self.st = P(), P()
        # Kept alive here because the store borrows them for its whole life.
        self._principal = principal
        self._names = [principal] + [o for o, _ in others]
        grants = [(principal, rights)] + list(others)
        self._grant = (Grant * len(grants))(*[Grant(_b(n), r) for n, r in grants])
        if LIB.sqlite3_open(path.encode(), C.byref(self.db)) != 0:
            raise VkError(f"cannot open {path}")
        if key is not None:
            # Before any other statement: SQLCipher reads the header on the first one.
            buf = (C.c_char * len(key)).from_buffer(key)
            rc = LIB.sqlite3_key(self.db, C.addressof(buf), len(key))
            del buf
            key[:] = bytes(len(key))
            if rc != 0:
                self._fail(f"cannot key {path}")
        # The first real read. A wrong key fails here, not at sqlite3_key, which
        # only sets the cipher context.
        if LIB.sqlite3_exec(self.db, b"SELECT count(*) FROM sqlite_schema", None, None, None) != 0:
            self._fail(f"{path} is not readable: wrong key, or not a diary")
        if LIB.vk_open(self.db, _b(principal), self._grant, len(grants), C.byref(self.st)) != OK:
            self._fail(f"vk_open: {self.errmsg()}")

    def _fail(self, why):
        LIB.sqlite3_close(self.db)
        self.db, self.st = P(), P()
        raise VkError(why)

    def errmsg(self):
        m = LIB.vk_errmsg(self.st) if self.st else None
        return m.decode() if m else "?"

    def close(self):
        if self.st:
            LIB.vk_close(self.st)
        if self.db:
            LIB.sqlite3_close(self.db)

    def exec(self, sql):
        if LIB.sqlite3_exec(self.db, sql.encode(), None, None, None) != 0:
            raise VkError(f"sql failed: {sql}")

    def append(self, log, kind, akey, body, ts=b"", reference=b"", rank=0):
        """LOG-1: the next position of this principal's log. Returns the id."""
        a = Assertion()
        a.author, a.kind, a.akey = _b(self._principal), _b(kind), _b(akey)
        a.ts, a.reference, a.body, a.log = _b(ts), _b(reference), _b(body), _b(log)
        a.rank[:] = list(rank.to_bytes(16, "big"))
        out = Id()
        if LIB.vk_append(self.st, C.byref(a), C.byref(out)) not in (OK, EXISTS):
            raise VkError(f"append {akey!r}: {self.errmsg()}")
        return bytes(out)

    def log_span(self, log, author=None):
        """(lowest, highest) position held in author's log, or None if it holds none."""
        lo, hi = C.c_uint64(), C.c_uint64()
        rc = LIB.vk_log_span(self.st, _b(log), _b(author or self._principal), C.byref(lo), C.byref(hi))
        if rc == NOTFOUND:
            return None
        if rc != OK:
            raise VkError(f"log span: {self.errmsg()}")
        return lo.value, hi.value

    def forget(self, log, frm, to, ts=b"", rank=0):
        """W-15: one range tombstone over positions frm..to of this principal's log."""
        r = (C.c_ubyte * 16)(*rank.to_bytes(16, "big"))
        out = Id()
        if LIB.vk_forget(self.st, _b(log), _b(self._principal), frm, to, _b(ts), C.byref(r), C.byref(out)) != OK:
            raise VkError(f"forget: {self.errmsg()}")
        return bytes(out)

    def put(self, kind, akey, body, ts=b"", reference=b"", rank=0, supersedes=(), derived_from=(), chain=False):
        """Returns (id, added)."""
        sup = (Id * max(1, len(supersedes)))(*[Id(*s) for s in supersedes])
        der = (Id * max(1, len(derived_from)))(*[Id(*d) for d in derived_from])
        a = Assertion()
        a.author, a.kind, a.akey = _b(self._principal), _b(kind), _b(akey)
        a.ts, a.reference, a.body = _b(ts), _b(reference), _b(body)
        a.rank[:] = list(rank.to_bytes(16, "big"))
        a.supersedes, a.nSupersedes = sup, len(supersedes)
        a.derivedFrom, a.nDerivedFrom = der, len(derived_from)
        out = Id()
        rc = (LIB.vk_put_chain if chain else LIB.vk_put)(self.st, C.byref(a), C.byref(out))
        if rc not in (OK, EXISTS):
            raise VkError(f"put {akey!r}: {self.errmsg()}")
        return bytes(out), rc == OK

    def current(self, akey):
        """(status, body): body is None unless the status is OK."""
        r = Row()
        rc = LIB.vk_current(self.st, _b(akey), C.byref(r))
        body = C.string_at(r.body.p, r.body.n) if rc == OK and r.body.n else (b"" if rc == OK else None)
        if rc == OK:
            LIB.vk_row_close(C.byref(r))
        return rc, body

    def walk(self):
        """Every row in arrival order, as Records."""
        c = Cursor()
        if LIB.vk_cursor_open(self.st, 0, C.byref(c)) != OK:
            raise VkError(f"cursor: {self.errmsg()}")
        try:
            while (rc := LIB.vk_cursor_next(C.byref(c))) == OK:
                yield Record(c)
            if rc != DONE:
                raise VkError(f"cursor: {self.errmsg()}")
        finally:
            LIB.vk_cursor_close(C.byref(c))

    def head_id(self, akey):
        """The id of akey's current row, or None."""
        r = Row()
        if LIB.vk_current(self.st, _b(akey), C.byref(r)) != OK:
            return None
        out = bytes(r.id)
        LIB.vk_row_close(C.byref(r))
        return out

    def get(self, id_):
        c = Cursor()
        rc = LIB.vk_get(self.st, C.byref(Id(*id_)), C.byref(c))
        try:
            if rc == NOTFOUND:
                return None
            if rc != OK:
                raise VkError(f"get: {self.errmsg()}")
            return Record(c)
        finally:
            LIB.vk_cursor_close(C.byref(c))

    def standing(self, akey, id_):
        """Q-4: (is_head, heads on the akey); heads 0 if the akey has none."""
        head, n = C.c_int(), C.c_int()
        rc = LIB.vk_standing(self.st, _b(akey), C.byref(Id(*id_)), C.byref(head), C.byref(n))
        if rc == NOTFOUND:
            return False, 0
        if rc != OK:
            raise VkError(f"standing: {self.errmsg()}")
        return bool(head.value), n.value

    def bm25_enable(self):
        if LIB.vk_bm25_enable(self.st) != OK:
            raise VkError(f"bm25: {self.errmsg()}")

    def use_outlines(self, author):
        """OL-4: whose outlines searches use; b"" for none. Kept alive here: the store borrows it."""
        self._outlines = author
        LIB.vk_use_outlines(self.st, _b(author))

    def use_vectors(self, model):
        """Q-6: which model the vector method uses; b"" for none. Kept alive here: the store borrows it."""
        self._vectors = model
        LIB.vk_use_vectors(self.st, _b(model))

    def vector_put(self, id_, model, lo, hi, vec):
        """One vector (a sequence of floats) of the span lo..hi of id_'s body."""
        buf = (C.c_float * len(vec))(*vec)
        rc = LIB.vk_vector_put(self.st, C.byref(Id(*id_)), _b(model), lo, hi, buf, len(vec))
        if rc != OK:
            raise VkError(f"vector put: {self.errmsg()}")

    def vector_drop(self, id_, model):
        """Every vector of id_'s body under model goes (before embedding it afresh)."""
        if LIB.vk_vector_drop(self.st, C.byref(Id(*id_)), _b(model)) != OK:
            raise VkError(f"vector drop: {self.errmsg()}")

    def vector(self, vec):
        """(hits, coverage), nearest first; (None, None) if blind. A hit's node is (None, lo, hi, None)."""
        import array
        return self.search(VECTOR, array.array("f", vec).tobytes())

    def grep(self, text):
        """(hits, coverage) for an exact substring of any readable body."""
        return self.search(GREP, text)

    def regex(self, pattern):
        """(hits, coverage) for a regular expression, in SQLite's regexp.c dialect (Q-6)."""
        return self.search(REGEX, pattern)

    def bm25(self, text):
        """(hits, coverage), best first; (None, None) if blind (not indexed)."""
        return self.search(BM25, text)

    def search(self, method, text):
        """(hits, coverage): each hit (id, node), node None for a whole assertion, else
        (titles from the root down, lo, hi, depth)."""
        s, cov, out, ids, hit = Search(), Coverage(), Id(), [], Hit()
        rc = LIB.vk_search_open(self.st, method, _b(text), C.byref(s))
        try:
            if rc == BLIND:
                return None, None
            if rc != OK:
                raise VkError(f"search: {self.errmsg()}")
            while (rc := LIB.vk_search_next(C.byref(s), C.byref(out))) == OK:
                if LIB.vk_search_hit(C.byref(s), C.byref(hit)) != OK:
                    raise VkError(f"search: {self.errmsg()}")
                node = None
                if hit.bRange:
                    node = (None, hit.lo, hit.hi, None)
                elif hit.bNode:
                    path = json.loads(C.string_at(hit.path.p, hit.path.n).decode("utf-8", "replace"))
                    node = (path, hit.lo, hit.hi, hit.depth)
                ids.append((bytes(out), node))
            if rc != DONE:
                raise VkError(f"search: {self.errmsg()}")   # a cut-short list must not read as complete
            LIB.vk_search_coverage(C.byref(s), C.byref(cov))
            return ids, {"method": cov.zMethod.decode(), "exhaustive": bool(cov.bExhaustive),
                         "searched": cov.nSearched, "whole": cov.nWhole}
        finally:
            LIB.vk_search_close(C.byref(s))
