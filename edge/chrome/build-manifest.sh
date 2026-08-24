#!/bin/sh
# build-manifest.sh -- generate manifest.json from sites.json.
#
# The manifest is GENERATED so that campus hostnames never appear in viki's
# source. Warren: "this is how i will use viki - these are not what viki is."
# The reader framework is viki's; d2l.coloradomesa.edu is not, and baking it
# into a checked-in manifest made the boundary invisible.
#
# Usage: sh edge/chrome/build-manifest.sh
set -e
D=$(cd "$(dirname "$0")" && pwd)
python3 - "$D" <<'PY'
import json, sys, os
d = sys.argv[1]
cfg = json.load(open(os.path.join(d, 'sites.json')))
hosts = ["http://127.0.0.1/*"]
scripts = []
for s in cfg['sites']:
    js = os.path.join(d, 'sites', s['name'] + '.js')
    if not os.path.exists(js):
        print("  skipping %s: no sites/%s.js" % (s['name'], s['name']))
        continue
    hosts += s['matches']
    scripts.append({
        "matches": s['matches'],
        "js": ["content/common.js", "sites/%s.js" % s['name']],
        "run_at": "document_idle"
    })
manifest = {
    "manifest_version": 3,
    "name": "viki reader",
    "version": "0.1.0",
    "description": "Reads your own notifications into your local viki. Observe only - it never posts, replies, or clicks.",
    "permissions": ["storage", "alarms"],
    "host_permissions": hosts,
    "background": {"service_worker": "background.js"},
    "action": {"default_popup": "popup.html", "default_title": "viki reader"},
    "content_scripts": scripts
}
open(os.path.join(d, 'manifest.json'), 'w').write(json.dumps(manifest, indent=2) + "\n")
print("  %d site(s): %s" % (len(scripts), ", ".join(s['matches'][0] for s in scripts)))
PY
