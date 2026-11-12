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
//   POST /nudge    {"to": "Dave" | "*", "msg": "..."} -> {"sent": ["Dave"]}

export const MAX_MSG = 40;           // characters per nudge
export const MAX_INBOX = 20;         // oldest dropped beyond this
export const MIN_GAP_MS = 15_000;    // between one unit's nudges
export const MAX_PER_HOUR = 12;      // per sending unit

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
export function cleanMessage(msg) {
  if (typeof msg !== "string") return null;
  const s = msg.replace(/[‘’]/g, "'").replace(/[“”]/g, '"').trim();
  if (!s || s.length > MAX_MSG || !/^[\x20-\x7E]+$/.test(s)) return null;
  return s;
}

export async function handle(request, env, now) {
  const url = new URL(request.url);
  const me = caller(request, env);
  if (!me) return json(401, { error: "unknown or missing unit token" });
  const all = units(env);

  if (request.method === "GET" && url.pathname === "/roster") {
    return json(200, { me, friends: Object.keys(all).filter((n) => n !== me) });
  }

  if (request.method === "GET" && url.pathname === "/inbox") {
    const key = `inbox:${me}`;
    const nudges = (await env.NUDGES.get(key, "json")) || [];
    if (nudges.length) await env.NUDGES.delete(key);  // only write when there was mail
    return json(200, { nudges });
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
    for (const name of to) {
      const key = `inbox:${name}`;
      const inbox = (await env.NUDGES.get(key, "json")) || [];
      inbox.push(nudge);
      await env.NUDGES.put(key, JSON.stringify(inbox.slice(-MAX_INBOX)));
    }
    sent.push(now);
    await env.NUDGES.put(rateKey, JSON.stringify(sent), { expirationTtl: 3600 });
    return json(200, { sent: to });
  }

  return json(404, { error: "not found" });
}
