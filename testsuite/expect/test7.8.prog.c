/*****************************************************************************\
 *  test7.8.prog.c - Test of sched/wiki plugin
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

/* global variables */
char *control_addr;
int   is_bluegene, sched_port;
long  job_id1, job_id2;

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

static void _xmit(char *msg)
{
	int msg_len = strlen(msg);
	char *out_msg, *in_msg, *sc_ptr;
	int wiki_fd = _conn_wiki_port(control_addr, sched_port);
	int sc;

	out_msg = msg;
	printf("send:%s\n", out_msg);
	_send_msg(wiki_fd, out_msg, strlen(out_msg));
	in_msg = _recv_msg(wiki_fd);
	printf("recv:%s\n\n", in_msg);
	sc_ptr = strstr(in_msg, "SC=");
	sc = atoi(sc_ptr+3);
	if (sc != 0) {
		fprintf(stderr, "RPC failure\n");
		exit(1);
	}
	free(in_msg);
	close(wiki_fd);
}

static void _get_jobs(void)
{
	time_t now = time(NULL);
	char out_msg[128];

	/* Dump all data */
	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=%s",
		(uint32_t) now, "CMD=GETJOBS ARG=0:ALL");
	_xmit(out_msg);
}

static void _get_nodes(void)
{
	time_t now = time(NULL);
	char out_msg[128];

	/* Dump all data */
	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=%s",
		(uint32_t) now, "CMD=GETNODES ARG=0:ALL");
	_xmit(out_msg);
}

static void _cancel_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=CANCELJOB ARG=%ld TYPE=ADMIN",
		(uint32_t) now, my_job_id);
	_xmit(out_msg);
}

static void _modify_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[256];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=MODIFYJOB ARG=%ld "
		/* "PARTITION=pdebug " */
		/* "NODES=2 " */
		/* "DEPEND=afterany:3 " */
		/* "INVALID=123 " */
		"TIMELIMIT=10 BANK=test_bank",
		(uint32_t) now, my_job_id);
	_xmit(out_msg);
}

static void _resume_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=RESUMEJOB ARG=%ld",
		(uint32_t) now, my_job_id);
	_xmit(out_msg);
}

static void _start_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=STARTJOB ARG=%ld TASKLIST=",
		/* Empty TASKLIST means we don't care */
		(uint32_t) now, my_job_id);
	_xmit(out_msg);
}

static void _suspend_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=SUSPENDJOB ARG=%ld",
		(uint32_t) now, my_job_id);
	_xmit(out_msg);
}

int main(int argc, char * argv[])
{
	if (argc != 6) {
		printf("Usage: %s, control_addr job_id1 job_id2 sched_port is_bluegene\n",
			argv[0]);
		exit(1);
	}

	control_addr = argv[1];
	job_id1      = atoi(argv[2]);
	job_id2      = atoi(argv[3]);
	sched_port   = atoi(argv[4]);
	is_bluegene  = atoi(argv[5]);
	printf("control_addr=%s job_id=%ld,%ld sched_port=%d is_bluegene=%d\n",
		control_addr, job_id1, job_id2, sched_port, is_bluegene);

	_get_jobs();
	_get_nodes();
	_modify_job(job_id1);
	_get_jobs();
	_start_job(job_id1);
	if (!is_bluegene) {
		_suspend_job(job_id1);
		_resume_job(job_id1);
	}
	_cancel_job(job_id2);
	sleep(5);
	_get_jobs();

	printf("SUCCESS\n");
	exit(0);
}

