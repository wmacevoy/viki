#!/usr/bin/env python3
"""viki-httpd.py -- push/pull over plain HTTP. The whole network leg.

WHY THIS IS NOT THE ctypes SHIM P1-transport.md PROPOSED. That design ships
ROWS: manifest, diff, fetch bodies, insert. The only redaction-safe ingress in
core is viki_merge(sqlite3*), which sweeps kind='redact' tombstones on the
DESTINATION inside its own transaction; viki_put does not sweep. So a
row-shipping transport re-animates redacted content -- and "every row reaches
both" REWARDS that bug, because re-adding rows makes the two stores agree MORE.
`viki pull PATH` IS viki_merge. Shipping a file and pulling it is therefore
redaction-safe by construction, with no ABI to hand-copy, no .so to build, no
viki_abi() guard to remember, and nothing added to core.

THE FOUR BOUNDS, kept: nothing in core/ (this is host-layer, a separate
process); no auth in viki (a fronting container decides access); no TLS in
viki (Caddy terminates; this speaks plain HTTP); anti-entropy, not a log tail
(pull is a union -- associative, commutative, idempotent, no clock).

THE DEVIATION, named rather than buried: P1 said "no process calls". This is
all process calls. That was the right rule for a server calling CORE, where
ctypes drift is silent and there is no compiler to catch it. Here the process
call IS the feature: it reuses the exact merge path the CLI and every probe
already exercise, instead of a second implementation that must be kept in step.

WHAT IT DOES NOT DO: it ships the WHOLE store each exchange, not a delta. At
140 assertions / 1.5MB that is free. `observe --lacking` is the optimisation
and it is ADDITIVE -- serve a filtered clone instead of a full one, same verbs,
same safety. Do not build it before someone measures a store where it matters.
"""
import argparse, os, subprocess, sys, tempfile, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_BODY = 512 * 1024 * 1024          # a diary, not a stream. Refuse the rest.

def viki(store, keyfile, *args):
    """One place that knows how to invoke the binary."""
    cmd = [os.environ.get("VIKI_BIN", "/mnt/lbn-tribes/viki/core/build/viki"),
           "--keyfile", keyfile, "--store", store] + list(args)
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout.strip(), p.stderr.strip()

def snapshot(store, keyfile):
    """A CHECKPOINTED clone, never the live file: the store has rows in -wal,
    and serving the .diary alone would hand a peer a torn read."""
    fd, path = tempfile.mkstemp(suffix=".diary"); os.close(fd); os.unlink(path)
    rc, _, err = viki(store, keyfile, "clone", path)
    if rc != 0:
        if os.path.exists(path): os.unlink(path)
        raise RuntimeError("clone failed: " + err)
    return path

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, fmt, *a):
        sys.stderr.write("%s %s\n" % (self.address_string(), fmt % a))

    def do_GET(self):
        if self.path != "/diary": return self.send_error(404, "only /diary")
        try: path = snapshot(self.server.store, self.server.keyfile)
        except Exception as e: return self.send_error(500, str(e))
        try:
            n = os.path.getsize(path)
            self.send_response(200)
            self.send_header("Content-Type", "application/vnd.viki.diary")
            self.send_header("Content-Length", str(n))
            self.end_headers()
            with open(path, "rb") as f:
                while True:
                    b = f.read(1 << 20)
                    if not b: break
                    self.wfile.write(b)
        finally:
            os.unlink(path)

    def do_POST(self):
        if self.path != "/diary": return self.send_error(404, "only /diary")
        n = int(self.headers.get("Content-Length") or 0)
        if n <= 0 or n > MAX_BODY: return self.send_error(413, "bad length")
        fd, path = tempfile.mkstemp(suffix=".diary")
        try:
            with os.fdopen(fd, "wb") as f:
                left = n
                while left:
                    b = self.rfile.read(min(left, 1 << 20))
                    if not b: break
                    f.write(b); left -= len(b)
            # PULL, NOT INSERT. viki_merge sweeps redactions on the destination;
            # this is the whole reason the transport moves files.
            rc, out, err = viki(self.server.store, self.server.keyfile, "pull", path)
            if rc != 0: return self.send_error(500, err or "pull failed")
            body = (out + "\n").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)
        finally:
            os.unlink(path)

def do_pull(url, store, keyfile):
    fd, path = tempfile.mkstemp(suffix=".diary")
    try:
        with urllib.request.urlopen(url.rstrip("/") + "/diary") as r, os.fdopen(fd, "wb") as f:
            while True:
                b = r.read(1 << 20)
                if not b: break
                f.write(b)
        rc, out, err = viki(store, keyfile, "pull", path)
        if rc != 0: sys.exit("pull: " + (err or "failed"))
        print("pull  <- %s" % out)
    finally:
        os.unlink(path)

def do_push(url, store, keyfile):
    path = snapshot(store, keyfile)
    try:
        with open(path, "rb") as f: data = f.read()
        req = urllib.request.Request(url.rstrip("/") + "/diary", data=data,
                  headers={"Content-Type": "application/vnd.viki.diary"}, method="POST")
        # THE SERVER ANSWERS IN ITS OWN VOICE: its POST handler runs `pull`, so
        # its reply says "pulled". From here that was a PUSH. Label it locally
        # or the two legs of a sync print the same word and look like a bug.
        with urllib.request.urlopen(req) as r:
            print("push  -> %s (as the peer counts it)" % r.read().decode().strip())
    finally:
        os.unlink(path)

def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("verb", choices=["serve", "pull", "push", "sync"])
    ap.add_argument("url", nargs="?")
    ap.add_argument("--store", required=True); ap.add_argument("--keyfile", required=True)
    ap.add_argument("--bind", default="127.0.0.1:8710")
    a = ap.parse_args()
    if a.verb == "serve":
        host, _, port = a.bind.rpartition(":")
        srv = ThreadingHTTPServer((host or "127.0.0.1", int(port)), Handler)
        srv.store, srv.keyfile = a.store, a.keyfile
        sys.stderr.write("viki-httpd on %s serving %s\n" % (a.bind, a.store))
        srv.serve_forever()
    elif not a.url:
        sys.exit("%s needs a URL" % a.verb)
    elif a.verb == "pull": do_pull(a.url, a.store, a.keyfile)
    elif a.verb == "push": do_push(a.url, a.store, a.keyfile)
    else:
        # SYNC IS NOT A THIRD VERB -- it is pull then push (a5102c4cb41e).
        # push alone drops whatever another peer left there since.
        do_pull(a.url, a.store, a.keyfile); do_push(a.url, a.store, a.keyfile)

if __name__ == "__main__":
    main()
