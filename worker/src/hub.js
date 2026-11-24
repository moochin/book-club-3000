// Book-Club 3000 nudge hub logic: passes short messages ("GET YOUR ASS
// ONLINE") between the club's units. src/index.js is the Worker entry point;
// this module is kept separate because the Workers runtime treats every named
// export of the entry module as a handler.
//
// Bindings (see wrangler.toml and README.md):
//   NUDGES  a KV namespace holding each unit's inbox and send log
//   UNITS   a secret: JSON {"Pete": "<token>", "Dave": "<token>", ...}
//
// Every request carries "Authorization: Bearer <token>"; the token says which
// unit is calling. Routes:
//   GET  /roster   -> {"me": "Pete", "friends": ["Dave", "Sam"]}
//   GET  /inbox    -> {"nudges": [{"from","msg","at"}]}  (and empties it)
//   POST /nudge    {"to": "Dave" | "*", "msg": "...", "re": true?} -> {"sent": ["Dave"]}
//                  A nudge to everyone ("*") lands in the sender's inbox too,
//                  so their own unit shows it like everyone else's ("sent"
//                  still lists only the friends).
//                  "re" marks a reply to a nudge; units show it as one and
//                  offer no reply to it, so two units can't ping-pong.
//   GET  /         the web page for sending from a phone (no token needed;
//                  it signs in with one, then uses the routes below)
//   POST /signal   {"msg": "..."} -> {"signal": {"from","msg","at","left"}}
//   GET  /signals  -> {"signals": [{"from","msg","at","left"}]}  (newest first)
//   GET  /poll?ack=AT -> all three in one reply: {"me","friends","nudges","signals"}.
//                  Units from 1.5 use this alone, so each check is one TLS
//                  connection instead of three. Unlike /inbox it doesn't empty
//                  the inbox: it drops only the nudges at or before `ack`
//                  (the newest "at" the unit has taken), so a reply the unit
//                  couldn't read is sent again rather than lost.
//                  Units from 1.5.25 add their health to the query (fw, fwst,
//                  why, bad, mem, up, panel), which the hub keeps for /fleet.
//   GET  /fleet    -> {"units": [{"name","fw","fwst","why","bad","mem","panel",
//                  "seen","started"}]}: every unit's last report, for the
//                  page's UNITS table. "seen" and "started" are ms since 1970
//                  (0: never). Any unit's token can read it.
//
// A status is written only when it changes (version, probation, why it last
// started, a restart) or every 30 minutes, so "seen" is up to 30 minutes
// old: KV's free plan allows 1,000 writes a day, and every unit polls 288
// times a day.
//
// Signals are quotes broadcast to the whole club, one per unit per day (UK
// time), and each is gone 24 hours after it was sent: KV
// deletes it, and /signals ignores any copy KV hasn't swept up yet. "left" is
// seconds until it goes, so the units don't need their own clocks to agree.

export const MAX_MSG = 40;           // characters per nudge
export const MAX_INBOX = 20;         // oldest dropped beyond this
export const MIN_GAP_MS = 15_000;    // between one unit's nudges
export const MAX_PER_HOUR = 12;      // per sending unit
export const MAX_SIGNAL = 100;       // characters per signal
export const SIGNAL_TTL_S = 86_400;  // a signal lasts 24 hours
export const TIME_ZONE = "Europe/London";  // where the club's "day" starts
export const STATUS_REFRESH_MS = 30 * 60_000;  // rewrite an unchanged status this often
export const STATUS_TTL_S = 60 * 86_400;       // forget a unit silent this long
const MAX_FIELD = 48;                          // characters per status field

import { PAGE } from "./page.js";

function json(status, body) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });
}

// Constant-time string comparison, so token checks don't leak timing.
function sameToken(a, b) {
  if (typeof a !== "string" || typeof b !== "string" || a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

function units(env) {
  try {
    const u = JSON.parse(env.UNITS || "{}");
    return u && typeof u === "object" && !Array.isArray(u) ? u : {};
  } catch {
    return {};
  }
}

function caller(request, env) {
  const m = /^Bearer\s+(\S+)$/.exec(request.headers.get("authorization") || "");
  if (!m) return null;
  for (const [name, token] of Object.entries(units(env))) {
    if (sameToken(m[1], token)) return name;
  }
  return null;
}

// Plain printable text only: the units' bitmap font is ASCII.
export function cleanMessage(msg, max = MAX_MSG) {
  if (typeof msg !== "string") return null;
  const s = msg.replace(/[‘’]/g, "'").replace(/[“”]/g, '"').replace(/…/g, "...").trim();
  if (!s || s.length > max || !/^[\x20-\x7E]+$/.test(s)) return null;
  return s;
}

// A stored signal as the units see it, or null once it has expired.
function liveSignal(sig, now) {
  if (!sig || typeof sig.at !== "number") return null;
  const left = Math.floor((sig.at + SIGNAL_TTL_S * 1000 - now) / 1000);
  return left > 0 ? { from: sig.from, msg: sig.msg, at: sig.at, left } : null;
}

// The club's date ("2026-11-15") at `now`, for the one-signal-a-day rule.
export function clubDay(now) {
  return new Intl.DateTimeFormat("en-CA", { timeZone: TIME_ZONE }).format(new Date(now));
}

// A unit's report from the /poll query, or null if it sent none (firmware
// before 1.5.25, or the web page).
export function readStatus(params, now) {
  if (!params.has("fw")) return null;
  const field = (k) => cleanMessage(params.get(k) || "", MAX_FIELD) || "";
  const upMin = Math.max(0, Math.floor(Number(params.get("up")) || 0));
  return {
    fw: field("fw"),
    fwst: field("fwst"),
    why: field("why"),
    bad: params.get("bad") === "1",
    mem: field("mem"),
    panel: field("panel"),
    started: now - upMin * 60_000,
    seen: now,
  };
}

// Whether a fresh report needs writing over the stored one.
export function statusChanged(old, s) {
  if (!old) return true;
  for (const k of ["fw", "fwst", "why", "bad", "panel"]) if (old[k] !== s[k]) return true;
  if (Math.abs((old.started || 0) - s.started) > 5 * 60_000) return true;  // it restarted
  return s.seen - (old.seen || 0) >= STATUS_REFRESH_MS;
}

function page() {
  return new Response(PAGE, {
    headers: {
      "content-type": "text/html; charset=utf-8",
      "cache-control": "no-cache",
      "content-security-policy":
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; " +
        "connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
      "referrer-policy": "no-referrer",
      "x-content-type-options": "nosniff",
    },
  });
}

export async function handle(request, env, now) {
  const url = new URL(request.url);
  if (request.method === "GET" && (url.pathname === "/" || url.pathname === "/index.html")) return page();
  const me = caller(request, env);
  if (!me) return json(401, { error: "unknown or missing unit token" });
  const all = units(env);

  if (request.method === "GET" && url.pathname === "/roster") {
    return json(200, { me, friends: Object.keys(all).filter((n) => n !== me) });
  }

  const takeInbox = async () => {
    const key = `inbox:${me}`;
    const nudges = (await env.NUDGES.get(key, "json")) || [];
    if (nudges.length) await env.NUDGES.delete(key);  // only write when there was mail
    return nudges;
  };
  const liveSignals = async () => {
    const signals = [];
    for (const name of Object.keys(all)) {
      const sig = liveSignal(await env.NUDGES.get(`signal:${name}`, "json"), now);
      if (sig) signals.push(sig);
    }
    return signals.sort((a, b) => b.at - a.at);
  };

  if (request.method === "GET" && url.pathname === "/inbox") {
    return json(200, { nudges: await takeInbox() });
  }

  if (request.method === "GET" && url.pathname === "/poll") {
    const key = `inbox:${me}`;
    const ack = Number(url.searchParams.get("ack")) || 0;
    const inbox = (await env.NUDGES.get(key, "json")) || [];
    const nudges = inbox.filter((n) => !(n.at <= ack));
    const status = readStatus(url.searchParams, now);
    if (status) {
      const skey = `status:${me}`;
      if (statusChanged(await env.NUDGES.get(skey, "json"), status)) {
        await env.NUDGES.put(skey, JSON.stringify(status), { expirationTtl: STATUS_TTL_S });
      }
    }
    if (nudges.length !== inbox.length) {  // only write when the unit took some
      if (nudges.length) await env.NUDGES.put(key, JSON.stringify(nudges));
      else await env.NUDGES.delete(key);
    }
    return json(200, {
      me,
      friends: Object.keys(all).filter((n) => n !== me),
      nudges,
      signals: await liveSignals(),
    });
  }

  if (request.method === "POST" && url.pathname === "/nudge") {
    let body;
    try {
      body = await request.json();
    } catch {
      return json(400, { error: "body must be JSON" });
    }
    const msg = cleanMessage(body && body.msg);
    if (!msg) return json(400, { error: `msg must be 1-${MAX_MSG} plain characters` });
    const to = body.to === "*" ? Object.keys(all).filter((n) => n !== me) : [body.to];
    if (!to.length || to.some((n) => typeof n !== "string" || !(n in all) || n === me)) {
      return json(404, { error: "no such friend" });
    }

    // Rate limit: a short gap between nudges and a cap per hour.
    const rateKey = `sent:${me}`;
    const sent = ((await env.NUDGES.get(rateKey, "json")) || []).filter((t) => now - t < 3_600_000);
    if (sent.length && now - sent[sent.length - 1] < MIN_GAP_MS) {
      return json(429, { error: "easy, tiger: wait a few seconds" });
    }
    if (sent.length >= MAX_PER_HOUR) return json(429, { error: "that's enough nudging for this hour" });

    const nudge = { from: me, msg, at: now };
    if (body.re === true) nudge.re = true;
    const inboxes = body.to === "*" ? [...to, me] : to;
    for (const name of inboxes) {
      const key = `inbox:${name}`;
      const inbox = (await env.NUDGES.get(key, "json")) || [];
      inbox.push(nudge);
      await env.NUDGES.put(key, JSON.stringify(inbox.slice(-MAX_INBOX)));
    }
    sent.push(now);
    await env.NUDGES.put(rateKey, JSON.stringify(sent), { expirationTtl: 3600 });
    return json(200, { sent: to });
  }

  if (request.method === "GET" && url.pathname === "/fleet") {
    const list = [];
    for (const name of Object.keys(all)) {
      const s = (await env.NUDGES.get(`status:${name}`, "json")) || {};
      list.push({
        name,
        fw: s.fw || "",
        fwst: s.fwst || "",
        why: s.why || "",
        bad: !!s.bad,
        mem: s.mem || "",
        panel: s.panel || "",
        seen: s.seen || 0,
        started: s.started || 0,
      });
    }
    return json(200, { units: list, now });
  }

  if (request.method === "GET" && url.pathname === "/signals") {
    return json(200, { signals: await liveSignals() });
  }

  if (request.method === "POST" && url.pathname === "/signal") {
    let body;
    try {
      body = await request.json();
    } catch {
      return json(400, { error: "body must be JSON" });
    }
    const msg = cleanMessage(body && body.msg, MAX_SIGNAL);
    if (!msg) return json(400, { error: `msg must be 1-${MAX_SIGNAL} plain characters` });

    // One a day: the club's date of this unit's last signal.
    const rateKey = `signaled:${me}`;
    const today = clubDay(now);
    if ((await env.NUDGES.get(rateKey)) === today) {
      return json(429, { error: "one signal a day: back tomorrow" });
    }

    const sig = { from: me, msg, at: now };
    await env.NUDGES.put(`signal:${me}`, JSON.stringify(sig), { expirationTtl: SIGNAL_TTL_S });
    await env.NUDGES.put(rateKey, today, { expirationTtl: 2 * SIGNAL_TTL_S });
    return json(200, { signal: liveSignal(sig, now) });
  }

  return json(404, { error: "not found" });
}
