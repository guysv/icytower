/*
 * Optional mDNS/Bonjour (DNS-SD) alongside UDP multicast discovery.
 * Implemented on macOS via dnssd; stubbed elsewhere.
 */

#ifndef LAN_LAN_MDNS_H
#define LAN_LAN_MDNS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * +add: instance + resolved fields valid.
 * !add: only instance (service name from browse); remove matching row.
 */
typedef void (*lan_mdns_browse_cb)(void *ctx, bool add,
		const char *instance, uint64_t sid, const char *room,
		uint16_t game_port, uint32_t host_ipv4, uint16_t discovery_port);

bool lan_mdns_browse_start(void *ctx, lan_mdns_browse_cb cb);
void lan_mdns_browse_stop(void);

bool lan_mdns_register(const char *room, uint64_t sid, uint16_t game_port,
		uint16_t discovery_port);
void lan_mdns_unregister(void);

void lan_mdns_poll(void);

#endif /* LAN_LAN_MDNS_H */
