#!/usr/bin/env python3
"""A local web page for editing the club file in its secret gist.

    python3 tools/club_editor.py [gist link or ID]

Opens http://127.0.0.1:8300 in your browser. The page loads bookclub.json from
the gist, gives you a form for it, checks every change with the same rules as
the units (check_bookclub.py) and saves it back to the gist. Saving refuses a
file the units would reject. The gist keeps every revision, so a bad save can
be undone from its Revisions tab.

Needs the GitHub CLI, signed in as the gist's owner (`gh auth login`).
The gist link is remembered in .bookclub-gist at the repo root, which git
ignores: never commit it. Standard library only. The server only listens on
this computer.
"""
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import webbrowser

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_bookclub import check  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "club_editor.html")
GIST_FILE = os.path.join(ROOT, ".bookclub-gist")
FILE_NAME = "bookclub.json"
PORT = 8300


def gist_id_from(text):
    m = re.search(r"\b([0-9a-f]{20,40})\b", text or "")
    return m.group(1) if m else None


def resolve_gist():
    arg = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("BOOKCLUB_GIST", "")
    gid = gist_id_from(arg)
    if not gid and os.path.exists(GIST_FILE):
        gid = gist_id_from(open(GIST_FILE).read())
    if not gid:
        gid = gist_id_from(input("Paste the club gist's link: "))
    if not gid:
        sys.exit("That doesn't look like a gist link.")
    with open(GIST_FILE, "w") as f:
        f.write(gid + "\n")
    return gid


def gh(args, stdin=None):
    r = subprocess.run(["gh"] + args, input=stdin, capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        raise RuntimeError((r.stderr or r.stdout).strip() or "gh failed")
    return r.stdout


def load(gid):
    g = json.loads(gh(["api", f"gists/{gid}"]))
    f = g["files"].get(FILE_NAME)
    if not f:
        raise RuntimeError(f"the gist has no {FILE_NAME}")
    if f.get("truncated"):
        raise RuntimeError(f"{FILE_NAME} is too big to edit here")
    return {"content": f["content"], "url": g["html_url"], "updated": g["updated_at"]}


def save(gid, content):
    body = json.dumps({"files": {FILE_NAME: {"content": content}}})
    g = json.loads(gh(["api", "-X", "PATCH", f"gists/{gid}", "--input", "-"], stdin=body))
    return g["updated_at"]


def make_handler(gid):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def _local(self):
            # Only answer this computer's browser (guards against DNS rebinding).
            host = (self.headers.get("Host") or "").split(":")[0]
            return host in ("127.0.0.1", "localhost")

        def _send(self, code, body, ctype="application/json"):
            data = body.encode("utf-8") if isinstance(body, str) else body
            self.send_response(code)
            self.send_header("Content-Type", ctype + "; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def _json(self, code, obj):
            self._send(code, json.dumps(obj))

        def do_GET(self):
            if not self._local():
                return self._send(403, "forbidden", "text/plain")
            if self.path == "/":
                return self._send(200, open(PAGE, "rb").read(), "text/html")
            if self.path == "/api/file":
                try:
                    f = load(gid)
                    errors, warnings = check(f["content"].encode("utf-8"))
                    return self._json(200, dict(f, errors=errors, warnings=warnings))
                except Exception as e:  # report to the page
                    return self._json(502, {"error": str(e)})
            self._send(404, "not found", "text/plain")

        def do_POST(self):
            if not self._local() or self.headers.get("Content-Type") != "application/json":
                return self._send(403, "forbidden", "text/plain")
            n = int(self.headers.get("Content-Length") or 0)
            try:
                content = json.loads(self.rfile.read(n))["content"]
            except (ValueError, KeyError, TypeError):
                return self._json(400, {"error": "bad request"})
            errors, warnings = check(content.encode("utf-8"))
            if self.path == "/api/check":
                return self._json(200, {"errors": errors, "warnings": warnings})
            if self.path == "/api/save":
                if errors:
                    return self._json(422, {"errors": errors, "warnings": warnings})
                try:
                    return self._json(200, {"updated": save(gid, content), "warnings": warnings})
                except Exception as e:
                    return self._json(502, {"error": str(e)})
            self._send(404, "not found", "text/plain")

    return Handler


def main():
    if not shutil.which("gh"):
        sys.exit("Needs the GitHub CLI (gh): https://cli.github.com, then `gh auth login`.")
    gid = resolve_gist()
    server = None
    for port in range(PORT, PORT + 20):
        try:
            server = http.server.ThreadingHTTPServer(("127.0.0.1", port), make_handler(gid))
            break
        except OSError:
            continue
    if not server:
        sys.exit("No free port between 8300 and 8319.")
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print(f"Club editor: {url}  (Ctrl+C to stop)")
    threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
