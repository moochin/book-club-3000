# Book-Club 3000

A desk terminal that shows how long it has been since the club last met (your
"exposure") and counts down to the next meeting (the "quest"). Every unit reads
the same file, `bookclub.json`, from a secret GitHub gist that the club shares.

![Screens rendered by the UI simulator](docs/screens.png)

```
bookclub.example.json   what the club file looks like (the real one is in the gist)
firmware/       the PlatformIO project for the ESP32 "Cheap Yellow Display"
art/            mascot art: PNGs you draw, baked into the firmware (art/README.md)
tools/          club_editor.py (edit the club file), check_bookclub.py, art tools
worker/         the nudge hub: a small Cloudflare Worker (worker/README.md)
docs/           the original handover/spec
case/           (to come) STL files for the printed case
```

## Updating the dates

The club file lives in a **secret gist**, not in this repo, so the meeting
details stay out of public view. Ask Pete for its link, and don't post it
anywhere public. Only the gist's owner can edit it: open the gist, click
**Edit**, change
`bookclub.json`, and click **Update secret gist**. Every unit picks it up
within 24 hours, or straight away from **settings → REFRESH NOW**. GitHub may
serve the old copy for a few minutes after an edit.

A secret gist isn't listed or searchable, but anyone with the link can read
it, so treat the link like a spare key. Gists have no collaborators, so
everyone else sends their changes to the owner.

**The easy way (gist owner):** `python3 tools/club_editor.py` opens a form
in your browser with every field: next and last meeting, a button for "the
next meeting happened", side quests, Fun Caps, history, the message and the
firmware update block. It checks each change with the units' own rules and
won't save a file they would reject. It needs the GitHub CLI signed in as
the gist's owner (`gh auth login`). The first run asks for the gist's link
and remembers it in `.bookclub-gist`, which git ignores. The page only
runs on your computer.

```json
{
  "version": 1,
  "last": { "date": "2026-08-15", "plan": "Retro games + curry", "location": "Pete's house" },
  "next": { "date": "2026-12-05", "time": "19:30", "plan": "Film night + pizza", "location": "TBC" },
  "history": [
    { "date": "2026-08-15", "plan": "Retro games + curry", "location": "Pete's house" },
    { "date": "2026-03-02", "plan": "Pub, then co-op games", "location": "The Bell, Winchester" }
  ],
  "side_quests": [],
  "fun_caps": {},
  "birthdays": {},
  "message": "Bring snacks"
}
```

- Dates are `YYYY-MM-DD`, times are 24-hour `HH:MM`, both UK local time.
- **After a meeting:** set `last` to that meeting, add the same entry to the
  top of `history`, and set `next` to the new date, or to `null` if there
  isn't one yet. Until `last` changes, the exposure counter keeps counting
  and the unit shows **DEBRIEF PENDING**.
- **Side quests** are get-togethers that aren't a proper club meeting, like
  the families coming along. List them in `side_quests`, each with a
  `date` and optionally `time`, `plan` and `location`:

  ```json
  "side_quests": [
    { "date": "2026-12-20", "time": "14:00", "plan": "Xmas do with the families", "location": "Pete's house" }
  ],
  ```

  The next one shows as a countdown on STAT and in full on DATA, and on the
  day the unit reassures you that no Fun Caps are being spent. They
  **never reset the exposure counter**: only a proper meeting in `last`
  does. Past side quests stay in the LOG, marked as side quests, but don't
  count towards the meeting total or the longest gap. Leave old ones in the
  list or delete them, either is fine.
- **Fun Caps** are the club's joke currency of goodwill from home, shown on
  each unit's owner card (hold the knob 2 s → OWNER CARD). Everyone earns
  **1 cap per fortnight** at home, up to **10**; each proper club night
  costs **3** (side quests are free). The balance is worked out from the
  meeting history, so it needs no upkeep and every unit agrees. To award a
  bonus or a penalty, add the person's name (as set on their unit) to
  `fun_caps`, with a whole number or a number and a reason:

  ```json
  "fun_caps": {
    "Pete": { "adjust": 2, "note": "Did the school run" },
    "Dave": -3
  },
  ```

  Adjustments are running totals: change the number rather than adding new
  entries. A balance can drop to -5 (overdrawn, in the doghouse). The rules
  live in `CapsRules` in `firmware/lib/bookclub_state/src/bc_state.h`.
- **Birthdays** go in `birthdays`, as `"MM-DD"` or a full `"YYYY-MM-DD"`
  (the year isn't used), under each person's name as set on their unit:

  ```json
  "birthdays": { "Pete": "03-14", "Dave": "1984-10-22" },
  ```

  On the day, every unit puts the mascot in his party hat and says whose
  birthday it is; the birthday person's own unit says "HAPPY BIRTHDAY" and
  plays the jingle once (outside quiet hours). A club meeting on the same
  day still gets its own celebration. A 29 February birthday is marked on
  28 February in other years. The DATA tab always shows whose birthday is
  next, e.g. `BDAY  DAVE  22 OCT (12D)`.
- `plan` is what's on (games, a film, food, the pub...) and shows on the
  DATA and LOG tabs. `next`, `time`, `plan`, `location` and `message` are
  all optional. Older files that say `book` instead of `plan` still work.
- If an edit breaks the file, the units ignore the broken version and keep
  showing the last good one; **ABOUT** shows why it was rejected. Fix it and
  they pick it up on the next sync. To check an edit first, run
  `python3 tools/check_bookclub.py <the gist's Raw link>`.

## Setting up a unit (each friend)

1. Plug it in. With no Wi-Fi saved it shows **WI-FI SETUP** and a network
   name like `BookClub-3F2A1C`.
2. Join that network on your phone. A setup page opens (or browse to
   `192.168.4.1`) — tap **Configure WiFi**.
3. Pick your home network, enter its password, and fill in your name, theme
   (`green` or `amber`), the seven owner-card stats, the screen rotation, and
   your **Nudge token** if you've been given one.
   For **Data file URL**, paste the gist's **Raw** link (open the gist, click
   **Raw**, copy the address, then delete the long code between `/raw/` and
   `/bookclub.json` so it always gets the latest version). Save.

The unit joins your network, sets its clock and fetches the file. If your
Wi-Fi drops it carries on counting from the dates it already has and keeps
retrying in the background.

**Touch screen:** tap STAT, DATA or LOG to switch tabs, tap the clock for
settings, tap the mascot for your owner card, and tap a screen's title to go
back. On LOG, tap the top or bottom half of the list to page through it. A
stylus works best.
**Knob:** turn to switch between STAT, DATA and LOG. On LOG, press to switch
between changing tabs and scrolling. Hold 2 s for settings (refresh, send nudge, sound,
brightness, theme, scanlines, screen off at night, dim when idle, owner card,
how to use, about, Wi-Fi reset).
**The screen** switches itself off at night (23:00–07:00 by default) and after
10 minutes in a dark room, and dims after 30 minutes untouched; turn the knob
or tap to wake it. Both times are in the settings menu. It's an LCD, so it
can't burn in, but this saves its backlight, which slowly wears with use.
**Factory reset:** hold the BOOT button while plugging in, for 10 seconds.

## Nudges

Poke a friend from your desk: **SETTINGS → SEND NUDGE**, pick a friend (or
EVERYONE), then a message. It lands on their unit within a minute, with a
chirp, a flashing blue light and the message in big letters until they press
or tap. In quiet hours (22:00–08:00) it arrives silently and waits on screen.

The built-in messages include *GET YOUR ASS ONLINE*, *THINKING OF YOU,
LOSER*, *WHERE ARE YOU, COWARD?* and *I'M ON. YOU'RE NOT. SHAME.* To use the
club's own in-jokes instead, add them under **NUDGES** in the club editor
(up to 16, 40 characters each).

Nudges go through a small hub that one person sets up once on a free
Cloudflare account: see [`worker/README.md`](worker/README.md). They then put
the hub's address in the club file and give each friend their own token for
the setup page.

Firmware details, the bring-up checklist and the release process are in
[`firmware/README.md`](firmware/README.md).
