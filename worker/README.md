# Nudge hub

A tiny [Cloudflare Worker](https://developers.cloudflare.com/workers/) that
passes nudges ("GET YOUR ASS ONLINE") between the club's units. Each unit
checks its inbox once a minute; sending goes through the knob menu
(**SETTINGS → SEND NUDGE**).

One person sets it up once, on their own free Cloudflare account. It needs no
server and costs nothing at this size. Four units checking every minute is
about 5,800 requests a day, against the free plan's 100,000.

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

## Changing the line-up

To add or remove someone, or replace a leaked token, run `npm run tokens`
again (or edit the JSON by hand) and repeat step 5. Then give anyone whose
token changed their new one. Units with an old token get "unknown or missing
unit token" until they're updated.

## How it works

| Request | What it does |
| --- | --- |
| `GET /roster` | Who this unit is, and the friends it can nudge |
| `GET /inbox` | This unit's waiting nudges, which are then removed |
| `POST /nudge` `{"to": "Dave" or "*", "msg": "…"}` | Sends a nudge to one friend or everyone |

- **Authentication:** every request carries `Authorization: Bearer <token>`,
  and the token says which unit is calling.
- **Messages:** plain text, 40 characters at most. Curly quotes are
  straightened for the units' font.
- **Inboxes:** each keeps the newest 20.
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

Then talk to it with `curl`:

```bash
curl -X POST -H "Authorization: Bearer tok-pete" -H "content-type: application/json" -d '{"to":"Dave","msg":"GET YOUR ASS ONLINE"}' http://127.0.0.1:8787/nudge
```

```bash
curl -H "Authorization: Bearer tok-dave" http://127.0.0.1:8787/inbox
```

The code is in `src/hub.js`. `src/index.js` is only the entry point, because
the Workers runtime treats every named export of the entry module as a
handler.
