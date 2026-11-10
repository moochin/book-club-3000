# Book-Club 3000

A desk terminal that shows how long it has been since the club last met (your
"exposure") and counts down to the next meeting (the "quest"). Every unit reads
the same file, [`bookclub.json`](bookclub.json), from this repo.

![Screens rendered by the UI simulator](docs/screens.png)

```
bookclub.json   the shared dates file — edit this one
firmware/       the PlatformIO project for the ESP32 "Cheap Yellow Display"
art/            mascot art: PNGs you draw, baked into the firmware (art/README.md)
tools/          check_bookclub.py (the same checks the units apply), art tools
docs/           the original handover/spec
case/           (to come) STL files for the printed case
```

## Updating the dates (everyone)

Open [`bookclub.json`](bookclub.json) on GitHub, click the pencil, edit, and
commit. Every unit picks it up within 24 hours, or straight away from the
knob menu: **hold 2 s → REFRESH NOW**. GitHub may serve the old copy for a few
minutes after an edit.

```json
{
  "version": 1,
  "last": { "date": "2026-08-15", "plan": "Retro games + curry", "location": "Pete's house" },
  "next": { "date": "2026-12-05", "time": "19:30", "plan": "Film night + pizza", "location": "TBC" },
  "history": [
    { "date": "2026-08-15", "plan": "Retro games + curry", "location": "Pete's house" },
    { "date": "2026-03-02", "plan": "Pub, then co-op games", "location": "The Bell, Winchester" }
  ],
  "message": "Bring snacks"
}
```

- Dates are `YYYY-MM-DD`, times are 24-hour `HH:MM`, both UK local time.
- **After a meeting:** set `last` to that meeting, add the same entry to the
  top of `history`, and set `next` to the new date, or to `null` if there
  isn't one yet. Until `last` changes, the exposure counter keeps counting
  and the unit shows **DEBRIEF PENDING**.
- `plan` is what's on (games, a film, food, the pub...) and shows on the
  DATA and LOG tabs. `next`, `time`, `plan`, `location` and `message` are
  all optional. Older files that say `book` instead of `plan` still work.
- If an edit breaks the file, the **Check bookclub.json** action shows a red
  cross on the commit, and the units ignore the broken version and keep
  showing the last good one. Fix it and they pick it up on the next sync.
  To check locally: `python3 tools/check_bookclub.py`.

## Setting up a unit (each friend)

1. Plug it in. With no Wi-Fi saved it shows **WI-FI SETUP** and a network
   name like `BookClub-3F2A1C`.
2. Join that network on your phone. A setup page opens (or browse to
   `192.168.4.1`) — tap **Configure WiFi**.
3. Pick your home network, enter its password, and fill in your name, theme
   (`green` or `amber`), the seven owner-card stats, and the screen rotation.
   Leave the data URL as it is. Save.

The unit joins your network, sets its clock and fetches the file. If your
Wi-Fi drops it carries on counting from the dates it already has and keeps
retrying in the background.

**Knob:** turn to switch between STAT, DATA and LOG. On LOG, press to switch
between changing tabs and scrolling. Hold 2 s for settings (refresh, sound,
brightness, theme, scanlines, owner card, about, Wi-Fi reset).
**Factory reset:** hold the BOOT button while plugging in, for 10 seconds.

Firmware details, the bring-up checklist and the release process are in
[`firmware/README.md`](firmware/README.md).
