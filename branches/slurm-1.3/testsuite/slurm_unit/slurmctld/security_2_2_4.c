/*
 * security_2_2_4.c
 * LLNL security test to validate SLURM's wiki interface security
 *
 * Compilation line varies by system.
 * For Peleton:
 *   cc -L/usr/lib64 -lslurm -osecurity_2_2_4 security_2_2_4.c
 * For most Linux system:
 *   cc -lslurm -osecurity_2_8 security_2_2_4.c
 *
 * Execute line:
 *   ./security_2_2_4
 *
 * Expected response:
 *   Bad checksum reported
 *   SUCCESS
 */
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <slurm/slurm.h>
#include <sys/types.h>
#include <sys/socket.h>

#define _DEBUG 0

static int _conn_wiki_port(char *host, int port)
{
	int sock_fd;
	struct sockaddr_in wiki_addr;
	struct hostent *hptr;

	hptr = gethostbyname(host);
	if (hptr == NULL) {
		perror("gethostbyname");
		exit(1);
	}
	if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}
	bzero((char *) &wiki_addr, sizeof(wiki_addr));
	wiki_addr.sin_family = AF_INET;
	wiki_addr.sin_port   = htons(port);
	memcpy(&wiki_addr.sin_addr.s_addr, hptr->h_addr, hptr->h_length);		
	sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (connect(sock_fd, (struct sockaddr *) &wiki_addr, 
			sizeof(wiki_addr))) {
		perror("connect");
		exit(1);
	}
	return sock_fd;
}

static size_t _read_bytes(int fd, char *buf, const size_t size)
{
	size_t bytes_remaining, bytes_read;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while (bytes_remaining > 0) {
		bytes_read = read(fd, ptr, bytes_remaining);
		if (bytes_read <= 0)
			return 0;
		bytes_remaining -= bytes_read;
		ptr += bytes_read;
	}

	return size;
}

static size_t _write_bytes(int fd, char *buf, const size_t size)
{
	size_t bytes_remaining, bytes_written;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while (bytes_remaining > 0) {
		bytes_written = write(fd, ptr, bytes_remaining);
		if (bytes_written < 0)
			return 0;
		bytes_remaining -= bytes_written;
		ptr += bytes_written;
	}
	return size;
}

static size_t _send_msg(int fd, char *buf, size_t size)
{
	char header[10];
	size_t data_sent;

	(void) sprintf(header, "%08lu\n", (uint32_t) size);
	if (_write_bytes(fd, header, 9) != 9) {
		perror("writing message header");
		exit(1);
	}

	data_sent = _write_bytes(fd, buf, size);
	if (data_sent != size) {
		perror("writing message");
		exit(1);
	}

	return data_sent;
}

static char *_recv_msg(int fd)
{
	char header[10];
	uint32_t size;
	char *buf;

	if (_read_bytes(fd, header, 9) != 9) {
		perror("reading message header");
		exit(1);
	}
	if (sscanf(header, "%ul", &size) != 1) {
		perror("parsing message header");
		exit(1);
	}
	buf = calloc(1, (size+1));	/* need '\0' on end to print */
	if (buf == NULL) {
		perror("malloc");
		exit(1);
	}
	if (_read_bytes(fd, buf, size) != size) {
		perror("reading message");
		exit(1);
	}
	return buf;
}

main(int argc, char **argv)
{
	int wiki=0, sched_port=0, wiki_fd;
	char control_addr[1024];
	char *in_msg, out_msg[1024], *resp;

	/*
	 * Get current SLURM configuration
	 */
#ifdef AIX
	if (argc == 1) {
		printf("Usage: %s <ControlAddr>\n");
		exit(1);
	}
	strcpy(control_addr, argv[1]);
	sched_port = 7321;
	wiki = 1;
#else
	slurm_ctl_conf_t *conf_ptr;
	slurm_load_ctl_conf((time_t) 0, &conf_ptr);
	if (strcasecmp(conf_ptr->schedtype, "sched/wiki2") == 0)
		wiki=1;
	strncpy(control_addr, conf_ptr->control_addr, sizeof(control_addr));
	sched_port = conf_ptr->schedport;
	slurm_free_ctl_conf(conf_ptr);
#endif

	if (wiki == 0) {
		printf("SLURM's Wiki2 plugin not configured, nothing to test\n");
		printf("SUCCESS\n");
		exit(0);
	}
#if _DEBUG
	printf("SLURM's Wiki2 configured on %s:%d\n", control_addr, sched_port);
#endif

	/*
	 * Build a Wiki request with arbitrary encryption key
	 */
	snprintf(out_msg, sizeof(out_msg),
		"CK=1234567812345678 TS=%u AUTH=root DT=CMD=GETJOBS ARG=0:ALL",
		(uint32_t) time(NULL));

	/*
	 * Send the message and get the response from SLURM
	 */
#if _DEBUG
	printf("Sending message: %s\n", out_msg);
#endif
	wiki_fd = _conn_wiki_port(control_addr, sched_port);
	_send_msg(wiki_fd, out_msg, strlen(out_msg));
	in_msg = _recv_msg(wiki_fd);
#if _DEBUG
	printf("Received message: %s\n", in_msg);
#endif

	/*
	 * Parse the results for desired error
	 */
	resp = strstr(in_msg, "RESPONSE=bad checksum");
	if (resp) {
		printf("Bad checksum reported\n");
		printf("SUCCESS\n");
		exit(0);
	} else {
		printf("Bad response: %s\n", in_msg);
		exit(1);
	}
}
