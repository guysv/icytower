#ifndef LAN_LAN_NET_H
#define LAN_LAN_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct LanAddr {
	uint32_t ip;   /* host order */
	uint16_t port; /* host order */
} LanAddr;

bool lan_addr_equal(LanAddr a, LanAddr b);
void lan_addr_to_string(LanAddr a, char *out, size_t cap);
void lan_addr_rewrite_loopback_for_local_hosts(LanAddr *a);

/*
 * Dedicated discovery UDP socket: binds discovery_port, joins ASM multicast group,
 * SO_BROADCAST for subnet-wide fallback.
 */
bool lan_net_open_discovery(uint16_t bind_port_want,
		int *out_fd, uint16_t *out_actual_port);

bool lan_net_open_game(uint16_t bind_port_want, int *out_fd,
		uint16_t *out_actual_port); /* bind_port_want==0 ⇒ ephemeral */

void lan_net_close(int fd);

int lan_net_recv(int fd, uint8_t *buf, size_t buf_cap,
		LanAddr *from);

bool lan_net_send(int fd, const uint8_t *buf, size_t len,
		LanAddr to);

LanAddr lan_addr_broadcast(uint16_t port);

LanAddr lan_addr_multicast_peer(uint16_t port);

#endif
