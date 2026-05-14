/*
 * Connectivity transport for LAN Phase 1.
 *
 * Session adverts use IPv4 ASM to LAN_MULTICAST_GROUP plus subnet broadcast while
 * the discovery receiver joins the ASM group on the discovery port — see
 * tools/mac_lan_probe/ for rationale (ephemeral binds miss fixed-port adverts).
 */

#include "lan_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "lan_internal.h"

bool lan_addr_equal(LanAddr a, LanAddr b)
{
	return a.ip == b.ip && a.port == b.port;
}

void lan_addr_rewrite_loopback_for_local_hosts(LanAddr *a)
{
	struct ifaddrs *ifa;
	struct ifaddrs *head;
	uint32_t lb;

	if (!a || a->ip == 0u || a->ip == 0xFFFFFFFFu)
		return;
	lb = (127u << 24) | 1u;
	if (a->ip == lb)
		return;
	if (getifaddrs(&head) != 0)
		return;
	for (ifa = head; ifa != NULL; ifa = ifa->ifa_next) {
		uint32_t ip;

		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		ip = ntohl(((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr);
		if (ip == a->ip) {
			a->ip = lb;
			break;
		}
	}
	freeifaddrs(head);
}

static int udp_socket_base(bool want_broadcast)
{
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int yes = 1;
	int flags;

	if (fd < 0)
		return -1;
	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		close(fd);
		return -1;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
		close(fd);
		return -1;
	}
	if (want_broadcast
			&& setsockopt(fd, SOL_SOCKET, SO_BROADCAST,
					&yes, sizeof yes) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

/*
 * macOS/BSD: SO_REUSEADDR alone does not let two processes bind the same UDP port;
 * discovery must use SO_REUSEPORT so LAN PARTY can run twice on one machine.
 * Linux 3.9+ supports SO_REUSEPORT similarly.
 */
static void sock_try_reuseport(int fd)
{
#ifdef SO_REUSEPORT
	int one = 1;
	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
}

static bool bind_port(int fd, uint16_t port, uint16_t *out_actual)
{
	struct sockaddr_in sa;

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0)
		return false;
	if (out_actual) {
		socklen_t sl = sizeof sa;

		if (getsockname(fd, (struct sockaddr *)&sa, &sl) == 0)
			*out_actual = ntohs(sa.sin_port);
		else
			*out_actual = port;
	}
	return true;
}

static int multicast_join_asm(int fd, const char *group_s)
{
	struct ip_mreq mreq;

	memset(&mreq, 0, sizeof mreq);
	if (inet_pton(AF_INET, group_s, &mreq.imr_multiaddr) != 1)
		return -1;
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq)
			!= 0)
		return -1;
	return 0;
}

bool lan_net_open_discovery(uint16_t bind_port_want, int *out_fd,
		uint16_t *out_actual_port)
{
	int fd = udp_socket_base(true /* SO_BROADCAST */);

	if (fd < 0)
		return false;
	sock_try_reuseport(fd);
	if (!bind_port(fd, bind_port_want, out_actual_port)) {
		close(fd);
		return false;
	}
	if (multicast_join_asm(fd, LAN_MULTICAST_GROUP) != 0) {
		perror("[lan] IP_ADD_MEMBERSHIP discovery listener");
		/* continue: broadcast adverts may still work on some LANs */
	}
	*out_fd = fd;
	return true;
}

bool lan_net_open_game(uint16_t bind_port_want, int *out_fd,
		uint16_t *out_actual_port)
{
	int fd = udp_socket_base(false);

	if (fd < 0)
		return false;
	if (bind_port_want != 0)
		sock_try_reuseport(fd);
	if (!bind_port(fd, bind_port_want, out_actual_port)) {
		close(fd);
		return false;
	}
	*out_fd = fd;
	return true;
}

void lan_net_close(int fd)
{
	if (fd >= 0)
		close(fd);
}

int lan_net_recv(int fd, uint8_t *buf, size_t buf_cap,
		LanAddr *from)
{
	ssize_t n;
	struct sockaddr_in sa;
	socklen_t salen = sizeof sa;

	memset(&sa, 0, sizeof sa);
	n = recvfrom(fd, buf, buf_cap, 0,
			(struct sockaddr *)&sa, &salen);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
	if (from) {
		from->ip = ntohl(sa.sin_addr.s_addr);
		from->port = ntohs(sa.sin_port);
	}
	return (int)n;
}

bool lan_net_send(int fd, const uint8_t *buf, size_t len, LanAddr to)
{
	struct sockaddr_in sa;

	LanAddr t;

	t = to;
	lan_addr_rewrite_loopback_for_local_hosts(&t);
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(t.ip);
	sa.sin_port = htons(t.port);
	return sendto(fd, buf, len, 0,
			(const struct sockaddr *)&sa,
			sizeof sa)
			== (ssize_t)len;
}

void lan_addr_to_string(LanAddr a, char *out, size_t cap)
{
	snprintf(out, cap, "%u.%u.%u.%u:%u",
			(unsigned)((a.ip >> 24) & 0xFF),
			(unsigned)((a.ip >> 16) & 0xFF),
			(unsigned)((a.ip >> 8) & 0xFF),
			(unsigned)(a.ip & 0xFF),
			(unsigned)a.port);
}

LanAddr lan_addr_broadcast(uint16_t port)
{
	LanAddr a = { .ip = (uint32_t)0xFFFFFFFFu, .port = port };

	return a;
}

LanAddr lan_addr_multicast_peer(uint16_t port)
{
	LanAddr a = { .ip = 0, .port = port };
	struct in_addr ina;

#ifdef IN_MULTICAST_LINUX
#undef IN_MULTICAST_LINUX
#endif
	if (inet_pton(AF_INET, LAN_MULTICAST_GROUP, &ina) == 1)
		a.ip = ntohl(ina.s_addr);
	return a;
}
