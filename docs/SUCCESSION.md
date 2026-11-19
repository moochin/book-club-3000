# If the owner can't look after it

Everything the club uses runs on one person's accounts: the owner's. This
page is for whoever takes over if they can't keep it going, for a while or for good. It holds no
secrets: those live in the password manager (see the end).

The units themselves need nobody. They keep the clock, the countdown and the
mascot going on the last club file they fetched, and they keep trying to
fetch a new one. What stops is the ability to *change* things.

## What runs where

| Piece | Where it lives | If nobody looks after it |
|---|---|---|
| **The club file** (`bookclub.json`) | A secret gist on the owner's GitHub account | Units keep the last copy. No new meeting can be set, so exposure keeps climbing and they show DEBRIEF PENDING. |
| **The nudge hub** | A Cloudflare Worker on the owner's Cloudflare account (free plan) | Keeps running on its own. It stops only if the account is closed. Then nudges, signals and UNITS stop; everything else carries on. |
| **The firmware** | This public repository | Nothing breaks; there are just no new versions. |

Each unit is told two things on its own setup page, and keeps them: where the
club file is, and its nudge token. The hub's address is *in the club file*.
So a new club file can point the units at a new hub too.

## Small jobs (the owner is away, their accounts still work)

**Ask someone with access first.** If a second person is a member of the owner's
Cloudflare account, the hub is already covered. The gist can only be edited
from the owner's GitHub login.

If you can't edit the gist, the units are fine for months. Nothing needs
doing until the next meeting needs setting.

## Taking over (the owner's accounts are gone or unreachable)

You need: a GitHub account, a computer with Node.js 18+ (only for the hub),
and a few minutes with each unit.

### 1. Make your own club file

1. Sign in to GitHub and go to <https://gist.github.com>.
2. Start from the last good copy of the club file. If you have the old gist's
   link, open it, click **Raw** and copy everything. If not, start from
   [`bookclub.example.json`](../bookclub.example.json) in this repo and fill
   in the next meeting and the history from memory.
3. Name the file `bookclub.json`, paste it in and click **Create secret gist**.
4. Check it: `python3 tools/check_bookclub.py <your gist's Raw link>`, or the
   club editor (`python3 tools/club_editor.py`, which edits your gist directly
   once the GitHub CLI is signed in as you).
5. Copy the **Raw** link, then delete the long code between `/raw/` and
   `/bookclub.json`, so it always gets the latest version. It looks like
   `https://gist.githubusercontent.com/<you>/<id>/raw/bookclub.json`.

Treat that link like a spare key: anyone with it can read the club file.
Never commit it to this repo.

### 2. Point each unit at it

On each unit: **SETTINGS → THIS UNIT → WI-FI RESET → OPEN**. The unit opens
its setup network (`BookClub-XXXXXX`). Join it on a phone, tap **Configure
WiFi**, and:

- Pick the home network and enter its password (the page doesn't remember it).
- **Data file URL**: replace it with your new Raw link.
- **Nudge token**: the new token for this person, if you set up a new hub
  (step 3). Leave it blank to keep the old one.

Save. The unit restarts, joins Wi-Fi and fetches your file. Its name, stats
and settings are kept.

### 3. Set up a new hub (only if the old one is gone)

Follow [`worker/README.md`](../worker/README.md) on your own free Cloudflare
account. It makes a token for each person. Then:

- Put the new hub's address in your club file under `"nudges": {"url": "…"}`
  (or **NUDGES** in the club editor).
- Give each person their own token privately, for step 2.

Old tokens don't work on the new hub, so every unit needs its new one.

### 4. Firmware updates (optional)

Units update only when the club file's `firmware` block names a newer
version (see "Firmware updates" in [`firmware/README.md`](../firmware/README.md)).

- If this repo still exists, its releases keep working: you can point your
  club file at them as before.
- To make new versions, fork the repo on GitHub and switch on Actions in the
  fork (GitHub leaves them off on a fork). Its Firmware workflow builds
  `firmware.bin` and `firmware-st7789.bin` whenever you publish a release
  whose tag (`vX.Y.Z`) matches `BC_FW_VERSION` in `firmware/src/config.h`.
  Then point the `firmware` block's URLs at your fork's release downloads.

Since 1.5.0 a unit rolls back an update that doesn't prove itself, so a bad
release won't strand anyone. Try a release on one unit first with
`"only": ["Name"]` in the `firmware` block.

If a unit is ever truly stuck, it can be flashed over USB: see the top of
[`firmware/README.md`](../firmware/README.md).

## What to keep in the password manager

Stored by the owner, shared with the second person:

- The club gist's link (and its Raw link).
- The hub's address and the `UNITS` token list (the JSON from
  `npm run tokens`).
- Who has access to the Cloudflare account.
- Who the second person is.
