// Stream client: for each FRAME waits the fence, reads the counter blocks, prints "counter ready_ns submit_ns".
// usage: probe-client <socket> <frames> <logfile>
#include "../../src/platform/linux/hermes_stream_v1.h"
#include <drm_fourcc.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }
int main(int argc, char **argv)
{
	int s = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	struct sockaddr_un a = { .sun_family = AF_UNIX }; strncpy(a.sun_path, argv[1], sizeof(a.sun_path) - 1);
	if (connect(s, (void *)&a, sizeof(a))) { perror("connect"); return 1; }
	long want = atol(argv[2]); FILE *log = fopen(argv[3], "w");
	struct hermes_client_hello h = { .hdr = { HERMES_CLIENT_HELLO, 0, sizeof(h) }, .version_major = HERMES_STREAM_VERSION_MAJOR, .version_minor = HERMES_STREAM_VERSION_MINOR };
	strcpy(h.client_name, "probe"); send(s, &h, sizeof(h), 0);
	struct { struct hermes_client_configure c; struct hermes_format_modifier f; } cfg = { 0 };
	cfg.c.hdr.type = HERMES_CLIENT_CONFIGURE; cfg.c.hdr.size = sizeof(cfg); cfg.c.width = 1920; cfg.c.height = 1080; cfg.c.slot_count = 3; cfg.c.num_formats = 1;
	cfg.f.drm_format = DRM_FORMAT_XRGB8888; cfg.f.modifier = DRM_FORMAT_MOD_LINEAR;
	send(s, &cfg, sizeof(cfg), 0);
	uint8_t *maps[8] = { 0 }; uint32_t strides[8]; long got = 0; uint32_t cur_pool = 0;
	for (;;) {
		char buf[HERMES_MAX_MESSAGE_SIZE], ctl[CMSG_SPACE(sizeof(int))];
		struct iovec iov = { buf, sizeof(buf) };
		struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl, .msg_controllen = sizeof(ctl) };
		ssize_t n = recvmsg(s, &m, 0);
		if (n <= 0) break;
		int fd = -1; struct cmsghdr *cm = CMSG_FIRSTHDR(&m);
		if (cm && cm->cmsg_type == SCM_RIGHTS) memcpy(&fd, CMSG_DATA(cm), sizeof(int));
		struct hermes_msg_header *hd = (void *)buf;
		if (getenv("PROBE_DEBUG")) fprintf(stderr, "msg type=%u n=%zd fd=%d\n", hd->type, n, fd);
		if (hd->type == HERMES_SERVER_POOL) { cur_pool = ((struct hermes_server_pool *)buf)->pool_id; }
		else if (hd->type == HERMES_SERVER_BUFFER) {
			struct hermes_server_buffer *b = (void *)buf;
			strides[b->slot] = b->strides[0];
			close(fd);
		} else if (hd->type == HERMES_SERVER_ERROR) { fprintf(stderr, "server error: %s\n", ((struct hermes_server_error *)buf)->message); return 1; }
		else if (hd->type == HERMES_SERVER_FRAME) {
			struct hermes_server_frame *f = (void *)buf;
			uint64_t recv_t = now_ns();
			if (fd >= 0) { struct pollfd p = { fd, POLLIN, 0 }; poll(&p, 1, 1000); close(fd); }
			uint64_t ready = now_ns();
			uint32_t counter = (uint32_t)f->frame_id;
			fprintf(log, "%u %llu %llu %llu\n", counter, (unsigned long long)ready, (unsigned long long)f->submit_time_ns, (unsigned long long)recv_t);
			fflush(log);
			struct hermes_client_release r = { .hdr = { HERMES_CLIENT_RELEASE, 0, sizeof(r) }, .pool_id = cur_pool, .slot = f->slot };
			send(s, &r, sizeof(r), 0);
			if (++got >= want) break;
		}
	}
	fclose(log); return 0;
}
