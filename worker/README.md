# Nudge hub

A tiny [Cloudflare Worker](https://developers.cloudflare.com/workers/) that
passes nudges ("GET YOUR ASS ONLINE") between the club's units, and keeps the
club's signals: quotes broadcast to everyone that vanish after 24 hours. Each
unit checks in every 5 minutes with one `/poll` (nudges, signals and the
friends list together); sending goes through the knob menu (**SETTINGS → SEND
NUDGE** and **SETTINGS → SIGNALS**) or the hub's web page.

One person sets it up once, on their own free Cloudflare account. It needs no
server and costs nothing at this size. Four units checking every 5 minutes
is about 1,200 requests a day, against the free plan's 100,000.

## Set it up

You need Node.js 18 or later and a free Cloudflare account.

1. Install the tools (from this `worker/` folder):

   ```bash
   npm install
   ```

2. Sign in to Cloudflare:

   ```bash
   npx wrangler login
   ```

3. Create the storage for the inboxes:

   ```bash
   npx wrangler kv namespace create NUDGES
   ```

   It prints an `id`. Paste it into `wrangler.toml` in place of
   `PASTE_YOUR_KV_NAMESPACE_ID_HERE`. The id isn't a secret.

4. Make a token for each unit, using each person's name as their friends
   should see it:

   ```bash
   npm run tokens -- Pete Dave Sam Jonno
   ```

   This prints one line of JSON, e.g. `{"Pete":"k3J…","Dave":"9xQ…",…}`.
   Keep it somewhere safe, like a password manager.

5. Give the Worker the tokens as a secret (paste the JSON line when asked):

   ```bash
   npx wrangler secret put UNITS
   ```

6. Deploy:

   ```bash
   npx wrangler deploy
   ```

   It prints the hub's address, e.g.
   `https://bookclub-nudge.<your-subdomain>.workers.dev`.

7. Tell the units:
   - Put the address in the club file, under **NUDGES** in the club editor
     (or `"nudges": {"url": "…"}` in the gist). Every unit picks it up on its
     next sync.
   - Send each friend **their own** token, privately. They enter it on their
     unit's setup page (**SETTINGS → WI-FI RESET → OPEN**, then *Nudge token*).

## Updating the hub

When `src/hub.js` changes (signals were added after the first version), run
`npx wrangler deploy` from this folder again. The storage and the `UNITS`
secret are kept.

## Changing the line-up

To add or remove someone, or replace a leaked token, run `npm run tokens`
again (or edit the JSON by hand) and repeat step 5. Then give anyone whose
token changed their new one. Units with an old token get "unknown or missing
unit token" until they're updated.

## How it works

| Request | What it does |
| --- | --- |
| `GET /` | The web page: sign in with a token, send nudges and signals in your own words (no token needed to load it) |
| `GET /roster` | Who this unit is, and the friends it can nudge |
| `GET /inbox` | This unit's waiting nudges, which are then removed |
| `POST /nudge` `{"to": "Dave" or "*", "msg": "…"}` | Sends a nudge to one friend or everyone |
| `GET /poll` | All of the above for this unit in one reply: `me`, `friends`, `nudges` (then removed) and `signals`. Units from 1.5 use only this |
| `POST /signal` `{"msg": "…"}` | Puts this unit's signal on the air for 24 hours, replacing its last one |
| `GET /signals` | Every unit's live signal (this one's too), newest first, each with `left`: seconds until it's gone |

- **Authentication:** every request carries `Authorization: Bearer <token>`,
  and the token says which unit is calling.
- **Messages:** plain text, 40 characters at most. Curly quotes are
  straightened for the units' font.
- **Inboxes:** each keeps the newest 20.
- **Signals:** plain text, 100 characters at most. Each is stored with a
  24-hour expiry, so Cloudflare deletes it, and `/signals` skips any copy
  older than that it hasn't deleted yet. A unit can broadcast one a day (the
  day starts at midnight, UK time), from the unit or the web page. Reading
  signals doesn't use them up.
- **The web page** is in `src/page.js`, in the units' setup-page look, and
  needs nothing but the hub. It never reads an inbox, so it can't take a
  unit's nudges. A token in the address after `#t=` signs straight in; the
  page then removes it from the address bar. The mascot at the top is
  `src/mascot.js`: after redrawing `art/mascot/level0.png`, run
  `python3 tools/make-mascot.py` here and redeploy.
- **Rate limit:** a unit can send one nudge every 15 seconds and 12 an hour.
- **Free-plan limits:** checking an empty inbox doesn't write to storage, so
  it stays well inside the free plan's 1,000 writes a day.

## Test it

```bash
npm test
```

The tests run in Node with an in-memory stand-in for the storage, so they
need no Cloudflare account. CI runs them on every change to `worker/`.

To try the real Worker runtime locally, put
`UNITS={"Pete":"tok-pete","Dave":"tok-dave"}` in a file called `.dev.vars`
(git ignores it), then run:

```bash
npx wrangler dev --local
```

Open `http://127.0.0.1:8787/` for the web page, or talk to it with `curl`:

```bash
curl -X POST -H "Authorization: Bearer tok-pete" -H "content-type: application/json" -d '{"to":"Dave","msg":"GET YOUR ASS ONLINE"}' http://127.0.0.1:8787/nudge
```

```bash
curl -H "Authorization: Bearer tok-dave" http://127.0.0.1:8787/inbox
```

```bash
curl -X POST -H "Authorization: Bearer tok-pete" -H "content-type: application/json" -d '{"msg":"Look on my works, ye Mighty, and despair!"}' http://127.0.0.1:8787/signal
```

```bash
curl -H "Authorization: Bearer tok-dave" http://127.0.0.1:8787/signals
```

The code is in `src/hub.js`. `src/index.js` is only the entry point, because
the Workers runtime treats every named export of the entry module as a
handler.
