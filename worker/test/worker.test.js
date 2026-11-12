// Tests for the nudge hub, with an in-memory stand-in for Workers KV.
//   cd worker && npm test       (Node 18+, no Cloudflare account needed)
import assert from "node:assert/strict";
import { test } from "node:test";

import { handle, MAX_INBOX, MAX_PER_HOUR, MIN_GAP_MS, cleanMessage } from "../src/hub.js";

function fakeKv() {
  const data = new Map();
  return {
    data,
    writes: 0,
    async get(key, type) {
      const v = data.get(key);
      return v === undefined ? null : type === "json" ? JSON.parse(v) : v;
    },
    async put(key, value) { this.writes++; data.set(key, value); },
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
