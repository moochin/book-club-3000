#!/usr/bin/env python3
"""Previews the Wi-Fi setup page without a unit.

Rebuilds the portal's pages the way WiFiManager assembles them, from the
library's own HTML templates (wm_strings_en.h) plus our theme
(src/portal_theme.h) and mascot (src/art_data.h), and writes them as HTML
files. Then open them in a browser, or let sim/portal_preview.js screenshot
them at phone size.

    python3 tools/build_art.py && pio pkg install -e cyd
    python3 sim/portal_preview.py sim/out/portal
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
WM = os.path.join(FW, ".pio", "libdeps", "cyd", "WiFiManager", "wm_strings_en.h")


def c_strings(path):
    """name -> value for `const char NAME[] PROGMEM = "..." "...";` (and our
    raw string literals), concatenating adjacent literals."""
    text = open(path, encoding="utf-8").read()
    text = re.sub(r"^[ \t]*//.*$", "", text, flags=re.M)  # whole-line comments between pieces
    out = {}
    for m in re.finditer(r'static const char (\w+)\[\] = R"PORTAL\((.*?)\)PORTAL";', text, re.S):
        out[m.group(1)] = m.group(2)
    for m in re.finditer(r'(?:static )?const char (\w+)\[\]\s*(?:PROGMEM)?\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', text):
        parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
        out[m.group(1)] = "".join(bytes(p, "utf-8").decode("unicode_escape") for p in parts)
    return out


def menu_items(path):
    text = open(path, encoding="utf-8").read()
    block = re.search(r"HTTP_PORTAL_MENU\[\] PROGMEM = \{(.*?)\};", text, re.S).group(1)
    return [bytes(s, "utf-8").decode("unicode_escape") for s in re.findall(r'"((?:[^"\\]|\\.)*)"', block)]


def build(theme, out_dir):
    wm = c_strings(WM)
    menu = menu_items(WM)
    ours = c_strings(os.path.join(FW, "src", "portal_theme.h"))
    art = c_strings(os.path.join(FW, "src", "art_data.h"))
    amber = theme == "amber"
    ap = "BookClub-Pete"
    head = ours["kPortalHead"]
    for k, v in {
        "%A%": "#FFB028" if amber else "#3CFF6E",
        "%D%": "#B07818" if amber else "#24A048",
        "%F%": "#4A3410" if amber else "#10401E",
        "%BG%": "#0C0802" if amber else "#030C05",
        "%G%": "rgba(255,176,40,.45)" if amber else "rgba(60,255,110,.45)",
        "%HUE%": "0" if amber else "70",
        "%IMG%": art.get(f"kPortalMascot_{theme}", ""),
        "%AP%": ap,
    }.items():
        head = head.replace(k, v)

    def page(title, body):
        return (wm["HTTP_HEAD_START"].replace("{v}", title) + wm["HTTP_SCRIPT"] + wm["HTTP_STYLE"] + head
                + wm["HTTP_HEAD_END"].replace("{c}", "invert") + body + wm["HTTP_END"])

    # Front page: title, AP name, our welcome (the "custom" menu item), wifi, info, exit.
    root = (wm["HTTP_ROOT_MAIN"].replace("{t}", "Book-Club 3000").replace("{v}", ap)
            + ours["kPortalIntro"] + menu[0] + menu[2] + menu[6] + wm["HTTP_STATUS_NONE"])

    # Wi-Fi page: networks, the form with our grouped fields, scan link.
    nets = ""
    for name, q in (("Pete-Home-5G", 4), ("Pete-Home", 3), ("BT-Hub-7A2C", 2), ("SKY-NEIGHBOUR", 1)):
        nets += (wm["HTTP_ITEM"].replace("{V}", name).replace("{v}", name)
                 .replace("{qi}", wm["HTTP_ITEM_QI"].replace("{q}", str(q)).replace("{r}", str(q * 25))
                          .replace("{i}", "l").replace("{h}", ""))
                 .replace("{qp}", ""))

    def param(pid, label, value, length, custom, show_label=True):
        item = (wm["HTTP_FORM_LABEL"] if show_label else "") + wm["HTTP_FORM_PARAM"]
        return (item.replace("{i}", pid).replace("{n}", pid).replace("{t}", label).replace("{l}", str(length))
                .replace("{v}", value).replace("{c}", custom))

    fields = (ours["kPortalSecOperative"]
              + param("owner", "Your name (as your friends know you)", "Pete", 23, "autocapitalize='words' placeholder='Pete'")
              + param("stats", "Owner card stats", "8,10,4,2,9,7,3", 31, "inputmode='numeric' placeholder='7,3,9,5,8,2,6'")
              + ours["kPortalStatsHint"] + ours["kPortalSecTerminal"]
              + param("theme", "", theme, 6, "type='hidden'", False)
              + param("rot", "", "1", 1, "type='hidden'", False)
              + ours["kPortalSecClub"]
              + param("url", "Club file (the gist's Raw link)", "", 159,
                      "type='url' autocapitalize='none' autocorrect='off' placeholder='https://gist.githubusercontent.com/...'")
              + param("ntoken", "Nudge token", "", 63,
                      "type='password' autocomplete='off' placeholder='blank = keep · - = clear'")
              + ours["kPortalAdvancedOpen"]
              + param("token", "GitHub token (private club file only)", "", 127,
                      "type='password' autocomplete='off' placeholder='blank = keep · - = clear'")
              + param("otapass", "Update password (optional)", "", 31,
                      "type='password' autocomplete='off' placeholder='blank = keep · - = clear'")
              + ours["kPortalAdvancedClose"])
    wifi = (nets + wm["HTTP_FORM_START"].replace("{v}", "wifisave")
            + wm["HTTP_FORM_WIFI"].replace("{v}", "").replace("{p}", "")
            + wm["HTTP_FORM_PARAM_HEAD"] + fields + wm["HTTP_FORM_END"] + wm["HTTP_SCAN_LINK"])

    saved = wm["HTTP_SAVED"]

    os.makedirs(out_dir, exist_ok=True)
    for name, title, body in (("root", "Book-Club 3000", root), ("wifi", "Config ESP", wifi),
                              ("saved", "Credentials saved", saved)):
        path = os.path.join(out_dir, f"{theme}_{name}.html")
        open(path, "w", encoding="utf-8").write(page(title, body))
        print("wrote", path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out", "portal")
    for theme in ("green", "amber"):
        build(theme, out)
