#!/usr/bin/env python3
"""Checks the guide's own consistency, so its conventions are enforced rather than hoped for (SE1/O3):
every relative link and anchor resolves; every DM1 citation names a real principle in the file that
defines it; every principle says, on the line under its heading, whether it is *New.* or which SE1
principles it *Reframes*; every SE1 link names the pinned SE1 release and the chapter and anchor its ID
implies, and a principle and anchor that exist in vendor/se1, which is the pinned release, with the copies of
SE1's files here equal to it; copies cite no DM1 ID their labeled release lacks; a patch release changes no principle,
AGENTS.md, or template; README's principle index and its SE1 map match the chapters exactly; chapter
numbers agree; llms.txt links to files that exist; and CHANGELOG.md, AGENTS.md, and the templates agree
on the release. Exits nonzero on any problem. Python 3.9 or later and git; standard library only.
Usage: python3 -I tools/check.py
       python3 -I tools/check.py --print   (the index and the SE1 map as they should read)"""
import os
import re
import subprocess
import sys
import unicodedata

sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # Windows' default code page cannot print every link

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = "DM1"
SE = "SE1"
# The SE1 release this guide is written against. Every SE1 link names it, so a reader lands on the
# text that was read, not on whatever main says now (SE1/R10, SE1/U9). Bumping it is one edit here
# plus a rewrite of the links, and a walk through SE1's CHANGELOG for every ID this guide reframes.
SE_RELEASE = "v1.7"
SE_BLOB = f"https://github.com/wmacevoy/software-engineering/blob/{SE_RELEASE}/"
SE_CHAPTER = {"P": "01-purpose", "U": "02-unknowing", "F": "03-feedback", "R": "04-requirements", "D": "05-design",
              "T": "06-testing", "O": "07-operating", "W": "08-work", "H": "09-people", "A": "10-agents"}
HEADING = re.compile(r'### <a id="([a-z0-9]+)"></a>DM1/([A-Z][0-9]+)\. (.*)')
BASIS = re.compile(r"\*(New|Reframes (.+))\.\*")
SE_LINK = re.compile(r"\[SE1/([A-Z][0-9]+)\]\(([^)]+)\)")

problems = 0


def problem(msg):
    global problems
    print(msg)
    problems += 1


def read(path):
    with open(path, encoding="utf-8", newline="") as fh:
        return fh.read()


def git_in(where, *args):
    return subprocess.run(["git", "-C", where, *args], capture_output=True, check=True).stdout.decode("utf-8")


def git(*args):
    return git_in(root, *args)


GIT_FAILED = (subprocess.CalledProcessError, OSError, UnicodeDecodeError)


def posix(path):
    return path.replace(os.sep, "/")


def rel(path, start=root):  # forward slashes on every platform, so a report and a git path read the same
    try:
        return posix(os.path.relpath(path, start))
    except ValueError:  # Windows: another drive has no relative path
        return posix(path)


# Releases come from CHANGELOG.md's "## v1.0 — date" headings, newest first. Until one is dated,
# copies (AGENTS.md, templates) link to main; after, to the newest dated release.
RELEASE = re.compile(r"## (v[0-9]+\.[0-9]+(?:\.[0-9]+)?) — ([0-9]{4}-[0-9]{2}-[0-9]{2}|unreleased)")
changelog = read(os.path.join(root, "CHANGELOG.md"))
release_lines = [m for m in (RELEASE.fullmatch(line) for line in changelog.split("\n")) if m]
releases = [m.group(1) for m in release_lines]
latest = next((m.group(1) for m in release_lines if m.group(2) != "unreleased"), None)


def minor(v):  # a patch release (v1.0.1) changes no principle, AGENTS.md, or template, so copies keep the minor's label
    return ".".join(v.split(".")[:2])


GH = re.compile(r"^https://github\.com/wmacevoy/data-mining/blob/(main|" + "|".join(re.escape(r) for r in releases) + ")/")

files = []
for folder, dirs, names in os.walk(root):
    # Dot-directories include .git.
    dirs[:] = sorted(d for d in dirs if not d.startswith("."))
    # vendor/ at the root is another repository's, and answers to its own checker.
    if folder == root:
        dirs[:] = [d for d in dirs if d != "vendor"]
    # A HANDOFF note is finished history: it keeps the SE1 links it was written with.
    files += [os.path.join(folder, n) for n in sorted(names)
              if n.endswith(".md") and not n.startswith((".", "HANDOFF-"))]


def in_dir(path, name):
    return rel(path).startswith(name + "/")


def slug(heading):  # GitHub's heading slug, close enough for the headings used here
    kept = "".join(c for c in heading.strip().lower()
                   if unicodedata.category(c)[0] in "LN" or c.isspace() or c in "_-")
    return re.sub(r"\s", "-", kept)


def anchors_of_text(text):
    anchors = set()
    for line in text.split("\n"):
        if re.match(r"#{1,6} ", line):
            anchors.add(slug(re.sub(r'<a id="[^"]+"></a>', "", re.sub(r"^#+ ", "", line), count=1)))
    anchors.update(re.findall(r'<a id="([^"]+)">', text))
    return anchors


def anchors_of(path):
    return anchors_of_text(read(path))


# SE1 IDs, read from vendor/se1, which must be the pinned release: passing against another says nothing about
# it (SE1/U7).
se_root = os.path.join(root, "vendor", "se1")
se_text, se_source = None, None


def se_chapter(name):
    p = os.path.join(se_root, "guide", f"{name}.md")
    return read(p) if os.path.exists(p) else None


if os.path.exists(os.path.join(se_root, "README.md")) and os.path.exists(os.path.join(se_root, "AGENTS.md")):
    se_text = read(os.path.join(se_root, "README.md"))
    se_source = f"{SE_RELEASE} in vendor/se1"
    se_agents = read(os.path.join(se_root, "AGENTS.md"))
    m = re.search(r"This copy is\s+from\s+\*\*(v[0-9.]+)\*\*", se_agents)
    if not m or m.group(1) != SE_RELEASE:
        problem(f"vendored SE1   vendor/se1 is {m.group(1) if m else 'unlabeled'}, and SE_RELEASE is {SE_RELEASE}: copy it again at that tag")
    # The copies that run or load here are the vendored files, unedited.
    for copy, source in (("se", "se"), ("se.ps1", "se.ps1"), ("se.py", "se.py"), ("docs/SE1-AGENTS.md", "AGENTS.md")):
        a, b = os.path.join(root, copy), os.path.join(se_root, source)
        if not (os.path.exists(a) and os.path.exists(b) and read(a).replace("\r\n", "\n") == read(b).replace("\r\n", "\n")):
            problem(f"vendored SE1   {copy} differs from vendor/se1/{source}: copy it again, unedited")
else:
    problem("vendored SE1   vendor/se1 has no README.md or AGENTS.md: copy SE1 there at the pinned tag")

se_ids = set(re.findall(r"\[\*\*SE1/([A-Z][0-9]+)\*\*\]", se_text)) if se_text else None
se_anchors = {}  # SE1 chapter file -> its anchors at SE_RELEASE, read once; None if unreadable


def se_has_anchor(id_):
    name = SE_CHAPTER[id_[0]]
    if name not in se_anchors:
        t = se_chapter(name)
        se_anchors[name] = None if t is None else anchors_of_text(t)
    a = se_anchors[name]
    return a is None or id_.lower() in a


def check_se(id_, url, where):
    name = SE_CHAPTER.get(id_[0])
    if not name:
        problem(f"unknown SE1 chapter {where}: SE1/{id_}")
    elif url != f"{SE_BLOB}guide/{name}.md#{id_.lower()}":
        problem(f"SE1 link       {where}: SE1/{id_} should link to {SE_BLOB}guide/{name}.md#{id_.lower()}")
    if se_ids is not None and id_ not in se_ids:
        problem(f"unknown SE1 ID {where}: SE1/{id_}")
    elif se_ids is not None and name and not se_has_anchor(id_):
        problem(f"SE1 anchor     {where}: SE1/{id_} is in {SE_RELEASE}'s index but guide/{name}.md has no anchor for it")


# Principles, their basis lines, and the index and map README must contain.
defined_in = {}  # DM1 ID -> chapter file
index = []
reframes = {}  # SE1 ID -> [DM1 links]
ORDER = "PUFRDTOWHA"


def se_sort(id_):
    return (ORDER.find(id_[0]), int(id_[1:]))


for f in sorted(x for x in files if in_dir(x, "guide")):
    lines = read(f).split("\n")
    for i, line in enumerate(lines):
        m = HEADING.fullmatch(line)
        if not m:
            continue
        anchor, id_, title = m.groups()
        if anchor != id_.lower():
            problem(f'anchor         {rel(f)}: {id_} has anchor "{anchor}"')
        if id_ in defined_in:
            problem(f"{id_} defined twice")
        defined_in[id_] = f
        link = f"guide/{os.path.basename(f)}#{anchor}"
        # An unescaped | in a list item renders as a table on the site, so the index escapes it.
        index.append(f"- [**{VERSION}/{id_}**]({link}) {title.replace('|', chr(92) + '|')}")
        basis = BASIS.fullmatch(lines[i + 2] if i + 2 < len(lines) else "")
        if not basis:
            problem(f"no basis       {rel(f)}: {id_} needs *New.* or *Reframes [SE1/…](…).* under its heading")
            continue
        if basis.group(1) == "New":
            continue
        cited = SE_LINK.findall(basis.group(2))
        if not cited:
            problem(f"no SE1 link    {rel(f)}: {id_} says Reframes but links no SE1 principle")
        for se_id, _ in cited:
            reframes.setdefault(se_id, []).append(f"[{VERSION}/{id_}]({link})")

se_map = [f"- [SE1/{s}]({SE_BLOB}guide/{SE_CHAPTER.get(s[0])}.md#{s.lower()}) → {', '.join(reframes[s])}"
          for s in sorted(reframes, key=se_sort)]

# Links: relative files and anchors, DM1 citations, SE1 citations.
for f in files:
    text = read(f)
    for m in SE_LINK.finditer(text):
        check_se(m.group(1), m.group(2), f"{rel(f)}: {m.group(0)}")
    for m in re.finditer(r"\[([^\]]+)\]\(([^)]+)\)", text):
        label, href = m.groups()
        where = f"{rel(f)}: {m.group(0)}"
        gh = GH.match(href)
        if not gh and href.startswith("https://github.com/wmacevoy/data-mining/blob/"):
            problem(f"unknown ref    {where}")
            continue
        if gh:
            href = os.path.relpath(os.path.join(root, href[gh.end():]), os.path.dirname(f))
        elif re.match(r"[a-z]+:", href):
            continue
        parts = href.split("#")
        target, anchor = parts[0], parts[1] if len(parts) > 1 else None
        target_file = os.path.normpath(os.path.join(os.path.dirname(f), target)) if target else f
        if not os.path.exists(target_file):
            problem(f"missing file   {where}")
            continue
        if anchor and anchor not in anchors_of(target_file):
            problem(f"missing anchor {where}")
        cite = label.replace("*", "")
        if re.fullmatch(r"[PUFRDTOWHA][0-9]+", cite):
            problem(f"unversioned    {where}")
            continue
        v = re.fullmatch(r"(DM[0-9]+)/([A-Z][0-9]+)", cite)
        if v:
            if v.group(1) != VERSION:
                problem(f"wrong version  {where}")
            elif v.group(2) not in defined_in:
                problem(f"unknown ID     {where}")
            elif defined_in[v.group(2)] != target_file:
                problem(f"wrong file     {where} (defined in {rel(defined_in[v.group(2)])})")

# Bare citations, the form AGENTS.md uses: "(DM1/F5, SE1/T3)". Only chapter letters count, so "(K3)"
# and other parentheticals are left alone.
CITE = r"(?:(?:DM|SE)[0-9]+/)?[PUFRDTOWHA][0-9]+"
for f in files:
    for m in re.finditer(rf"\(({CITE}(?:,\s*{CITE})*)\)", read(f)):
        for cite in re.split(r",\s*", m.group(1)):
            where = f"{rel(f)}: {m.group(0)}"
            version, id_ = cite.split("/") if "/" in cite else (None, cite)
            if not version:
                problem(f"unversioned    {where}")
            elif version == VERSION:
                if id_ not in defined_in:
                    problem(f"unknown ID     {where}")
            elif version == SE:
                if se_ids is not None and id_ not in se_ids:
                    problem(f"unknown SE1 ID {where}")
            else:
                problem(f"wrong version  {where}")

# Chapter numbers: the file prefix, the title, and every link label that names a chapter agree.
chapter_title = {}
for f in files:
    if re.search(r"guide[\\/][0-9]{2}-[a-z-]+\.md$", f):
        title = re.sub(r"^# ", "", read(f).split("\n")[0], count=1)
        n = int(os.path.basename(f)[:2])
        if not title.startswith(f"{n}. "):
            problem(f'title number   {rel(f)}: "{title}"')
        chapter_title[os.path.basename(f)] = title
for f in files:
    for m in re.finditer(r"\[([0-9]+)\. ([^\]]+)\]\(([^)#]*?)([0-9]{2}-[a-z-]+\.md)\)", read(f)):
        if m.group(3).startswith("http"):
            continue  # a chapter of SE1, not of this guide
        title = chapter_title.get(m.group(4))
        if title and f"{m.group(1)}. {m.group(2)}" != title:
            problem(f'chapter label  {rel(f)}: {m.group(0)} (title is "{title}")')

# llms.txt points agents at raw files by URL, which the link check above never sees.
RAW = "https://raw.githubusercontent.com/wmacevoy/data-mining/main/"
for m in re.finditer(r"\]\(([^)]+)\)", read(os.path.join(root, "llms.txt"))):
    url = m.group(1)
    se_raw = re.match(r"https://raw\.githubusercontent\.com/wmacevoy/software-engineering/([^/]+)/", url)
    if se_raw:
        if se_raw.group(1) != SE_RELEASE:
            problem(f"SE1 not pinned llms.txt: {m.group(0)} should name {SE_RELEASE}")
        continue
    if not url.startswith(RAW):
        problem(f"not a raw link llms.txt: {m.group(0)}")
    elif not os.path.exists(os.path.join(root, url[len(RAW):])):
        problem(f"missing file   llms.txt: {m.group(0)}")

# Releases: the newest release states the principle count, and every copy names the same release.
count = re.search(r"· ([0-9]+) principles", changelog)
if not releases:
    problem('CHANGELOG.md has no "## vX.Y — yyyy-mm-dd|unreleased" release')
if not count or int(count.group(1)) != len(defined_in):
    problem(f"principle count CHANGELOG.md says {count.group(1) if count else 'nothing'}, the chapters define {len(defined_in)}")
copies = ["AGENTS.md"] + [rel(f) for f in files if in_dir(f, "templates")]
copy_ref = minor(latest) if latest else "main"
for f in copies:
    for m in re.finditer(r"wmacevoy/data-mining/(?:blob|tree)/([^/\s;)]+)", read(os.path.join(root, f))):
        if m.group(1) != copy_ref:
            problem(f"stale release  {f}: links to {m.group(1)}, copies should link to {copy_ref}")
agents = re.search(r"This copy is\s+from\s+\*\*(v[0-9]+\.[0-9]+)\*\*", read(os.path.join(root, "AGENTS.md")))
newest = minor(releases[0]) if releases else None
if not agents or agents.group(1) != newest:
    problem(f"stale release  AGENTS.md: says it is from {agents.group(1) if agents else 'nothing'}, newest release is "
            f"{releases[0] if releases else 'nothing'} (copies say its minor, {newest})")


def bodies(text):
    """A principle's text: its heading to the next heading or horizontal rule, whitespace collapsed so
    rewrapping is not a change. The rule matters: the `---` after a chapter's last principle is not its
    text, or adding a principle after it would "change" it."""
    out, id_, buf = {}, None, []

    def flush():
        if id_:
            out[id_] = re.sub(r"\s+", " ", " ".join(buf)).strip()
    for line in text.split("\n"):
        m = HEADING.fullmatch(line)
        if m or re.match(r"#{1,3} ", line) or line == "---":
            flush()
            id_, buf = (m.group(2) if m else None), []
        if id_:
            buf.append(line)
    flush()
    return out


# A patch release may only change what is not a principle or a copy: compare with the minor's tag.
# The rule holds while the patch is the newest CHANGELOG section; work toward the next release goes
# under an unreleased section above it, and from then on the minor-release rules apply.
top = release_lines[0].group(1) if release_lines else None
if latest and top == latest and len(latest.split(".")) == 3:
    base = minor(latest)
    try:
        git("rev-parse", "-q", "--verify", f"refs/tags/{base}")
        for f in copies:
            if git("show", f"{base}:{f}") != read(os.path.join(root, f)):
                problem(f"patch release  {latest} changed {f}; copies are only re-copied at a minor release")
        for f in sorted(set(defined_in.values())):
            before = bodies(git("show", f"{base}:{rel(f)}"))
            for id_, now in bodies(read(f)).items():
                if before.get(id_) != now:
                    problem(f"patch release  {latest} changed {id_}; a principle changes only at a minor release")
    except GIT_FAILED:
        problem(f"patch release  {latest} needs its minor's tag {base} (git fetch --tags)")

readme = read(os.path.join(root, "README.md"))
for line in index:
    if line not in readme:
        problem(f"README index out of date: {line}")
for line in se_map:
    if line not in readme:
        problem(f"README SE1 map out of date: {line}")


def index_ids(text):
    return set(re.findall(r"\[\*\*DM1/([A-Z][0-9]+)\*\*\]", text))


# A copy that is ahead of its label says nothing (SE1/U7): every DM1 ID AGENTS.md and the templates
# cite must be in README's index at the release they name, read from that tag when it exists and
# from the working tree otherwise (a release not yet tagged, or copies that still link to main).
for f in copies:
    text = read(os.path.join(root, f))
    if f == "AGENTS.md":
        label = agents.group(1) if agents else None
    else:
        m = re.search(r"wmacevoy/data-mining/blob/(v[0-9]+\.[0-9]+(?:\.[0-9]+)?)/", text)
        label = m.group(1) if m else None
    if not label:
        continue
    try:
        ids = index_ids(git("show", f"{label}:README.md"))
    except GIT_FAILED:
        ids = index_ids(readme)
    for id_ in dict.fromkeys(re.findall(r"DM1/([A-Z][0-9]+)", text)):
        if id_ not in ids:
            problem(f"ahead of label {f}: cites DM1/{id_}, which {label} does not define")

if "--print" in sys.argv:
    print("\n".join(["", *index, "", *se_map, ""]))
reframing = {link for links in reframes.values() for link in links}
print(f"{len(files)} files, {len(defined_in)} principles ({len(defined_in) - len(reframing)} new), "
      f"{len(reframes)} SE1 principles reframed, SE1 IDs "
      f"{'checked against ' + se_source if se_ids is not None else 'NOT checked (no vendor/se1)'}, {problems} problems")
sys.exit(1 if problems else 0)
