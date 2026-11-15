// Cloud backups for Pocket Squatch / Pocket Pony boards: a Cloudflare Worker with one KV
// namespace (SAVES). A board's backup code is its only key: the Worker stores each save
// under a hash of the code, so nothing in KV says which board or pony it belongs to, and
// the code itself never lands anywhere but on the board and wherever its owner wrote it.
//
//   PUT /v1/save/<CODE>   body: the save (a few hundred bytes). Keeps it as the latest,
//                         and as that day's copy for a week.
//   GET /v1/save/<CODE>   the latest save;  ?day=YYYY-MM-DD  that day's, if still kept.
//
//   GET  /ping            a small page: type a lost board's code and press Ping.
//   POST /v1/ping/<CODE>  ask that board to call out (kept two hours); DELETE takes it back.
//   GET  /v1/ping/<CODE>  the board, about hourly: 200 "ring" (and the ping is used up) or 204.
//
// <CODE> is 16 characters of Crockford base32 (0-9, A-Z without I, L, O, U): 80 random bits.

const CODE = /^\/v1\/save\/([0-9A-HJKMNP-TV-Z]{16})$/;
const PING = /^\/v1\/ping\/([0-9A-HJKMNP-TV-Z]{16})$/;
const PING_TTL = 2 * 60 * 60;
const MAX_BYTES = 8192;
const WEEK = 8 * 24 * 60 * 60;

async function keyFor(code) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode("pocket-squatch:" + code));
  return "save:" + [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

async function ping(req, code, env) {
  const key = "ping:" + (await keyFor(code)).slice(5);
  if (req.method === "POST") {
    await env.SAVES.put(key, String(Date.now()), { expirationTtl: PING_TTL });
    return new Response("pinged\n", { headers: { "cache-control": "no-store" } });
  }
  if (req.method === "DELETE") {
    await env.SAVES.delete(key);
    return new Response("cancelled\n", { headers: { "cache-control": "no-store" } });
  }
  if (req.method === "GET") {
    if (!(await env.SAVES.get(key))) return new Response(null, { status: 204, headers: { "cache-control": "no-store" } });
    await env.SAVES.delete(key);
    return new Response("ring\n", { headers: { "cache-control": "no-store" } });
  }
  return new Response("method not allowed\n", { status: 405 });
}

const PAGE = `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Find my pet</title>
<style>
body{font-family:system-ui,sans-serif;background:#f6efe2;color:#3b2a1e;margin:0;padding:24px;display:flex;justify-content:center}
main{max-width:420px;width:100%}h1{font-size:28px;margin:0 0 8px}p{line-height:1.45}
input{width:100%;box-sizing:border-box;font:600 22px ui-monospace,monospace;letter-spacing:2px;padding:14px;border:2px solid #8a6a4a;border-radius:12px;text-transform:uppercase}
button{width:100%;min-height:64px;margin-top:14px;font:700 22px system-ui,sans-serif;border:0;border-radius:14px;background:#c8902e;color:#fff;cursor:pointer}
button.quiet{background:#e8dcc6;color:#3b2a1e;font-size:18px}#msg{min-height:3em;font-weight:600}
</style></head><body><main>
<h1>Find my pet</h1>
<p>Lost your Pocket Pony or Pocket Squatch somewhere at home? Type its backup code (on the toy under
Settings &gt; Wi-Fi, and on the setup page) and press Ping.</p>
<p>The toy checks in about once an hour over your home Wi-Fi. When it hears the ping it lights up and calls
out every few seconds for five minutes, or until someone touches it.</p>
<input id="code" placeholder="XXXX-XXXX-XXXX-XXXX" autocomplete="off" spellcheck="false" maxlength="24">
<button id="go">Ping</button><button id="stop" class="quiet">Cancel the ping</button>
<p id="msg" role="status"></p>
<script>
const tidy=t=>{t=t.toUpperCase().replace(/[\\s-]/g,"").replace(/O/g,"0").replace(/[IL]/g,"1");return /^[0-9A-HJKMNP-TV-Z]{16}$/.test(t)?t:null};
const msg=document.getElementById("msg");
async function send(method){const c=tidy(document.getElementById("code").value);
 if(!c){msg.textContent="That doesn't look like a backup code: 16 letters and numbers.";return}
 try{const r=await fetch("/v1/ping/"+c,{method});if(!r.ok)throw 0;
  msg.textContent=method==="POST"?"Pinged! Within the hour it should start calling out. Listen for it.":"Ping cancelled."}
 catch(e){msg.textContent="Couldn't send that. Try again in a moment."}}
document.getElementById("go").onclick=()=>send("POST");document.getElementById("stop").onclick=()=>send("DELETE");
</script></main></body></html>`;

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    if (url.pathname === "/ping" || url.pathname === "/ping/")
      return new Response(PAGE, { headers: { "content-type": "text/html; charset=utf-8" } });
    const p = url.pathname.match(PING);
    if (p) return ping(req, p[1], env);
    const m = url.pathname.match(CODE);
    if (!m) return new Response("not found\n", { status: 404 });
    const key = await keyFor(m[1]);

    if (req.method === "PUT") {
      const body = await req.arrayBuffer();
      if (body.byteLength < 16 || body.byteLength > MAX_BYTES) return new Response("bad size\n", { status: 413 });
      const day = new Date().toISOString().slice(0, 10);
      await env.SAVES.put(key, body, { metadata: { at: Date.now(), bytes: body.byteLength } });
      await env.SAVES.put(`${key}:${day}`, body, { expirationTtl: WEEK });
      return new Response("saved\n");
    }

    if (req.method === "GET") {
      const day = url.searchParams.get("day");
      if (day && !/^\d{4}-\d{2}-\d{2}$/.test(day)) return new Response("bad day\n", { status: 400 });
      const save = await env.SAVES.get(day ? `${key}:${day}` : key, "arrayBuffer");
      if (!save) return new Response("no backup\n", { status: 404 });
      return new Response(save, { headers: { "content-type": "application/octet-stream", "cache-control": "no-store" } });
    }

    return new Response("method not allowed\n", { status: 405 });
  },
};
