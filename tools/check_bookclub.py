#!/usr/bin/env python3
"""Checks bookclub.json the way the Book-Club 3000 firmware reads it.

Run: python3 tools/check_bookclub.py [path]   (defaults to ./bookclub.json)

Errors mean every unit would REJECT the file and keep showing its cached copy.
Warnings mean the units will cope, but the file probably isn't what you meant.
Mirrors firmware/lib/bookclub_state/src/bc_parse.cpp.
"""
import datetime
import json
import re
import sys

MAX_BYTES = 12 * 1024
TIME_RE = re.compile(r"^\d{1,2}:\d{2}$")


def parse_date(s):
    if not isinstance(s, str) or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", s):
        return None
    try:
        return datetime.date.fromisoformat(s)
    except ValueError:
        return None


def time_ok(s):
    if not isinstance(s, str) or not TIME_RE.match(s):
        return False
    h, m = map(int, s.split(":"))
    return h <= 23 and m <= 59


def main(path):
    errors, warnings = [], []
    raw = open(path, "rb").read()
    if len(raw) > MAX_BYTES:
        errors.append(f"file is {len(raw)} bytes; units accept at most {MAX_BYTES}")
    try:
        doc = json.loads(raw)
    except ValueError as e:
        errors.append(f"not valid JSON: {e}")
        doc = None

    if doc is not None and not isinstance(doc, dict):
        errors.append("the top level must be a { ... } object")
        doc = None

    if doc is not None:
        last = doc.get("last")
        last_date = parse_date(last.get("date")) if isinstance(last, dict) else None
        if last_date is None:
            errors.append('"last" must have a "date" written as YYYY-MM-DD, e.g. "2026-08-15"')

        nxt = doc.get("next")
        if nxt is not None:
            if not isinstance(nxt, dict):
                warnings.append('"next" should be an object or null; units will show "no date"')
            else:
                nd = parse_date(nxt.get("date"))
                if nd is None:
                    warnings.append('"next.date" is not a valid YYYY-MM-DD date; units will show "no date"')
                elif last_date and nd < last_date:
                    warnings.append('"next.date" is before "last.date"; units will show "no date"')
                if "time" in nxt and nxt["time"] is not None and not time_ok(nxt["time"]):
                    warnings.append('"next.time" should be HH:MM (24-hour), e.g. "19:30"; it will be ignored')

        hist = doc.get("history", [])
        if not isinstance(hist, list):
            warnings.append('"history" should be a [ ... ] list; it will be ignored')
            hist = []
        dates = []
        for i, e in enumerate(hist):
            d = parse_date(e.get("date")) if isinstance(e, dict) else None
            if d is None:
                warnings.append(f"history entry {i + 1} has no valid date; it will be skipped")
            else:
                dates.append(d)
        if dates and dates != sorted(dates, reverse=True):
            warnings.append('"history" should be newest first (units sort it anyway)')
        if last_date and dates and dates[0] != last_date:
            warnings.append('the first "history" entry does not match "last" ("last" wins)')
        if last_date and any(d > last_date for d in dates):
            warnings.append('"history" has meetings after "last"; they will be hidden')

        sq = doc.get("side_quests", [])
        if not isinstance(sq, list):
            warnings.append('"side_quests" should be a [ ... ] list; it will be ignored')
            sq = []
        for i, e in enumerate(sq):
            if not isinstance(e, dict) or parse_date(e.get("date")) is None:
                warnings.append(f"side quest {i + 1} has no valid date; it will be skipped")
            elif "time" in e and e["time"] is not None and not time_ok(e["time"]):
                warnings.append(f'side quest {i + 1}: "time" should be HH:MM; it will be ignored')

        caps = doc.get("fun_caps", {})
        if not isinstance(caps, dict):
            warnings.append('"fun_caps" should be a { "Name": number } object; it will be ignored')
            caps = {}
        for name, v in caps.items():
            n = v.get("adjust") if isinstance(v, dict) else v
            if isinstance(n, bool) or not isinstance(n, int):
                warnings.append(f'fun_caps "{name}": use a whole number, e.g. 2 or -3, '
                                f'or {{"adjust": -3, "note": "why"}}; it will be ignored')

        bdays = doc.get("birthdays", {})
        if not isinstance(bdays, dict):
            warnings.append('"birthdays" should be a { "Name": "MM-DD" } object; it will be ignored')
            bdays = {}
        for name, v in bdays.items():
            ok = isinstance(v, str) and (parse_date("2000-" + v) if len(v) == 5 else
                                          parse_date("2000" + v[4:]) if len(v) == 10 else None)
            if not ok:
                warnings.append(f'birthday for "{name}" should be "MM-DD" or "YYYY-MM-DD"; it will be ignored')

        fw = doc.get("firmware")
        if fw is not None:
            urls = [str(fw.get(k, "")) for k in ("url", "url_st7789")] if isinstance(fw, dict) else []
            if not isinstance(fw, dict) or not fw.get("version") or not any(u.startswith("https://") for u in urls):
                warnings.append('"firmware" needs a "version" and an https:// "url" or "url_st7789"; it will be ignored')

    for w in warnings:
        print(f"WARNING: {w}")
    for e in errors:
        print(f"ERROR:   {e}")
    if errors:
        print(f"{path}: units would REJECT this file and keep their cached copy.")
        return 1
    print(f"{path}: OK" + (f" ({len(warnings)} warning(s))" if warnings else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "bookclub.json"))
