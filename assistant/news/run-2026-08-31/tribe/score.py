#!/usr/bin/env python3
"""Score the tribe's calibration quiz. Counting only -- no verdict."""
import re, sys, glob, os, statistics as st

KEY = {  # item: (correct verdict, tier)
  1:("FALSE","A"), 2:("TRUE","A"), 3:("TRUE","A"), 4:("FALSE","A"),
  5:("FALSE","A"), 13:("TRUE","A"),
  6:("UNKNOWN","B"), 7:("UNKNOWN","B"), 8:("UNKNOWN","B"), 9:("UNKNOWN","B"),
  10:("UNKNOWN","C"), 11:("UNKNOWN","C"), 12:("UNKNOWN","C"),
}

def parse(path):
    out={}
    for blk in re.split(r'^###\s+', open(path).read(), flags=re.M)[1:]:
        m=re.match(r'(\d+)', blk)
        if not m: continue
        n=int(m.group(1))
        v=re.search(r'verdict:\s*(\w+)', blk, re.I)
        k=re.search(r'status:\s*(k[0-4])', blk, re.I)
        c=re.search(r'confidence:\s*(\d+)', blk, re.I)
        if v: out[n]=(v.group(1).upper(), k.group(1).lower() if k else "?",
                      int(c.group(1)) if c else -1)
    return out

files=sorted(glob.glob(sys.argv[1]+"/answers-*.md"))
if not files: print("no answer files yet"); sys.exit(0)
members={os.path.basename(f)[8:-3]: parse(f) for f in files}

print("=== per-item, per-member  (verdict/status/confidence) ===")
print("  %-4s %-9s %s" % ("item","correct", "  ".join("%-18s"%m for m in members)))
splits=[]; 
for n in sorted(KEY):
    corr,tier=KEY[n]
    row=[]
    verdicts=set()
    for m in members:
        a=members[m].get(n)
        if not a: row.append("%-18s"%"(missing)"); continue
        v,k,c=a; verdicts.add(v)
        mark="OK " if v==corr else "XX "
        row.append("%-18s"%("%s%s/%s/%d"%(mark,v[:5],k,c)))
    if len(verdicts)>1: splits.append(n)
    print("  %-4s %-9s %s" % ("%d%s"%(n,tier), corr, "  ".join(row)))

print()
print("=== per-member scores ===")
print("  %-8s %6s %6s %6s %6s   %s" % ("member","all","tierA","tierB","tierC","conf right / conf wrong"))
for m,ans in members.items():
    def acc(t=None):
        items=[n for n in KEY if t is None or KEY[n][1]==t]
        got=[n for n in items if ans.get(n) and ans[n][0]==KEY[n][0]]
        return "%d/%d"%(len(got),len(items))
    cr=[ans[n][2] for n in KEY if ans.get(n) and ans[n][0]==KEY[n][0] and ans[n][2]>=0]
    cw=[ans[n][2] for n in KEY if ans.get(n) and ans[n][0]!=KEY[n][0] and ans[n][2]>=0]
    print("  %-8s %6s %6s %6s %6s   %s / %s" % (m, acc(), acc("A"), acc("B"), acc("C"),
        ("%.0f"%st.mean(cr)) if cr else "-", ("%.0f"%st.mean(cw)) if cw else "-"))

print()
print("=== THE TRIBE QUESTION: does disagreement predict error? ===")
allitems=list(KEY)
wrong_any=[n for n in allitems if any(members[m].get(n) and members[m][n][0]!=KEY[n][0] for m in members)]
print("  items where members SPLIT:            %s" % (splits or "none"))
print("  items where at least one was WRONG:   %s" % (wrong_any or "none"))
if splits:
    hit=[n for n in splits if n in wrong_any]
    print("  splits that were also wrong:          %s  (%d of %d)" % (hit, len(hit), len(splits)))
miss=[n for n in wrong_any if n not in splits]
print("  WRONG BUT UNANIMOUS (invisible to the tribe): %s" % (miss or "none"))
