// Tests for the nudge hub, with an in-memory stand-in for Workers KV.
//   cd worker && npm test       (Node 18+, no Cloudflare account needed)
import assert from "node:assert/strict";
import { test } from "node:test";

import {
  handle, MAX_INBOX, MAX_PER_HOUR, MIN_GAP_MS, MAX_SIGNAL, SIGNAL_TTL_S, STATUS_REFRESH_MS, cleanMessage, clubDay,
} from "../src/hub.js";

function fakeKv() {
  const data = new Map();
  return {
    data,
    writes: 0,
    async get(key, type) {
      const v = data.get(key);
      return v === undefined ? null : type === "json" ? JSON.parse(v) : v;
    },
    ttls: new Map(),
    async put(key, value, opts) { this.writes++; data.set(key, value); this.ttls.set(key, opts && opts.expirationTtl); },
    async delete(key) { this.writes++; data.delete(key); },
  };
}

const TOKENS = { Pete: "tok-pete-123", Dave: "tok-dave-456", Sam: "tok-sam-789" };

function setup() {
  return { NUDGES: fakeKv(), UNITS: JSON.stringify(TOKENS) };
}

function req(env, who, method, path, body, now = 1_000_000) {
  const headers = who ? { authorization: `Bearer ${TOKENS[who] || who}` } : {};
  const init = { method, headers };
  if (body !== undefined) {
    init.body = typeof body === "string" ? body : JSON.stringify(body);
    headers["content-type"] = "application/json";
  }
  return handle(new Request(`https://hub.example${path}`, init), env, now).then(async (r) => ({
    status: r.status,
    body: await r.json(),
  }));
}

test("rejects missing and unknown tokens", async () => {
  const env = setup();
  assert.equal((await req(env, null, "GET", "/inbox")).status, 401);
  assert.equal((await req(env, "not-a-token", "GET", "/inbox")).status, 401);
});

test("roster lists everyone but the caller", async () => {
  const r = await req(setup(), "Pete", "GET", "/roster");
  assert.deepEqual(r.body, { me: "Pete", friends: ["Dave", "Sam"] });
});

test("a nudge lands in the friend's inbox once", async () => {
  const env = setup();
  const sent = await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "GET YOUR ASS ONLINE" });
  assert.equal(sent.status, 200);
  assert.deepEqual(sent.body.sent, ["Dave"]);
  const inbox = await req(env, "Dave", "GET", "/inbox");
  assert.deepEqual(inbox.body.nudges, [{ from: "Pete", msg: "GET YOUR ASS ONLINE", at: 1_000_000 }]);
  assert.deepEqual((await req(env, "Dave", "GET", "/inbox")).body.nudges, []);  // emptied
  assert.deepEqual((await req(env, "Sam", "GET", "/inbox")).body.nudges, []);   // not Sam's
});

test("an empty inbox check costs no KV writes", async () => {
  const env = setup();
  await req(env, "Dave", "GET", "/inbox");
  assert.equal(env.NUDGES.writes, 0);
});

test("everyone (*) reaches all friends but not the sender", async () => {
  const env = setup();
  const r = await req(env, "Sam", "POST", "/nudge", { to: "*", msg: "THINKING OF YOU, LOSER" });
  assert.deepEqual(r.body.sent, ["Pete", "Dave"]);
  assert.equal((await req(env, "Pete", "GET", "/inbox")).body.nudges.length, 1);
  assert.equal((await req(env, "Sam", "GET", "/inbox")).body.nudges.length, 0);
});

test("bad recipients and messages are refused", async () => {
  const env = setup();
  assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Bob", msg: "hi" })).status, 404);
  assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Pete", msg: "hi" })).status, 404);
  assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "" })).status, 400);
  assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "x".repeat(41) })).status, 400);
  assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "emoji 🎮" })).status, 400);
  assert.equal((await req(env, "Pete", "POST", "/nudge", "not json")).status, 400);
});

test("curly quotes are straightened for the units' font", () => {
  assert.equal(cleanMessage("I’M ON. YOU’RE NOT."), "I'M ON. YOU'RE NOT.");
});

test("rate limits: a short gap, then a cap per hour", async () => {
  const env = setup();
  let t = 1_000_000;
  assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "a" }, t)).status, 200);
  assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "b" }, t + 1000)).status, 429);
  for (let i = 1; i < MAX_PER_HOUR; i++) {
    t += MIN_GAP_MS;
    assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "c" }, t)).status, 200);
  }
  t += MIN_GAP_MS;
  assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "d" }, t)).status, 429);
  // An hour after the first one, there's room again.
  assert.equal((await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "e" }, 1_000_000 + 3_600_001)).status, 200);
  // Other units aren't affected.
  assert.equal((await req(env, "Sam", "POST", "/nudge", { to: "Dave", msg: "f" }, t)).status, 200);
});

test("an inbox keeps only the newest nudges", async () => {
  const env = setup();
  const inbox = Array.from({ length: MAX_INBOX }, (_, i) => ({ from: "Sam", msg: `old ${i}`, at: i }));
  env.NUDGES.data.set("inbox:Dave", JSON.stringify(inbox));
  await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "newest" });
  const got = (await req(env, "Dave", "GET", "/inbox")).body.nudges;
  assert.equal(got.length, MAX_INBOX);
  assert.equal(got[got.length - 1].msg, "newest");
  assert.equal(got[0].msg, "old 1");
});

test("a broken UNITS secret locks everyone out rather than crashing", async () => {
  const env = { NUDGES: fakeKv(), UNITS: "{not json" };
  assert.equal((await req(env, "Pete", "GET", "/roster")).status, 401);
});

test("unknown routes are 404", async () => {
  assert.equal((await req(setup(), "Pete", "GET", "/nope")).status, 404);
});

// ---- Signals ------------------------------------------------------------------

const DAY_MS = SIGNAL_TTL_S * 1000;

test("a signal reaches everyone, the sender included, with the time it has left", async () => {
  const env = setup();
  const quote = "Look on my works, ye Mighty, and despair!";
  const r = await req(env, "Pete", "POST", "/signal", { msg: quote });
  assert.equal(r.status, 200);
  assert.deepEqual(r.body.signal, { from: "Pete", msg: quote, at: 1_000_000, left: SIGNAL_TTL_S });
  for (const who of ["Pete", "Dave", "Sam"]) {
    const got = await req(env, who, "GET", "/signals", undefined, 1_000_000 + 3_600_000);
    assert.deepEqual(got.body.signals, [{ from: "Pete", msg: quote, at: 1_000_000, left: SIGNAL_TTL_S - 3600 }]);
  }
  // Reading doesn't use a signal up, and KV is told to delete it after 24 hours.
  assert.equal((await req(env, "Dave", "GET", "/signals")).body.signals.length, 1);
  assert.equal(env.NUDGES.ttls.get("signal:Pete"), SIGNAL_TTL_S);
});

test("a signal is gone after 24 hours, even before KV sweeps it up", async () => {
  const env = setup();
  await req(env, "Sam", "POST", "/signal", { msg: "Hope is the thing with feathers" });
  const before = await req(env, "Dave", "GET", "/signals", undefined, 1_000_000 + DAY_MS - 1000);
  assert.equal(before.body.signals.length, 1);
  assert.equal(before.body.signals[0].left, 1);
  const after = await req(env, "Dave", "GET", "/signals", undefined, 1_000_000 + DAY_MS);
  assert.deepEqual(after.body.signals, []);
});

// UK times, for the one-a-day rule (Date.UTC months are 0-based).
const T = (y, mo, d, h, mi = 0) => Date.UTC(y, mo - 1, d, h, mi);

test("signals come newest first, one per unit, and the next day's replaces the old", async () => {
  const env = setup();
  await req(env, "Pete", "POST", "/signal", { msg: "first" }, T(2026, 11, 14, 20));
  await req(env, "Dave", "POST", "/signal", { msg: "second" }, T(2026, 11, 15, 8));
  await req(env, "Pete", "POST", "/signal", { msg: "third" }, T(2026, 11, 15, 9));
  const got = (await req(env, "Sam", "GET", "/signals", undefined, T(2026, 11, 15, 9))).body.signals;
  assert.deepEqual(got.map((s) => [s.from, s.msg]), [["Pete", "third"], ["Dave", "second"]]);
});

test("one signal a day each, the day turning at UK midnight", async () => {
  const env = setup();
  const send = (who, at, msg = "a quote") => req(env, who, "POST", "/signal", { msg }, at);
  assert.equal((await send("Pete", T(2026, 11, 15, 9))).status, 200);
  const again = await send("Pete", T(2026, 11, 15, 23, 59));
  assert.equal(again.status, 429);
  assert.match(again.body.error, /one signal a day/);
  assert.equal((await send("Dave", T(2026, 11, 15, 23, 59))).status, 200);  // others aren't affected
  assert.equal((await send("Pete", T(2026, 11, 16, 0, 1))).status, 200);    // a new day
  // In summer the UK is an hour ahead: 23:30 UTC on 1 July is already 2 July there.
  assert.equal((await send("Sam", T(2026, 7, 1, 22, 30))).status, 200);
  assert.equal((await send("Sam", T(2026, 7, 1, 23, 30))).status, 200);
  assert.equal(clubDay(T(2026, 7, 1, 23, 30)), "2026-07-02");
});

test("signals are longer than nudges but not unlimited", async () => {
  const env = setup();
  assert.equal((await req(env, "Dave", "POST", "/signal", { msg: "x".repeat(MAX_SIGNAL) })).status, 200);
  assert.equal((await req(env, "Pete", "POST", "/signal", { msg: "x".repeat(MAX_SIGNAL + 1) })).status, 400);
  assert.equal((await req(env, "Sam", "POST", "/signal", { msg: "" })).status, 400);
  assert.equal((await req(env, "Sam", "POST", "/signal", "not json")).status, 400);
  assert.equal((await req(env, null, "GET", "/signals")).status, 401);
});

test("signals from a unit taken off the line-up aren't shown", async () => {
  const env = setup();
  await req(env, "Sam", "POST", "/signal", { msg: "still here" });
  env.UNITS = JSON.stringify({ Pete: TOKENS.Pete, Dave: TOKENS.Dave });
  assert.deepEqual((await req(env, "Pete", "GET", "/signals")).body.signals, []);
});

test("an ellipsis is spelled out for the units' font", () => {
  assert.equal(cleanMessage("To be continued…", MAX_SIGNAL), "To be continued...");
});

// ---- The web page ---------------------------------------------------------------

test("the web page is served without a token, locked down", async () => {
  const r = await handle(new Request("https://hub.example/"), setup(), 1_000_000);
  assert.equal(r.status, 200);
  assert.match(r.headers.get("content-type"), /text\/html/);
  const csp = r.headers.get("content-security-policy");
  assert.match(csp, /default-src 'none'/);
  assert.match(csp, /connect-src 'self'/);
  assert.match(csp, /frame-ancestors 'none'/);
  const html = await r.text();
  assert.match(html, /Book-Club 3000/);
  assert.match(html, /src="data:image\/png;base64,/);   // the mascot, inline
  assert.doesNotMatch(html, /\/inbox/);                    // never takes a unit's nudges
  assert.doesNotMatch(html, /innerHTML/);                  // hub text only via textContent
});

test("other pages still need a token", async () => {
  assert.equal((await req(setup(), null, "GET", "/roster")).status, 401);
});

// ---- One poll for everything (firmware 1.5+) ------------------------------------

test("poll returns who I am, friends, my nudges and the signals, in one go", async () => {
  const env = setup();
  await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "GET YOUR ASS ONLINE" });
  await req(env, "Sam", "POST", "/signal", { msg: "Hope is the thing with feathers" });
  const r = await req(env, "Dave", "GET", "/poll");
  assert.equal(r.status, 200);
  assert.equal(r.body.me, "Dave");
  assert.deepEqual(r.body.friends, ["Pete", "Sam"]);
  assert.deepEqual(r.body.nudges.map((n) => n.msg), ["GET YOUR ASS ONLINE"]);
  assert.deepEqual(r.body.signals.map((s) => s.from), ["Sam"]);
  // Until the unit acks them, the nudges stay: a reply it couldn't read
  // isn't lost. Signals stay on the air either way.
  const again = await req(env, "Dave", "GET", "/poll");
  assert.deepEqual(again.body.nudges.map((n) => n.msg), ["GET YOUR ASS ONLINE"]);
  const acked = await req(env, "Dave", "GET", `/poll?ack=${again.body.nudges[0].at}`);
  assert.deepEqual(acked.body.nudges, []);
  assert.equal(acked.body.signals.length, 1);
});

test("poll's ack drops only what the unit has taken", async () => {
  const env = setup();
  await req(env, "Pete", "POST", "/nudge", { to: "Dave", msg: "ONE" });
  const first = await req(env, "Dave", "GET", "/poll");
  const at = first.body.nudges[0].at;
  await req(env, "Sam", "POST", "/nudge", { to: "Dave", msg: "TWO" }, at + 1000);
  const r = await req(env, "Dave", "GET", `/poll?ack=${at}`, undefined, at + 2000);
  assert.deepEqual(r.body.nudges.map((n) => n.msg), ["TWO"]);
  assert.equal((await req(env, "Dave", "GET", "/poll?ack=garbage")).body.nudges.length, 1);
});

test("an empty poll costs no KV writes", async () => {
  const env = setup();
  await req(env, "Dave", "GET", "/poll");
  assert.equal(env.NUDGES.writes, 0);
  assert.equal((await req(env, null, "GET", "/poll")).status, 401);
});

test("a reply is marked as one; a plain nudge isn't", async () => {
  const env = setup();
  await req(env, "Dave", "POST", "/nudge", { to: "Pete", msg: "ON MY WAY", re: true });
  await req(env, "Sam", "POST", "/nudge", { to: "Pete", msg: "OI", re: "yes" });
  const got = (await req(env, "Pete", "GET", "/inbox")).body.nudges;
  assert.equal(got[0].re, true);
  assert.equal("re" in got[1], false);
});

const HEALTH = "&fw=1.5.25&fwst=&why=POWER%20ON&bad=0&mem=140K%20FREE&up=10&panel=st7789";

test("poll records the unit's health for /fleet", async () => {
  const env = setup();
  const t = 50_000_000;
  await req(env, "Dave", "GET", `/poll?ack=0${HEALTH}`, undefined, t);
  const { units } = (await req(env, "Pete", "GET", "/fleet", undefined, t)).body;
  assert.deepEqual(units.map((u) => u.name), ["Pete", "Dave", "Sam"]);
  const dave = units[1];
  assert.equal(dave.fw, "1.5.25");
  assert.equal(dave.why, "POWER ON");
  assert.equal(dave.panel, "st7789");
  assert.equal(dave.bad, false);
  assert.equal(dave.seen, t);
  assert.equal(dave.started, t - 10 * 60_000);
  assert.equal(units[0].seen, 0);  // Pete never reported
});

test("an unchanged status is rewritten only every 30 minutes", async () => {
  const env = setup();
  let t = 50_000_000;
  await req(env, "Dave", "GET", `/poll?ack=0${HEALTH}`, undefined, t);
  const after = env.NUDGES.writes;
  t += 5 * 60_000;  // next poll, 5 minutes up: memory moved, nothing else did
  await req(env, "Dave", "GET", `/poll?ack=0${HEALTH.replace("up=10", "up=15").replace("140K", "120K")}`, undefined, t);
  assert.equal(env.NUDGES.writes, after);
  // A change of version is written straight away.
  await req(env, "Dave", "GET", `/poll?ack=0${HEALTH.replace("1.5.25", "1.5.26").replace("up=10", "up=15")}`, undefined, t);
  assert.equal(env.NUDGES.writes, after + 1);
  // So is a restart, even with the same reason.
  t += 60_000;
  await req(env, "Dave", "GET", `/poll?ack=0${HEALTH.replace("1.5.25", "1.5.26").replace("up=10", "up=0")}`, undefined, t);
  assert.equal(env.NUDGES.writes, after + 2);
  // And the "seen" time, once 30 minutes have passed.
  t += STATUS_REFRESH_MS;
  await req(env, "Dave", "GET", `/poll?ack=0${HEALTH.replace("1.5.25", "1.5.26").replace("up=10", "up=31")}`, undefined, t);
  assert.equal(env.NUDGES.writes, after + 3);
});

test("a poll without health (older firmware) records nothing", async () => {
  const env = setup();
  await req(env, "Dave", "GET", "/poll?ack=0");
  assert.equal(env.NUDGES.writes, 0);
  assert.equal((await req(env, "Pete", "GET", "/fleet")).body.units[1].seen, 0);
});

test("health fields are cleaned", async () => {
  const env = setup();
  await req(env, "Dave", "GET", `/poll?ack=0&fw=${"9".repeat(60)}&why=%F0%9F%8E%AE&bad=1`);
  const dave = (await req(env, "Pete", "GET", "/fleet")).body.units[1];
  assert.equal(dave.fw, "");
  assert.equal(dave.why, "");
  assert.equal(dave.bad, true);
});

test("fleet needs a token", async () => {
  assert.equal((await req(setup(), null, "GET", "/fleet")).status, 401);
});
