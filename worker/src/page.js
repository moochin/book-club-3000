// The hub's web page: send a nudge or broadcast a signal from a phone, in
// your own words, with your unit's token. Served at GET / with no token; it
// then calls the same API the units use, so the token decides everything.
//
// It never reads /inbox: that would take nudges away from the unit.
// Everything shown from the hub goes through textContent, never innerHTML.

import { MASCOT } from "./mascot.js";

export const PAGE = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<title>Book-Club 3000 Transmitter</title>
<style>
  /* The units' Wi-Fi setup page look (firmware/src/portal_theme.h), green theme. */
  :root {
    --a: #3CFF6E; --d: #24A048; --f: #10401E; --bg: #030C05; --g: rgba(60,255,110,.45);
    --warn: #FFB028; --bad: #FF5030;
    color-scheme: dark;
  }
  * { box-sizing: border-box; }
  html { background: var(--bg); }
  body {
    margin: 0; min-height: 100vh; color: var(--a);
    background: var(--bg) repeating-linear-gradient(0deg, rgba(0,0,0,.28) 0 1px, transparent 1px 3px);
    font: 16px/1.45 ui-monospace, SFMono-Regular, Menlo, Consolas, "Liberation Mono", "Courier New", monospace;
    text-shadow: 0 0 5px var(--g);
  }
  main { max-width: 460px; margin: 0 auto; padding: 12px 16px 40px; }
  .bc-logo {
    display: block; margin: 4px auto 0; width: 96px; height: 144px;
    image-rendering: pixelated; image-rendering: crisp-edges; filter: drop-shadow(0 0 6px var(--g));
  }
  h1 {
    font-size: 1.55rem; letter-spacing: .14em; text-transform: uppercase; margin: 6px 0 0;
    text-align: center; font-weight: bold; text-shadow: 0 0 10px var(--g);
  }
  h1::after { content: "_"; animation: bcb 1s steps(1) infinite; }
  @keyframes bcb { 50% { opacity: 0; } }
  @media (prefers-reduced-motion: reduce) { h1::after { animation: none; } }
  .sub {
    font-size: .8rem; letter-spacing: .2em; text-transform: uppercase; color: var(--d);
    text-align: center; margin: 4px 0 18px;
  }
  .bc-sec {
    margin: 22px 0 10px; padding-bottom: 3px; border-bottom: 1px solid var(--d);
    letter-spacing: .22em; font-size: .95rem; font-weight: normal; text-transform: uppercase;
  }
  .bc-sec::before { content: "> "; }
  .sec-row {
    display: flex; align-items: center; gap: 12px; margin: 22px 0 12px; padding-bottom: 4px;
    border-bottom: 1px solid var(--d);
  }
  .sec-row .bc-sec { flex: 1; margin: 0; padding: 0; border: 0; }
  .bc-box { border: 1px solid var(--d); padding: 10px 12px; line-height: 1.45; font-size: .92rem; }
  label {
    display: block; color: var(--d); font-size: .78rem; letter-spacing: .12em;
    text-transform: uppercase; margin: 12px 0 4px;
  }
  input[type=password], input[type=text], select, textarea {
    width: 100%; background: #000; color: var(--a); border: 1px solid var(--d); border-radius: 0;
    font: inherit; padding: 9px; text-shadow: inherit;
  }
  textarea { resize: vertical; min-height: 4.8em; }
  input::placeholder, textarea::placeholder { color: var(--d); opacity: 1; }
  input:focus, select:focus, textarea:focus { outline: none; border-color: var(--a); box-shadow: 0 0 8px var(--g); }
  .check { display: flex; gap: 8px; align-items: center; margin-top: 10px; }
  .check input { width: 18px; height: 18px; accent-color: var(--a); margin: 0; }
  .check label { margin: 0; }
  button {
    width: 100%; margin-top: 18px; background: transparent; color: var(--a); border: 2px solid var(--a);
    border-radius: 0; font: inherit; font-size: 1rem; letter-spacing: .14em; text-transform: uppercase;
    line-height: 2.6rem; cursor: pointer; text-shadow: 0 0 5px var(--g);
    box-shadow: 0 0 8px var(--g), inset 0 0 8px var(--g);
  }
  button:active { background: var(--a); color: var(--bg); }
  @media (hover: hover) { button:hover { background: var(--a); color: var(--bg); } }
  button:disabled { border-color: var(--f); color: var(--d); box-shadow: none; background: transparent; cursor: default; }
  button.small {
    width: auto; margin: 0; padding: 0 12px; line-height: 2rem; font-size: .8rem;
    border-width: 1px; border-color: var(--d); color: var(--d); box-shadow: none;
  }
  button.small:active { background: var(--d); color: var(--bg); }
  @media (hover: hover) { button.small:hover { background: var(--d); color: var(--bg); } }
  .row { display: flex; justify-content: space-between; align-items: center; gap: 12px; flex-wrap: wrap; }
  .who { letter-spacing: .14em; text-transform: uppercase; text-shadow: 0 0 8px var(--g); }
  .tabs { display: flex; margin-top: 4px; }
  .tabs button {
    flex: 1; margin: 0; border-width: 1px; border-color: var(--d); color: var(--d); box-shadow: none; line-height: 2.4rem;
  }
  .tabs button + button { border-left: 0; }
  .tabs button[aria-selected=true] { background: var(--a); border-color: var(--a); color: var(--bg); }
  .count { text-align: right; font-size: .75rem; color: var(--d); margin-top: 4px; }
  .count.over, .warn { color: var(--warn); }
  .hint { color: var(--d); font-size: .75rem; line-height: 1.4; margin: 8px 0 0; }
  #status { min-height: 1.4em; margin: 12px 0 0; }
  #status.bad { color: var(--warn); }
  ul { list-style: none; margin: 0; padding: 0; }
  li { border: 1px solid var(--d); border-left: 6px solid var(--a); background: #000; padding: 10px 12px; margin-bottom: 10px; }
  .quote { overflow-wrap: anywhere; }
  .meta { display: flex; justify-content: space-between; font-size: .75rem; color: var(--d); margin-top: 6px; letter-spacing: .08em; text-transform: uppercase; }
  .bar { display: flex; gap: 2px; margin-top: 6px; }
  .bar > i { flex: 1; height: 6px; background: var(--f); }
  .bar > i.on { background: var(--a); box-shadow: 0 0 4px var(--g); }
  li.empty, li.warn { border-left-color: var(--d); color: var(--d); }
  li.warn { border-left-color: var(--warn); color: var(--warn); }
  [hidden] { display: none !important; }
</style>
</head>
<body>
<main>
  <img class="bc-logo" src="${MASCOT}" alt="">
  <h1>Book-Club 3000</h1>
  <p class="sub">Field transmitter</p>

  <section id="signin">
    <h2 class="bc-sec">Sign in</h2>
    <form id="signinForm">
      <label for="token">Your nudge token</label>
      <input id="token" type="password" autocomplete="current-password" autocapitalize="off" spellcheck="false" required>
      <div class="check"><input id="remember" type="checkbox" checked><label for="remember">Remember on this phone</label></div>
      <button type="submit">Connect</button>
    </form>
    <p class="hint">The same token you entered on your terminal's setup page. Ask whoever runs the hub if you don't have one.</p>
  </section>

  <div id="app" hidden>
    <div class="bc-box row">
      <div>Operative <span class="who" id="me"></span></div>
      <button class="small" id="signout" type="button">Sign out</button>
    </div>

    <section>
      <h2 class="bc-sec">Transmit</h2>
      <div class="tabs" role="tablist">
        <button type="button" role="tab" id="tabNudge" aria-selected="true">NUDGE</button>
        <button type="button" role="tab" id="tabSignal" aria-selected="false">SIGNAL</button>
      </div>

      <form id="nudgeForm">
        <label for="to">To</label>
        <select id="to"></select>
        <label for="nudgeMsg">Message</label>
        <input id="nudgeMsg" type="text" maxlength="40" autocomplete="off" placeholder="GET YOUR ASS ONLINE" required>
        <div class="count" id="nudgeCount">0/40</div>
        <button type="submit">Send nudge</button>
        <p class="hint">Pops up on their terminal within 5 minutes.</p>
      </form>

      <form id="signalForm" hidden>
        <label for="signalMsg">Your quote, for everyone</label>
        <textarea id="signalMsg" maxlength="100" rows="3" placeholder="Look on my works, ye Mighty, and despair! - Shelley" required></textarea>
        <div class="count" id="signalCount">0/100</div>
        <button type="submit">Broadcast</button>
        <p class="hint">One a day each, from here or your terminal (resets at midnight). It's on every terminal for 24 hours, then gone.</p>
      </form>
      <p id="status" role="status" aria-live="polite"></p>
    </section>

    <section>
      <div class="sec-row">
        <h2 class="bc-sec">On the air</h2>
        <button class="small" id="refresh" type="button">Scan</button>
      </div>
      <ul id="signals"></ul>
    </section>
  </div>
</main>
<script>
(() => {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const KEY = "bc3000-token";
  const DAY = 86400;
  let token = "";
  let me = "";

  const store = {
    get() { try { return localStorage.getItem(KEY) || ""; } catch { return ""; } },
    set(v) { try { localStorage.setItem(KEY, v); } catch {} },
    clear() { try { localStorage.removeItem(KEY); } catch {} },
  };

  // The units' font is plain ASCII: straighten what phones type, flag the rest.
  function clean(s) {
    return s.replace(/[\\u2018\\u2019]/g, "'").replace(/[\\u201C\\u201D]/g, '"')
            .replace(/\\u2026/g, "...").replace(/[\\u2013\\u2014]/g, "-").replace(/\\s+/g, " ").trim();
  }
  const plain = (s) => /^[\\x20-\\x7E]*$/.test(s);

  async function api(method, path, body) {
    const init = { method, headers: { authorization: "Bearer " + token } };
    if (body) {
      init.headers["content-type"] = "application/json";
      init.body = JSON.stringify(body);
    }
    let r;
    try {
      r = await fetch(path, init);
    } catch {
      throw new Error("can't reach the hub");
    }
    let data = {};
    try { data = await r.json(); } catch {}
    if (!r.ok) {
      const e = new Error(data.error || "hub said " + r.status);
      e.status = r.status;
      throw e;
    }
    return data;
  }

  function status(text, ok) {
    const s = $("status");
    s.textContent = text;
    s.className = text ? (ok ? "ok" : "bad") : "";
  }

  function leftText(secs) {
    if (secs >= 3600) return Math.floor(secs / 3600) + "h left";
    return Math.max(1, Math.ceil(secs / 60)) + "m left";
  }

  async function loadSignals() {
    const list = $("signals");
    try {
      const { signals } = await api("GET", "/signals");
      list.replaceChildren();
      if (!signals.length) {
        const li = document.createElement("li");
        li.className = "empty";
        li.textContent = "The airwaves are silent. Be the first.";
        list.append(li);
        return;
      }
      for (const s of signals) {
        const li = document.createElement("li");
        const q = document.createElement("div");
        q.className = "quote";
        q.textContent = s.msg;
        const meta = document.createElement("div");
        meta.className = "meta";
        const from = document.createElement("span");
        from.textContent = "- " + (s.from === me ? "you" : s.from);
        const left = document.createElement("span");
        left.textContent = leftText(s.left);
        meta.append(from, left);
        const bar = document.createElement("div");  // 20 segments, as on the units
        bar.className = "bar";
        const lit = Math.ceil((Math.min(s.left, DAY) / DAY) * 20);
        for (let i = 0; i < 20; i++) {
          const seg = document.createElement("i");
          if (i < lit) seg.className = "on";
          bar.append(seg);
        }
        li.append(q, meta, bar);
        list.append(li);
      }
    } catch (e) {
      list.replaceChildren();
      const li = document.createElement("li");
      li.className = "warn";
      li.textContent = "No signal: " + e.message;
      list.append(li);
    }
  }

  async function signIn(t, remember) {
    token = t.trim();
    if (!token) return false;
    let roster;
    try {
      roster = await api("GET", "/roster");
    } catch (e) {
      token = "";
      if (e.status === 401) store.clear();
      return e.message;
    }
    me = roster.me;
    if (remember) store.set(token); else store.clear();
    $("me").textContent = me;
    const to = $("to");
    to.replaceChildren(new Option("Everyone", "*"));
    for (const f of roster.friends) to.append(new Option(f, f));
    $("signin").hidden = true;
    $("app").hidden = false;
    loadSignals();
    return true;
  }

  function counter(input, out, max) {
    const update = () => {
      const s = clean(input.value);
      const bad = !plain(s);
      out.textContent = bad ? "plain letters, numbers and punctuation only (no emoji)" : s.length + "/" + max;
      out.classList.toggle("over", bad || s.length > max);
    };
    input.addEventListener("input", update);
    update();
  }
  counter($("nudgeMsg"), $("nudgeCount"), 40);
  counter($("signalMsg"), $("signalCount"), 100);

  function tab(signal) {
    $("tabNudge").setAttribute("aria-selected", String(!signal));
    $("tabSignal").setAttribute("aria-selected", String(signal));
    $("nudgeForm").hidden = signal;
    $("signalForm").hidden = !signal;
    status("");
  }
  $("tabNudge").onclick = () => tab(false);
  $("tabSignal").onclick = () => tab(true);

  async function submit(form, input, send) {
    const msg = clean(input.value);
    if (!msg) return;
    if (!plain(msg)) return status("Units can only show plain letters, numbers and punctuation.", false);
    const btn = form.querySelector("button[type=submit]");
    btn.disabled = true;
    status("Transmitting...", true);
    try {
      status(await send(msg), true);
      input.value = "";
      input.dispatchEvent(new Event("input"));
    } catch (e) {
      status("Not sent: " + e.message, false);
    } finally {
      btn.disabled = false;
    }
  }

  $("nudgeForm").addEventListener("submit", (ev) => {
    ev.preventDefault();
    const to = $("to").value;
    submit(ev.target, $("nudgeMsg"), async (msg) => {
      const r = await api("POST", "/nudge", { to, msg });
      return "Nudge sent to " + (to === "*" ? "everyone" : r.sent.join(", ")) + ".";
    });
  });

  $("signalForm").addEventListener("submit", (ev) => {
    ev.preventDefault();
    submit(ev.target, $("signalMsg"), async (msg) => {
      await api("POST", "/signal", { msg });
      loadSignals();
      return "On the air for 24 hours.";
    });
  });

  $("signinForm").addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const btn = ev.target.querySelector("button");
    btn.disabled = true;
    const ok = await signIn($("token").value, $("remember").checked);
    btn.disabled = false;
    if (ok !== true) {
      const p = ev.target.querySelector(".warn") || document.createElement("p");
      p.className = "hint warn";
      p.textContent = ok === "unknown or missing unit token" ? "That token isn't on the hub." : String(ok);
      ev.target.append(p);
    } else {
      $("token").value = "";
    }
  });

  $("signout").onclick = () => {
    token = "";
    me = "";
    store.clear();
    $("app").hidden = true;
    $("signin").hidden = false;
  };
  $("refresh").onclick = loadSignals;

  // A link can carry the token after "#t=" (never sent to the server); it is
  // removed from the address bar straight away.
  const m = /[#&]t=([^&]+)/.exec(location.hash);
  if (m) history.replaceState(null, "", location.pathname);
  const start = m ? decodeURIComponent(m[1]) : store.get();
  if (start) signIn(start, true);
})();
</script>
</body>
</html>
`;
