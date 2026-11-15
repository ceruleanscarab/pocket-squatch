# Cloud backups

Each board backs its pet up once a night (about 2 am) to a small Cloudflare Worker, and
right after its Wi-Fi is set up. If the board is reset, broken or replaced, the pet comes
back from its **backup code**, typed into the Wi-Fi setup page on a phone.

- The code (like `K7QM-2XHT-9BFA-WP4D`) is shown on the board under Settings > Wi-Fi and at
  the top of the Wi-Fi setup page. Write it down or take a photo of it: it's the only key.
- The Worker keeps the latest backup and one a day for the last week. It stores each under
  a hash of the code, so its storage says nothing about whose pet it is.
- A restored pet comes back as if from the boarding stable: fed, clean, and no older for
  the time the board was away.

## Set it up (once)

You need a Cloudflare account (the free plan is plenty) and Node.js.

```
cd cloud
npx wrangler login
npx wrangler kv namespace create SAVES
```

Copy the `id` it prints into `wrangler.toml` (replacing `PASTE-THE-KV-NAMESPACE-ID-HERE`), then

```
npx wrangler deploy
```

It prints the Worker's address, like `https://pocket-squatch-saves.<you>.workers.dev`. Put
that, with `/v1/save/` on the end, in `firmware/sdkconfig.defaults` as

```
CONFIG_SQUATCH_CLOUD_URL="https://pocket-squatch-saves.<you>.workers.dev/v1/save/"
```

so every build has it, including the ones the boards update to by themselves.

Check it: `curl -X PUT --data-binary @somefile https://.../v1/save/0123456789ABCDEF` says `saved`,
and a GET of the same address gives the file back.
