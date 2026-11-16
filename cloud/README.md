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

## Find a lost toy (ping)

Open **find.bluebeetle.games** (or `https://pocket-pony-saves.bluebeetlegames.workers.dev/ping`) on any phone or computer,
type the toy's backup code and press **Ping**. The toy checks about once an hour over the
home Wi-Fi (while it fetches the weather, or on its own if it has no town). When it hears the
ping it wakes, turns the screen to full brightness and calls out loudly every few seconds,
even if its sound is off, for five minutes or until someone touches it. A ping waits two
hours, then lapses; **Cancel the ping** takes it back. It only works where the toy can reach
its home Wi-Fi and has some battery left.

**No code?** On the same page, with the phone on the home Wi-Fi (mobile data off), press
**Ping toys on my Wi-Fi**. Each toy's hourly check also notes which home connection it came
from (a hash of the address, kept three hours), and this button pings every toy seen from
the phone's connection. Most phones reach the Worker over IPv6 while the toy uses IPv4, so
the page asks api.ipify.org for the phone's IPv4 address and sends that too; the Worker uses
it only when the phone came over IPv6. Someone who knew a home's address could make its toy
call out for a while, nothing more, and the answer never says whether any toy was found.

## Set it up (once)

You need a Cloudflare account (the free plan is plenty) and Node.js.

```
cd cloud
npx wrangler login
npx wrangler kv namespace create SAVES
```

Copy the `id` it prints into `wrangler.toml` (as `id = "..."`), then

```
npx wrangler deploy
```

It prints the Worker's address, like `https://pocket-pony-saves.<you>.workers.dev`. Put
that, with `/v1/save/` on the end, in `firmware/sdkconfig.defaults` as

```
CONFIG_SQUATCH_CLOUD_URL="https://pocket-pony-saves.<you>.workers.dev/v1/save/"
```

so every build has it, including the ones the boards update to by themselves. (Ours is
`https://pocket-pony-saves.bluebeetlegames.workers.dev/v1/save/`, already set.)

Check it: `curl -X PUT --data-binary @somefile https://.../v1/save/0123456789ABCDEF` says `saved`,
and a GET of the same address gives the file back.
