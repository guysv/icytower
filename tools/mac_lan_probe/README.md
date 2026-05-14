# mac_lan_probe — UDP bind/broadcast/multicast sanity checks (no icytower).

Build:

```sh
make -C tools/mac_lan_probe
./tools/mac_lan_probe/mac_lan_probe all
```

Modes: `s1` … `s4`, `s5` (skip), or `all`.

The probe uses port **58123** (not icytower’s discovery default) so it does not fight a running game.

Outcomes (Darwin reference):

- **S1** — Ephemeral listener misses datagrams addressed to a fixed discovery port; fixed-port broadcast must hit a matching bind.
- **S2** — Second `bind()` on the same UDP port with `SO_REUSEADDR` fails (one listener per port for unicast-style use).
- **S3** — Pinned discovery socket + separate ephemeral “game” bind; broadcast to discovery port is received.
- **S4** — IPv4 ASM multicast to `239.43.137.251` on that port works on loopback when the listener joins the group.

icytower Phase 1 LAN uses **dual sockets** (discovery + game) plus **multicast + broadcast** for `SESSION_ADVERT` (see `lan/lan_net.c`).
