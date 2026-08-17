#!/bin/sh
#
# test/tokenizer-conformance.sh -- does src/tokenizer.c produce the token ids
# all-MiniLM-L6-v2 was trained on?
#
# This is not a "does it crash" test. It builds src/tokenizer.c TWICE -- once
# as shipped and once with -DVIKI_TOKENIZER_CONFORMANT -- and diffs both
# against a reimplementation of HuggingFace's slow BertTokenizer, which is the
# tokenizer the pinned model was trained through. Three assertions, and the
# middle one is the one people find surprising:
#
#   1. SHIPPED build == reference on every pure-ASCII chunk of the fixture.
#      This is what "turning the flag on cannot invalidate a cached vector"
#      reduces to, because this repo's indexed corpus is ~100% ASCII.
#   2. SHIPPED build DIVERGES from reference on the documented non-ASCII
#      cases. That looks backwards until you remember what a conformance fix
#      costs here: embeddings are a shared function of (content_hash,
#      model_id, chunk_params) across all peers (D-11), so quietly making the
#      DEFAULT path conformant re-tokenizes every peer's content without a
#      viki-manifest epoch bump. This assertion fails loudly if somebody does
#      that by accident.
#   3. CONFORMANT build == reference on every case and every fixture chunk.
#
# It never runs build/build.sh, never writes into the repo, and needs only
# cc, python3 and the model's vocab.txt.
#
# Usage:
#     sh test/tokenizer-conformance.sh                 # run the three checks
#     sh test/tokenizer-conformance.sh --gen           # re-emit the Unicode
#                                                      # tables in tokenizer.c
#                                                      # to stdout, for diffing
#
# Env:
#     VIKI_MODEL_DIR   where vocab.txt lives (default build/dist/model)
#
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
MODEL_DIR=${VIKI_MODEL_DIR:-$ROOT/build/dist/model}
VOCAB="$MODEL_DIR/vocab.txt"

PY=${VIKI_EVAL_PYTHON:-}
if [ -z "$PY" ]; then
    for c in python3 python; do
        if command -v "$c" >/dev/null 2>&1; then PY="$c"; break; fi
    done
fi
[ -n "$PY" ] || { echo "tokenizer-conformance.sh: no python3 on PATH" >&2; exit 1; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/viki-tokconf.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# ---------------------------------------------------------------------------
# refbert.py -- HuggingFace's slow BertTokenizer, reimplemented. Every step
# mirrors transformers/models/bert/tokenization_bert.py; it is a
# reimplementation rather than an import because neither `transformers` nor
# `tokenizers` is installed here and there is no network (FINDINGS.md).
# ---------------------------------------------------------------------------
cat > "$WORK/refbert.py" <<'PYEOF'
import unicodedata


def _is_whitespace(ch):
    if ch in (" ", "\t", "\n", "\r"):
        return True
    return unicodedata.category(ch) == "Zs"


def _is_control(ch):
    if ch in ("\t", "\n", "\r"):
        return False
    return unicodedata.category(ch).startswith("C")


def _is_punctuation(ch):
    cp = ord(ch)
    if (33 <= cp <= 47) or (58 <= cp <= 64) or (91 <= cp <= 96) or (123 <= cp <= 126):
        return True
    return unicodedata.category(ch).startswith("P")


def _is_chinese_char(cp):
    return ((0x4E00 <= cp <= 0x9FFF) or (0x3400 <= cp <= 0x4DBF)
            or (0x20000 <= cp <= 0x2A6DF) or (0x2A700 <= cp <= 0x2B73F)
            or (0x2B740 <= cp <= 0x2B81F) or (0x2B820 <= cp <= 0x2CEAF)
            or (0xF900 <= cp <= 0xFAFF) or (0x2F800 <= cp <= 0x2FA1F))


def _clean_text(text):
    out = []
    for ch in text:
        cp = ord(ch)
        if cp == 0 or cp == 0xFFFD or _is_control(ch):
            continue
        out.append(" " if _is_whitespace(ch) else ch)
    return "".join(out)


def _tokenize_chinese_chars(text):
    out = []
    for ch in text:
        if _is_chinese_char(ord(ch)):
            out.append(" " + ch + " ")
        else:
            out.append(ch)
    return "".join(out)


def _run_strip_accents(text):
    text = unicodedata.normalize("NFD", text)
    return "".join(c for c in text if unicodedata.category(c) != "Mn")


def _run_split_on_punc(text):
    out, cur = [], []
    for ch in text:
        if _is_punctuation(ch):
            if cur:
                out.append("".join(cur))
                cur = []
            out.append(ch)
        else:
            cur.append(ch)
    if cur:
        out.append("".join(cur))
    return [t for t in out if t]


def basic_tokenize(text):
    text = _tokenize_chinese_chars(_clean_text(text))
    toks = []
    for tok in text.split():
        for t in _run_split_on_punc(_run_strip_accents(tok.lower())):
            toks.append(t)
    return toks


class Ref:
    def __init__(self, vocab_path):
        self.ids, self.toks = {}, []
        with open(vocab_path, encoding="utf-8") as f:
            for i, line in enumerate(f):
                t = line.rstrip("\n")
                self.toks.append(t)
                if t not in self.ids:
                    self.ids[t] = i
        self.unk, self.cls, self.sep = self.ids["[UNK]"], self.ids["[CLS]"], self.ids["[SEP]"]

    def wordpiece(self, word):
        if len(word) > 100:
            return [self.unk]
        out, start = [], 0
        while start < len(word):
            end, cur = len(word), None
            while start < end:
                sub = word[start:end]
                if start > 0:
                    sub = "##" + sub
                if sub in self.ids:
                    cur = self.ids[sub]
                    break
                end -= 1
            if cur is None:
                return [self.unk]
            out.append(cur)
            start = end
        return out

    def encode(self, text, max_len=None):
        ids = [self.cls]
        for w in basic_tokenize(text):
            ids.extend(self.wordpiece(w))
        ids.append(self.sep)
        if max_len is not None and len(ids) > max_len:
            ids = ids[:max_len - 1] + [self.sep]
        return ids

    def render(self, ids):
        return " ".join(self.toks[i] for i in ids)
PYEOF

# ---------------------------------------------------------------------------
# probe.c -- links the REAL src/tokenizer.c, so what is measured is what viki
# ships. Reads base64 records (one per line) so a record may contain newlines.
# ---------------------------------------------------------------------------
cat > "$WORK/probe.c" <<'CEOF'
#include "tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int b64val(int c){
    if( c >= 'A' && c <= 'Z' ) return c - 'A';
    if( c >= 'a' && c <= 'z' ) return c - 'a' + 26;
    if( c >= '0' && c <= '9' ) return c - '0' + 52;
    if( c == '+' ) return 62;
    if( c == '/' ) return 63;
    return -1;
}

int main(int argc, char **argv){
    viki_vocab *v;
    int maxLen, *ids;
    static char line[1 << 21];

    if( argc < 3 ){ fprintf(stderr, "usage: probe <vocab> <maxLen>\n"); return 2; }
    v = viki_vocab_load(argv[1]);
    if( !v ) return 1;
    maxLen = atoi(argv[2]);
    ids = malloc(sizeof(int) * (size_t)maxLen);

    while( fgets(line, sizeof(line), stdin) ){
        size_t i, len = strlen(line), n = 0;
        char *txt = malloc(len + 4);
        int acc = 0, bits = 0, nTok, k;
        for( i = 0; i < len; i++ ){
            int val = b64val((unsigned char)line[i]);
            if( val < 0 ) continue;
            acc = (acc << 6) | val;
            bits += 6;
            if( bits >= 8 ){ bits -= 8; txt[n++] = (char)((acc >> bits) & 0xFF); }
        }
        txt[n] = '\0';
        nTok = viki_tokenize(v, txt, ids, maxLen);
        for( k = 0; k < nTok; k++ ) printf("%s%d", k ? "," : "", ids[k]);
        printf("\n");
        free(txt);
    }
    viki_vocab_free(v);
    return 0;
}
CEOF

# ---------------------------------------------------------------------------
# gentab.py -- regenerates the Unicode tables that live inside the
# VIKI_TOKENIZER_CONFORMANT block of src/tokenizer.c. Everything it emits is
# derived from Python's own unicodedata, which is the same module
# BertTokenizer calls -- so the tables are a transcription of the reference,
# not a judgement call. `--selfcheck` proves the C pipeline's algebra
# (lower -> NFD-base -> lower, with Mn dropped) reproduces the reference over
# every codepoint.
# ---------------------------------------------------------------------------
cat > "$WORK/gentab.py" <<'PYEOF'
import sys
import unicodedata as ud

MAXCP = 0x110000
FOLD_BLOCKS = [("Latin", 0x00C0, 0x024F), ("GrCy", 0x0370, 0x04FF),
               ("LatAd", 0x1E00, 0x1FFF), ("Kana", 0x3040, 0x30FF)]
HANGUL_LO, HANGUL_HI = 0xAC00, 0xD7A3
CJKCOMPAT = [(0xF900, 0xFAFF), (0x2F800, 0x2FA1F)]


def cat(cp):
    return ud.category(chr(cp))


def strip_marks(s):
    s = ud.normalize("NFD", s)
    return "".join(c for c in s if ud.category(c) != "Mn")


def ranges(pred):
    out, s = [], None
    for cp in range(MAXCP):
        if pred(cp):
            if s is None:
                s = cp
        elif s is not None:
            out.append((s, cp - 1))
            s = None
    if s is not None:
        out.append((s, MAXCP - 1))
    return out


def in_dense(cp):
    return any(lo <= cp <= hi for _, lo, hi in FOLD_BLOCKS)


def in_hangul(cp):
    return HANGUL_LO <= cp <= HANGUL_HI


def in_cjkcompat(cp):
    return any(lo <= cp <= hi for lo, hi in CJKCOMPAT)


def lower_runs():
    items = [(cp, ord(chr(cp).lower()) - cp) for cp in range(0x80, MAXCP)
             if len(chr(cp).lower()) == 1 and chr(cp).lower() != chr(cp)]
    runs, i = [], 0
    while i < len(items):
        cp, d = items[i]
        best = None
        for step in (1, 2):
            j, last, cnt = i, cp, 1
            while j + 1 < len(items) and items[j + 1][0] == last + step and items[j + 1][1] == d:
                j += 1
                last = items[j][0]
                cnt += 1
            if best is None or cnt > best[0]:
                best = (cnt, step, last, j)
        cnt, step, last, j = best
        runs.append((cp, last, step, d))
        i = j + 1
    return runs


def other_fold():
    out = []
    for cp in range(0x80, MAXCP):
        if in_dense(cp) or in_hangul(cp) or in_cjkcompat(cp) or cat(cp) == "Mn":
            continue
        s = strip_marks(chr(cp))
        if s != chr(cp):
            out.append((cp, [ord(c) for c in s][:3]))
    return out


def emit_ranges(name, rs, comment):
    print("/* %s */" % comment)
    print("static const unsigned int a%s[][2] = {" % name)
    for i in range(0, len(rs), 4):
        print("    " + "".join("{0x%04X,0x%04X}," % (a, b) for a, b in rs[i:i + 4]))
    print("};")
    print("#define n%s (int)(sizeof(a%s)/sizeof(a%s[0]))" % (name, name, name))
    print()


def emit():
    print("/* ---- GENERATED TABLES (UCD %s); regenerate with" % ud.unidata_version)
    print("** `sh test/tokenizer-conformance.sh --gen` and diff. Do not hand-edit. ---- */")
    print()
    emit_ranges("PunctRange", ranges(lambda cp: cp >= 0x80 and cat(cp).startswith("P")),
                "category P*, the non-ASCII half of reference _is_punctuation()")
    emit_ranges("SpaceRange",
                ranges(lambda cp: cp >= 0x80 and cat(cp) in ("Zs", "Zl", "Zp")),
                "reference token boundaries: category Zs (its _is_whitespace) PLUS\n"
                "** Zl/Zp. Zl/Zp look wrong and are not: _is_whitespace() only tests Zs,\n"
                "** but whitespace_tokenize() then calls Python's str.split(), which\n"
                "** splits on U+2028 and U+2029 as well. Every other codepoint\n"
                "** str.split() breaks on is Cc, and _clean_text has already deleted\n"
                "** those. Found by sweeping all 1,112,032 codepoints through the C\n"
                "** code -- reasoning about the reference had missed it")
    emit_ranges("CtlRange", ranges(lambda cp: cp >= 0x80 and cat(cp) in ("Cc", "Cf", "Cs", "Co")),
                "categories Cc/Cf/Cs/Co: reference _is_control() minus Cn (see tokenizer.h)")
    emit_ranges("MarkRange", ranges(lambda cp: cat(cp) == "Mn"),
                "category Mn: reference _run_strip_accents() drops these after NFD")
    runs = lower_runs()
    print("/* Simple (1:1) lowercase for cp >= 0x80 as (lo,hi,step,delta) runs.")
    print("** Complete over Unicode. U+0130 is the one codepoint whose lowercase is")
    print("** two codepoints; its NFD base is ASCII 'I', so the fold table plus the")
    print("** trailing ASCII tolower() reach the reference answer without it. */")
    print("static const int aLowerRun[][4] = {")
    for i in range(0, len(runs), 3):
        print("    " + "".join("{0x%04X,0x%04X,%d,%d}," % r for r in runs[i:i + 3]))
    print("};")
    print("#define nLowerRun (int)(sizeof(aLowerRun)/sizeof(aLowerRun[0]))")
    print()
    for name, lo, hi in FOLD_BLOCKS:
        vals = []
        for cp in range(lo, hi + 1):
            s = strip_marks(chr(cp))
            vals.append(0 if (s == chr(cp) or len(s) != 1) else ord(s))
        print("/* NFD base for U+%04X..U+%04X indexed by (cp - 0x%04X); 0 == unchanged */"
              % (lo, hi, lo))
        print("#define FOLD_%s_LO 0x%04X" % (name.upper(), lo))
        print("#define FOLD_%s_HI 0x%04X" % (name.upper(), hi))
        print("static const unsigned short aFold%s[] = {" % name)
        for i in range(0, len(vals), 16):
            print("    " + "".join("0x%04X," % v for v in vals[i:i + 16]))
        print("};")
        print()
    print("/* Everything else with a canonical decomposition, sorted by codepoint:")
    print("** {cp, up to three replacement codepoints, 0-padded}. Covers the")
    print("** reference's own quirks -- U+2260 NOT EQUAL TO folds to '=' -- which")
    print("** conformance means reproducing, not improving on. */")
    print("static const unsigned int aFoldOther[][4] = {")
    for cp, s in other_fold():
        s = (s + [0, 0, 0])[:3]
        print("    {0x%05X,0x%04X,0x%04X,0x%04X}," % (cp, s[0], s[1], s[2]))
    print("};")
    print("#define nFoldOther (int)(sizeof(aFoldOther)/sizeof(aFoldOther[0]))")
    print()
    print("/* ---- END GENERATED TABLES ---- */")


def selfcheck():
    lowmap = {cp: ord(chr(cp).lower()) for cp in range(0x80, MAXCP)
              if len(chr(cp).lower()) == 1 and chr(cp).lower() != chr(cp)}
    dense = {}
    for _, lo, hi in FOLD_BLOCKS:
        for cp in range(lo, hi + 1):
            s = strip_marks(chr(cp))
            if s != chr(cp) and len(s) == 1:
                dense[cp] = s
    sparse = {cp: "".join(chr(c) for c in s) for cp, s in other_fold()}

    def L(cp):
        if cp < 0x80:
            return cp + 32 if 0x41 <= cp <= 0x5A else cp
        return lowmap.get(cp, cp)

    def F(cp):
        if in_hangul(cp):
            i = cp - HANGUL_LO
            t = i % 28
            return (chr(0x1100 + i // 588) + chr(0x1161 + (i % 588) // 28)
                    + (chr(0x11A7 + t) if t else ""))
        return dense.get(cp, sparse.get(cp, chr(cp)))

    bad = [cp for cp in range(MAXCP) if cat(cp) != "Mn"
           and strip_marks(chr(cp).lower()) != "".join(chr(L(ord(c))) for c in F(L(cp)))]
    undoc = [cp for cp in bad if not in_cjkcompat(cp)]
    print("  pipeline vs reference over %d codepoints: %d mismatches, %d of them "
          "outside the documented CJK-Compatibility-Ideograph gap" % (MAXCP, len(bad), len(undoc)))
    return 1 if undoc else 0


if __name__ == "__main__":
    sys.exit(selfcheck() if "--selfcheck" in sys.argv else emit())
PYEOF

if [ "${1:-}" = "--gen" ]; then
    exec "$PY" "$WORK/gentab.py"
fi

# ---------------------------------------------------------------------------
# the driver
# ---------------------------------------------------------------------------
cat > "$WORK/run.py" <<'PYEOF'
import base64
import os
import subprocess
import sys

sys.path.insert(0, os.environ["WORK"])
import refbert

WORK = os.environ["WORK"]
ROOT = os.environ["ROOT"]
VOCAB = os.environ["VOCAB"]
CHUNK_LINES = 40          # VIKI_CHUNK_LINES, what viki_index.c feeds us
MAXLEN = 256              # VIKI_MAX_SEQ_LEN, what embed.c asks for

# The 27 characters whose handling the header describes, plus the alphabet and
# ASCII-rule cases. Every row is a claim in tokenizer.h that must stay true.
PUNCT = "—–‑…“”‘’«»·" \
        "§¶•′″‚„≠ "
NONDIVERGENT = "°×→⇒≥−∪"

CASES = [("punct U+%04X" % ord(c), "decision%srecorded" % c) for c in PUNCT]
CASES += [("nondiv U+%04X" % ord(c), "decision%srecorded" % c) for c in NONDIVERGENT]
CASES += [
    ("accent fr",            "décision enregistrée au café"),
    ("accent UPPER",         "RÉSUMÉ NAÏVE"),
    ("accent decomposed",    "décision"),
    ("accent es/de/pt",      "mañana straße informação"),
    ("vietnamese",           "Tiếng Việt"),
    ("turkish dotted I",     "İSTANBUL"),
    ("greek",                "Αρχείο μνήμης"),
    ("cyrillic",             "ЖУРНАЛ памяти"),
    ("cjk short",            "中文"),
    ("cjk sentence",         "我们决定不按模型过滤"),
    ("japanese",             "日本語のガイド"),
    ("korean",               "한국어 기억"),
    ("emoji",                "shipped it \U0001f389 bug \U0001f41b"),
    ("zwsp/bom",             "deci​sion﻿ recorded"),
    ("ASCII 64-hex word",    "a" * 64),
    ("ASCII 100-char word",  "a" * 100),
    ("ASCII 101-char word",  "a" * 101),
    ("ASCII 300-char word",  "abcdefghij" * 30),
    ("ASCII ctl 0x01",       "alpha\x01beta"),
    ("ASCII ctl 0x7f",       "alpha\x7fbeta"),
    ("ASCII ctl 0x0b",       "alpha\x0bbeta"),
    ("ASCII identifiers",    "find_w_card gc_orphan_chunks VIKI_FTS_EPOCH_SLACK"),
    ("ASCII prose",          "The decision was NOT to filter FTS by model_id."),
]

# The rows that MUST still diverge in the shipped build. If one of these starts
# matching, the default path has quietly become conformant and every peer's
# cached vector for that content is now wrong without an epoch bump.
MUST_DIVERGE_OFF = set(["punct U+%04X" % ord(c) for c in PUNCT] +
                       ["accent fr", "accent UPPER", "accent decomposed",
                        "accent es/de/pt", "vietnamese", "turkish dotted I",
                        "greek", "cyrillic", "cjk short", "cjk sentence",
                        "japanese", "korean", "zwsp/bom",
                        "ASCII 101-char word", "ASCII 300-char word",
                        "ASCII ctl 0x01", "ASCII ctl 0x7f", "ASCII ctl 0x0b"])


def probe(binary, records):
    inp = b"".join(base64.b64encode(r) + b"\n" for r in records)
    p = subprocess.run([binary, VOCAB, str(MAXLEN)], input=inp,
                       stdout=subprocess.PIPE, check=True)
    return [[int(x) for x in ln.split(",")] for ln in p.stdout.decode().splitlines()]


def fixture_records():
    out, names = [], []
    for root, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in
                   ("vendor", "build", ".git", ".viki", "__pycache__", "experiments")]
        for f in sorted(files):
            if not f.endswith((".md", ".c", ".h", ".sh", ".py")):
                continue
            path = os.path.join(root, f)
            try:
                data = open(path, "rb").read()
            except OSError:
                continue
            lines = data.split(b"\n")
            for i in range(0, len(lines), CHUNK_LINES):
                blob = b"\n".join(lines[i:i + CHUNK_LINES])
                if blob.strip():
                    out.append(blob)
                    names.append("%s#%d" % (os.path.relpath(path, ROOT), i // CHUNK_LINES))
    return out, names


def main():
    ref = refbert.Ref(VOCAB)
    fails = []

    texts = [t.encode() for _, t in CASES]
    off = probe(os.path.join(WORK, "probe_off"), texts)
    on = probe(os.path.join(WORK, "probe_on"), texts)
    rf = [ref.encode(t, max_len=MAXLEN) for _, t in CASES]

    print("--- divergence table (%d cases) ---" % len(CASES))
    print("%-22s %-6s %-11s %-11s" % ("case", "ascii", "SHIPPED", "CONFORMANT"))
    nOff = nOn = 0
    for i, (name, t) in enumerate(CASES):
        isasc = all(ord(c) < 128 for c in t)
        o, c = off[i] == rf[i], on[i] == rf[i]
        nOff += o
        nOn += c
        print("%-22s %-6s %-11s %-11s" % (name, "yes" if isasc else "no",
                                          "=ref" if o else "diverges",
                                          "=ref" if c else "DIVERGES"))
        if not c:
            fails.append("case %s: conformant build does not match reference" % name)
        if name in MUST_DIVERGE_OFF and o:
            fails.append("case %s: SHIPPED build matches reference but must not -- "
                         "the default path has become conformant without an epoch bump" % name)
    print("shipped matches reference on %d/%d cases; conformant on %d/%d"
          % (nOff, len(CASES), nOn, len(CASES)))

    print()
    print("--- fixture: every 40-line chunk of this repo's .md/.c/.h/.sh/.py ---")
    recs, names = fixture_records()
    off = probe(os.path.join(WORK, "probe_off"), recs)
    on = probe(os.path.join(WORK, "probe_on"), recs)
    rf = [ref.encode(r.decode("utf-8", "replace"), max_len=MAXLEN) for r in recs]
    asc = [i for i, r in enumerate(recs) if all(b < 0x80 for b in r)]
    non = [i for i in range(len(recs)) if i not in set(asc)]

    dAsc = [i for i in asc if off[i] != on[i]]
    print("  chunks: %d (%d pure ASCII, %d not)" % (len(recs), len(asc), len(non)))
    print("  ASCII chunks where CONFORMANT differs from SHIPPED : %d" % len(dAsc))
    print("  ASCII chunks where SHIPPED    differs from reference: %d"
          % len([i for i in asc if off[i] != rf[i]]))
    print("  ASCII chunks where CONFORMANT differs from reference: %d"
          % len([i for i in asc if on[i] != rf[i]]))
    print("  non-ASCII chunks where SHIPPED    differs from reference: %d" % len([i for i in non if off[i] != rf[i]]))
    print("  non-ASCII chunks where CONFORMANT differs from reference: %d" % len([i for i in non if on[i] != rf[i]]))
    if dAsc:
        fails.append("%d pure-ASCII chunks tokenize differently with the flag on "
                     "(first: %s) -- ASCII must be untouched" % (len(dAsc), names[dAsc[0]]))
    for i in asc:
        if off[i] != rf[i]:
            fails.append("ASCII chunk %s: shipped build diverges from reference" % names[i])
            break
    for i in range(len(recs)):
        if on[i] != rf[i]:
            fails.append("chunk %s: conformant build diverges from reference" % names[i])
            break

    print()
    if fails:
        for f in fails:
            print("FAIL: %s" % f)
        print("tokenizer-conformance: %d failure(s)" % len(fails))
        return 1
    print("tokenizer-conformance: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
PYEOF

[ -f "$VOCAB" ] || { echo "tokenizer-conformance.sh: no vocab at '$VOCAB' (set VIKI_MODEL_DIR)" >&2; exit 1; }

cc -O2 -Wall -I"$ROOT/src" -o "$WORK/probe_off" "$WORK/probe.c" "$ROOT/src/tokenizer.c"
cc -O2 -Wall -DVIKI_TOKENIZER_CONFORMANT -I"$ROOT/src" -o "$WORK/probe_on" \
   "$WORK/probe.c" "$ROOT/src/tokenizer.c"

echo "--- table self-check (the C pipeline's algebra vs the UCD) ---"
"$PY" "$WORK/gentab.py" --selfcheck

WORK="$WORK" ROOT="$ROOT" VOCAB="$VOCAB" exec "$PY" "$WORK/run.py"
