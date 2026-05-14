/*
 * Standalone POSIX UDP probes for LAN Phase 1 (no icytower linkage).
 *
 * Usage: ./mac_lan_probe [s1|s2|s3|s4|s5|all]
 *
 * S1 "PASS": ephemeral listener missed broadcast destined to DISC_PORT
 *           (shows fixed-port broadcast adverts need a pinned listener).
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Use a high port so running the probe does not fight a real icytower on
 * LAN_DEFAULT_PORT during development.
 */
#define DISC_PORT 58123u
#define PKT       "PROBE"

static void die(const char *ctx)
{
	fprintf(stderr, "%s: %s\n", ctx, strerror(errno));
	exit(1);
}

static int udp_socket_nonblock(void)
{
	int fd, yes = 1, fl;

	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0)
		die("socket");
	fl = fcntl(fd, F_GETFL, 0);
	if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0)
		die("fcntl O_NONBLOCK");
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0)
		die("SO_REUSEADDR");
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes) < 0)
		die("SO_BROADCAST");
	return fd;
}

static void bind_any(int fd, uint16_t port)
{
	struct sockaddr_in sa;

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0)
		die("bind");
}

static void send_broadcast(int fd, uint16_t dest_port)
{
	struct sockaddr_in sa;

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(dest_port);
	sa.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	if (sendto(fd, PKT, sizeof PKT, 0, (const struct sockaddr *)&sa,
			sizeof sa) < 0)
		die("sendto broadcast");
}

static bool recv_once_or_timeout(int fd, int timeout_ms)
{
	fd_set fds;
	struct timeval tv;
	unsigned char buf[64];
	ssize_t n;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	n = select(fd + 1, &fds, NULL, NULL, &tv);
	if (n < 0)
		die("select");
	if (!n)
		return false;
	n = recvfrom(fd, buf, sizeof buf, 0, NULL, NULL);
	return n == (ssize_t)sizeof PKT && memcmp(buf, PKT, sizeof PKT) == 0;
}

static void send_multicast(int fd)
{
	unsigned char ttl = 1;
	struct sockaddr_in dst;

	memset(&dst, 0, sizeof dst);
	dst.sin_family = AF_INET;
	dst.sin_port = htons(DISC_PORT);
	if (inet_pton(AF_INET, "239.43.137.251", &dst.sin_addr) != 1)
		die("inet_pton mcast dest");
	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl) < 0)
		perror("IP_MULTICAST_TTL");
	if (sendto(fd, PKT, sizeof PKT, 0, (struct sockaddr *)&dst,
			sizeof dst) != (ssize_t)sizeof PKT)
		die("sendto multicast");
}

static int probe_s1_clean(void)
{
	pid_t pid;
	int st;

	pid = fork();
	if (pid < 0)
		die("fork S1 fork");
	if (pid == 0) {
		int ephem = udp_socket_nonblock();

		bind_any(ephem, 0);
		_exit(recv_once_or_timeout(ephem, 800) ? 2 : 0);
	}

	usleep(200000);

	{
		int tx;

		tx = udp_socket_nonblock();
		bind_any(tx, DISC_PORT);
		send_broadcast(tx, DISC_PORT);
		close(tx);
	}

	waitpid(pid, &st, 0);
	if (WIFEXITED(st) && WEXITSTATUS(st) != 2) {
		printf("S1 PASS: child on ephemeral-only saw no pkt on its "
		       "listening socket "
		       "(fixed-port broadcast reaches only :%u binds).\n",
		       (unsigned)DISC_PORT);
		return 0;
	}

	fprintf(stderr, "S1 FAIL: ephemeral listener unexpectedly got pkt "
			"(WEXIT=%d).\n",
			WIFEXITED(st) ? WEXITSTATUS(st) : -1);
	return 1;
}

static int probe_s2_dual_bind(void)
{
	int first = udp_socket_nonblock(), second = udp_socket_nonblock();
	struct sockaddr_in sa;

	bind_any(first, DISC_PORT);

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(DISC_PORT);
	if (bind(second, (struct sockaddr *)&sa, sizeof sa) != 0) {
		printf("S2 Darwin: Second bind SAME UDP port FAILED: %s\n",
				strerror(errno));
		close(first);
		close(second);
		return 0;
	}
	printf(
			"S2 Darwin: Second bind SAME UDP port SUCCEEDED "
			"(SO_REUSEADDR allows dual listeners — delivery split per "
			"kernel).\n");
	send_broadcast(first, DISC_PORT);
	(void)recv_once_or_timeout(first, 200);
	(void)recv_once_or_timeout(second, 200);
	close(first);
	close(second);
	return 0;
}

static int probe_s3_dual_sock(void)
{
	pid_t pid;
	int st;

	pid = fork();
	if (!pid) {
		int disc = udp_socket_nonblock(), game __attribute__((unused));

		game = udp_socket_nonblock();
		bind_any(disc, DISC_PORT);
		bind_any(game, 0);
		sleep(2);
		_exit(recv_once_or_timeout(disc, 1500) ? 0 : 3);
	}
	usleep(100000);

	{
		int tx;

		tx = udp_socket_nonblock();
		bind_any(tx, 0);
		send_broadcast(tx, DISC_PORT);
		close(tx);
	}
	waitpid(pid, &st, 0);
	if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
		printf("S3 PASS: pinned discovery hears broadcast while(game "
		       "sock ephemeral).\n");
		return 0;
	}
	fprintf(stderr,
			"S3 FAIL child exit=%d\n",
			WIFEXITED(st) ? WEXITSTATUS(st) : -1);
	return 1;
}

static int join_mgroup(int fd)
{
	struct ip_mreq mreq;

	memset(&mreq, 0, sizeof mreq);
	if (inet_pton(AF_INET, "239.43.137.251", &mreq.imr_multiaddr) != 1)
		die("inet_pton mcast");
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq)
			!= 0)
		perror("IP_ADD_MEMBERSHIP");
	else
		return 0;
	return -1;
}

static int probe_s4_mcast(void)
{
	pid_t pid;
	int st;

	pid = fork();
	if (!pid) {
		int fd = udp_socket_nonblock();

		bind_any(fd, DISC_PORT);
		if (join_mgroup(fd) != 0)
			_exit(10);
		sleep(5);
		_exit(recv_once_or_timeout(fd, 7800) ? 0 : 11);
	}
	usleep(200000);

	{
		int tx;

		tx = udp_socket_nonblock();
		bind_any(tx, 0);
		send_multicast(tx);
		close(tx);
	}
	waitpid(pid, &st, 0);
	if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
		printf("S4 PASS: multicast ASM to 239.43.137.251:%u worked "
		       "(loopback).\n",
		       (unsigned)DISC_PORT);
		return 0;
	}
	fprintf(stderr, "S4 FAIL child exit=%d\n",
			WIFEXITED(st) ? WEXITSTATUS(st) : -1);
	return 1;
}

static void note_s5(void)
{
	printf("S5 SKIP: optional Bonjour two-register sanity check "
	       "(build mac_lan_probe_bonjour target).\n");
}

int main(int argc, char **argv)
{
	const char *m = argc > 1 ? argv[1] : "all";
	int errs = 0;

	if (strcmp(m, "s1") == 0)
		return probe_s1_clean();

	if (strcmp(m, "s2") == 0)
		return probe_s2_dual_bind();

	if (strcmp(m, "s3") == 0)
		return probe_s3_dual_sock();

	if (strcmp(m, "s4") == 0)
		return probe_s4_mcast();

	if (strcmp(m, "s5") == 0) {
		note_s5();
		return 0;
	}

	if (strcmp(m, "all") == 0) {
		printf("--- S1 ephemeral vs fixed-port broadcast ---\n");
		errs += probe_s1_clean();
		putchar('\n');
		printf("--- S2 duplicate bind SO_REUSEADDR ---\n");
		probe_s2_dual_bind();
		putchar('\n');
		printf("--- S3 dual socket pinned discovery ---\n");
		errs += probe_s3_dual_sock();
		putchar('\n');
		printf("--- S4 ASM multicast discovery ---\n");
		errs += probe_s4_mcast();
		putchar('\n');
		note_s5();
		return errs ? 1 : 0;
	}

	fprintf(stderr, "usage: %s [s1|s2|s3|s4|s5|all]\n",
			argc > 0 ? argv[0] : "mac_lan_probe");
	return 2;
}
