// Cloud backups for Pocket Squatch / Pocket Pony boards: a Cloudflare Worker with one KV
// namespace (SAVES). A board's backup code is its only key: the Worker stores each save
// under a hash of the code, so nothing in KV says which board or pony it belongs to, and
// the code itself never lands anywhere but on the board and wherever its owner wrote it.
//
//   PUT /v1/save/<CODE>   body: the save (a few hundred bytes). Keeps it as the latest,
//                         and as that day's copy for a week.
//   GET /v1/save/<CODE>   the latest save;  ?day=YYYY-MM-DD  that day's, if still kept.
//
// <CODE> is 16 characters of Crockford base32 (0-9, A-Z without I, L, O, U): 80 random bits.

const CODE = /^\/v1\/save\/([0-9A-HJKMNP-TV-Z]{16})$/;
const MAX_BYTES = 8192;
const WEEK = 8 * 24 * 60 * 60;

async function keyFor(code) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode("pocket-squatch:" + code));
  return "save:" + [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
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
