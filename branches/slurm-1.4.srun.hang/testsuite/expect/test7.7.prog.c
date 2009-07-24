/*****************************************************************************\
 *  test7.7.prog.c - Test of sched/wiki2 plugin
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "./test7.7.crypto.c"

#define _DEBUG 0

/* global variables */
char *auth_key, *control_addr;
int   e_port, is_bluegene, sched_port;
long  job_id;

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

static int _conn_event_port(char *host, int port)
{
	int i, rc, sock_fd;
	struct sockaddr_in wiki_addr;
	struct hostent *hptr;

	hptr = gethostbyname(host);
	if (hptr == NULL) {
		perror("gethostbyname");
		exit(1);
	}
	if ((sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("socket");
		exit(1);
	}
	bzero((char *) &wiki_addr, sizeof(wiki_addr));
	wiki_addr.sin_family = AF_INET;
	wiki_addr.sin_port   = htons(port);
	memcpy(&wiki_addr.sin_addr.s_addr, hptr->h_addr, hptr->h_length);
	for (i=0; ; i++) {
		if (i)
			sleep(5);
		rc = bind(sock_fd, (struct sockaddr *) &wiki_addr,
			  sizeof(wiki_addr));
		if (rc == 0)
			break;
		if ((errno != EINVAL) || (i > 5)) {
			printf("WARNING: bind to port %i; %s\n", 
			       port, strerror(errno));
			return -1;
		}
		printf("WARNING: port %i in use, retrying\n", port);
	}
	listen(sock_fd, 1);
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

static int _xmit(char *msg)
{
	int msg_len = strlen(msg);
	char *out_msg, *in_msg, sum[20], *sc_ptr;
	int wiki_fd = _conn_wiki_port(control_addr, sched_port);
	int sc;

	out_msg = calloc(1, (msg_len+100));
	if (out_msg == NULL) {
		perror("malloc");
		exit(1);
	}
	checksum(sum, auth_key, msg);
	sprintf(out_msg, "%s %s", sum, msg);
	printf("send:%s\n", out_msg);
	_send_msg(wiki_fd, out_msg, strlen(out_msg));
	in_msg = _recv_msg(wiki_fd);
	printf("recv:%s\n\n", in_msg);
	sc_ptr = strstr(in_msg, "SC=");
	sc = atoi(sc_ptr+3);
	if (sc != 0)
		fprintf(stderr, "RPC failure\n");
	free(in_msg);
	close(wiki_fd);
	return sc;
}

static void _event_mgr(void)
{
	int accept_fd, event_fd;
	int accept_addr_len = sizeof(struct sockaddr);
	size_t cnt;
	char in_msg[5];
	struct sockaddr_in accept_addr;

	if ((event_fd = _conn_event_port(control_addr, e_port)) < 0)
		return;
	printf("READY_FOR_EVENT\n");
	if ((accept_fd = accept(event_fd, (struct sockaddr *) &accept_addr,
			&accept_addr_len)) < 0) {
		perror("accept");
		exit(1);
	}
	close(event_fd);

	cnt = _read_bytes(accept_fd, in_msg, sizeof(in_msg));
	if (cnt > 0)
		printf("event recv:%s\n\n", in_msg);
	close(accept_fd);
}

static void _get_jobs(void)
{
	time_t now = time(NULL);
	char out_msg[128];

	/* Dump all data */
	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=%s",
		(uint32_t) now, "CMD=GETJOBS ARG=0:ALL");
	if (_xmit(out_msg))
		exit(1);

	/* Dump volitile data */
	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=GETJOBS ARG=%u:ALL",
		(uint32_t) now, (uint32_t) 1);
	if (_xmit(out_msg))
		exit(1);

	/* Dump state only */
	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=GETJOBS ARG=%u:ALL",
		(uint32_t) now, (uint32_t) (now+2));
	if (_xmit(out_msg))
		exit(1);
}

static void _get_nodes(void)
{
	time_t now = time(NULL);
	char out_msg[128];

	/* Dump all data */
	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=%s", 
		(uint32_t) now, "CMD=GETNODES ARG=0:ALL");
	if (_xmit(out_msg))
		exit(1);

	/* Dump volitile data */
	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=GETNODES ARG=%u:ALL",
		(uint32_t) now, (uint32_t) 1);
	if (_xmit(out_msg))
		exit(1);

	/* Dump state only */
	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=GETNODES ARG=%u:ALL",
		(uint32_t) now, (uint32_t) (now+2));
	if (_xmit(out_msg))
		exit(1);
}

static void _cancel_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=CANCELJOB ARG=%ld "
		"TYPE=ADMIN "
		"COMMENT=\"cancel comment\" ",
		(uint32_t) now, my_job_id);
	if (_xmit(out_msg))
		exit(1);
}

static void _start_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];
	int i, rc;

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=STARTJOB ARG=%ld "
		"COMMENT=\'start comment\' "
		"TASKLIST=",	/* Empty TASKLIST means we don't care */
		(uint32_t) now, my_job_id);

	for (i=0; i<10; i++) {
		if (i)
			sleep(10);
		rc = _xmit(out_msg);
		if (rc == 0)
			break;
		/* Still completing after requeue */
	}
	if (rc != 0)
		exit(1);
}

static void _suspend_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=SUSPENDJOB ARG=%ld",
		(uint32_t) now, my_job_id);
	if (_xmit(out_msg))
		exit(1);
}

static void _signal_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=SIGNALJOB ARG=%ld VALUE=URG",
		(uint32_t) now, my_job_id);
	if (_xmit(out_msg))
		exit(1);
}

static void _modify_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[256];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=MODIFYJOB ARG=%ld "
		/* "MINSTARTTIME=55555 " */
		/* "JOBNAME=foo " */
		/* "RFEATURES=big " */
		/* "PARTITION=pdebug " */
		/* "NODES=2 " */ 
		/* "DEPEND=afterany:3 " */
		/* "INVALID=123 " */ 
		/* "VARIABLELIST=TEST_ENV1=test_val1 " */
		"VARIABLELIST=TEST_ENV1=test_val1,TEST_ENV2=test_val2 "
		"TIMELIMIT=10 BANK=test_bank",
		(uint32_t) now, my_job_id);
	if (_xmit(out_msg))
		exit(1);
}

static void _notify_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[256];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=NOTIFYJOB ARG=%ld "
		"MSG=this_is_a_test",
		(uint32_t) now, my_job_id);
	if (_xmit(out_msg))
		exit(1);
}

static void _resume_job(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=RESUMEJOB ARG=%ld",
		(uint32_t) now, my_job_id);
	if (_xmit(out_msg))
		exit(1);
}

static void _job_requeue(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=REQUEUEJOB ARG=%ld",
		(uint32_t) now, my_job_id);
	if (_xmit(out_msg))
		exit(1);
}

static void _job_will_run(long my_job_id)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=JOBWILLRUN ARG=JOBID=%ld,%s",
		(uint32_t) now, my_job_id,
		"");		/* put available node list here */
	if (_xmit(out_msg))
		exit(1);
}

static void _initialize(void)
{
	time_t now = time(NULL);
	char out_msg[128];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=INITIALIZE ARG=USEHOSTEXP=N EPORT=%u",
		(uint32_t) now, e_port);
	if (_xmit(out_msg))
		exit(1);
}

static void _single_msg(void)
{
	time_t now = time(NULL);
	char out_msg[1024];

	snprintf(out_msg, sizeof(out_msg),
		"TS=%u AUTH=root DT=CMD=%s",
		(uint32_t) now, 
		"JOBWILLRUN ARG=JOBID=65537,bgl[000x733] "
		"JOBID=65539,bgl[000x733] JOBID=65538,bgl[000x733]");
	if (_xmit(out_msg))
		exit(1);
}

int main(int argc, char * argv[])
{
	if (argc < 6) {
		printf("Usage: %s, auth_key control_addr e_port "
			"job_id sched_port is_bluegene\n", argv[0]);
		exit(1);
	}

	auth_key     = argv[1];
	control_addr = argv[2];
	e_port       = atoi(argv[3]);
	job_id       = atoi(argv[4]);
	sched_port   = atoi(argv[5]);
	is_bluegene  = atoi(argv[6]);
	printf("auth_key=%s control_addr=%s e_port=%d job_id=%d sched_port=%d "
		"is_bluegene=%d\n", 
		auth_key, control_addr, e_port, job_id, sched_port, is_bluegene);

#if _DEBUG
	_single_msg();
#else
	_initialize();
	_get_jobs();
	_get_nodes();
	_job_will_run(job_id);
	_modify_job(job_id);
	_get_jobs();
	_start_job(job_id);
	_get_jobs();
	if (!is_bluegene) {
		_suspend_job(job_id);
		_resume_job(job_id);
	}
	_notify_job(job_id);
	_signal_job(job_id);
	if (e_port)
		_event_mgr();
	else {
		printf("READY\n");
		sleep(3);
	}
	_cancel_job(job_id+1);
	_job_requeue(job_id);	/* Put job back into HELD state */
	sleep(10);
	_start_job(job_id);
	_get_jobs();
#endif
	printf("SUCCESS\n");
	exit(0);
}

