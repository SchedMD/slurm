/*****************************************************************************\
 *  main.c - Primary logic for slurmsmwd
 *****************************************************************************
 *  Copyright (C) 2017 Regents of the University of California
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <ctype.h>
#include <signal.h>
#include <sys/time.h>
#include <poll.h>

#include "src/common/slurm_xlator.h"
#include "slurm/slurm.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/daemonize.h"
#include "src/common/xsignal.h"
#include "src/common/log.h"
#include "src/common/proc_args.h"

#include "read_config.h"

#define MAX_POLL_WAIT 500

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#define POLLRDHUP POLLHUP
#endif

/* Local variables */
static int foreground = 0;
static log_options_t log_opts =		/* Log to stderr & syslog */
	LOG_OPTS_INITIALIZER;
static int _sigarray[] = {	/* blocked signals for this process */
	SIGINT,  SIGTERM, SIGCHLD, SIGUSR1,
	SIGUSR2, SIGTSTP, SIGXCPU, SIGQUIT,
	SIGPIPE, SIGALRM, SIGABRT, SIGHUP, 0 };
static const char *xtconsumer_path = "/opt/cray/hss/default/bin/xtconsumer";
static int slurmsmwd_log_fmt = LOG_FMT_ISO8601_MS;
static pthread_t xtc_thread;
static pid_t xtc_pid = 0;

static int stop_running = 0;
static uint16_t debug_level = 0;
static pthread_mutex_t down_node_lock;
static int *down_node;
static size_t n_down_node;
static size_t down_node_sz;
static const char *event_description[] = {
	"Invalid Event",
	"ec_node_failed",
	"ec_node_unavailable"
};

typedef enum event_type {
	EVENT_INVALID = 0,
	EVENT_NODE_FAILED,
	EVENT_NODE_UNAVAILABLE,
	EVENT_LIMIT
} event_type_t;

static int _start_xtconsumer(char **xtc_argv, pid_t *pid);

static void _shutdown_threads(void)
{
	stop_running = 1;
}

static int _getnid(const char *cname, int dim)
{
	int cabinet, row, chassis, slot, node;
	int nodes_per_slot = 4;
	int nodes_per_chassis = nodes_per_slot * 16;
	int nodes_per_cabinet = nodes_per_chassis * 3;
	int nodes_per_row = nodes_per_cabinet * dim;

	sscanf(cname, "c%d-%dc%ds%dn%d",
	       &cabinet, &row, &chassis, &slot, &node);

	return cabinet * nodes_per_cabinet + row * nodes_per_row +
		chassis * nodes_per_chassis + slot * nodes_per_slot + node;
}

static char *_getnidlist()
{
	char *ret = NULL;
	size_t idx = 0;
	int last_nid = 0;
	int in_range = 0;
	ret = xstrdup("nid[");
	for (idx = 0; idx < n_down_node; idx++) {
		int curr_nid = down_node[idx];
		if (last_nid == 0) {
			xstrfmtcat(ret, "%05d", curr_nid);
		} else if (curr_nid == last_nid) {
			continue;
		} else if (curr_nid - last_nid > 1) {
			if (in_range) {
				xstrfmtcat(ret, "-%05d", last_nid);
			}
			xstrfmtcat(ret, ",%05d", curr_nid);
			in_range = 0;
		} else if (idx == n_down_node - 1) {
			xstrfmtcat(ret, "-%05d", curr_nid);
		} else {
			in_range = 1;
		}
		last_nid = curr_nid;
	}
	xstrfmtcat(ret, "]");
	return ret;
}

static int _mark_nodes_down()
{
	/* locks are assumed to be held */
	int rc = 0;
	update_node_msg_t *update_msg = xmalloc(sizeof(update_node_msg_t));

	slurm_init_update_node_msg(update_msg);

	update_msg->node_names = _getnidlist();
	update_msg->node_state = NODE_STATE_NO_RESPOND;

	info("setting %s to NotResponding", update_msg->node_names);

	rc = slurm_update_node(update_msg);
	if (rc != SLURM_SUCCESS)
		error("failed to set %s to NotResponding: %m",
		      update_msg->node_names);

	slurm_free_update_node_msg(update_msg);
	return rc;

}

static void *_process_data(void *arg)
{
	while (!stop_running) {
		slurm_mutex_lock(&down_node_lock);
		if (n_down_node > 0) {
			slurm_info("down node cnt: %zu", n_down_node);
			_mark_nodes_down();
			n_down_node = 0;
		}
		slurm_mutex_unlock(&down_node_lock);
		usleep(2000000);

	}
	return NULL;
}

static event_type_t _parse_event(const char *input)
{
	if (strstr(input, "ec_node_failed") != NULL)
		return EVENT_NODE_FAILED;
	if (strstr(input, "ec_node_unavailable") != NULL)
		return EVENT_NODE_UNAVAILABLE;
	return EVENT_INVALID;
}

static int _cmp_nid(const void *a, const void *b, void *arg)
{
	int ai = * (const int *) a;
	int bi = * (const int *) b;

	return ai - bi;
}

static char *_trim(char *str)
{
	char *ptr = str;
	ssize_t len = 0;

	if (!str)
		return NULL;

	for ( ; isspace(*ptr) && *ptr != 0; ptr++) {
		/* that's it */
	}

	if (*ptr == 0)
		return ptr;

	len = strlen(ptr) - 1;
	for ( ; isspace(*(ptr + len)) && len > 0; len--) {
		*(ptr + len) = 0;
	}

	return ptr;
}

static void _send_failed_nodes(char *nodelist)
{
	char *search = nodelist;
	char *svptr = NULL;
	char *ptr = NULL;
	int nid = 0;

	slurm_mutex_lock(&down_node_lock);
	while ((ptr = strtok_r(search, " ", &svptr))) {
		search = NULL;

		ptr = strrchr(ptr, ':');
		if (!ptr)
			continue;
		ptr++;

		ptr = _trim(ptr);

		if (!strlen(ptr))
			continue;

		nid = _getnid(ptr, slurmsmwd_cabinets_per_row);

		if (!nid)
			continue;
		if (n_down_node + 1 >= down_node_sz) {
			size_t alloc_quantity = (n_down_node + 1) * 2;
			size_t alloc_size = sizeof(int) * alloc_quantity;
			down_node = xrealloc(down_node, alloc_size);
			down_node_sz = alloc_quantity;
		}
		down_node[n_down_node++] = nid;
	}
	qsort_r(down_node, n_down_node, sizeof(int), _cmp_nid, NULL);
	slurm_mutex_unlock(&down_node_lock);
}

/*
  2017-05-16 07:17:12|2017-05-16 07:17:12|0x40008063 - ec_node_failed|src=:1:s0|::c4-2c0s2n0 ::c4-2c0s2n2 ::c4-2c0s2n3
  2017-05-16 07:17:12|2017-05-16 07:17:12|0x400020e8 - ec_node_unavailable|src=:1:s0|::c4-2c0s2n2
  2017-05-16 08:11:01|2017-05-16 08:11:01|0x400020e8 - ec_node_unavailable|src=:1:s0|::c4-2c0s2n0 ::c4-2c0s2n1 ::c4-2c0s2n2 ::c4-2c0s2n3
*/
static void *_xtconsumer_listen(void *arg)
{
	int xtc_fd = 0;
	char *xtc_argv[] = {
		"xtconsumer",
		"-b",
		"ec_node_unavailable",
		"ec_node_failed"
	};
	char *line_ptr = NULL;
	char *buffer = NULL;
	size_t buffer_sz = 0;
	size_t buffer_off = 0;
	struct pollfd fds;
	int i = 0;
	int status = 0;

	xtc_fd = _start_xtconsumer(xtc_argv, &xtc_pid);
	debug2("got xtc_pid: %d", xtc_pid);

	if (xtc_fd < 0) {
		error("failed to open xtconsumer: %s",
		      slurm_strerror(slurm_get_errno()));
		return NULL;
	}

	/* xtconsumer seems to flush out its stdout on newline (typical)
	 * so reading line-by-line seems to be functional for this need
	 */
	buffer_sz = 1024;
	buffer = xmalloc(buffer_sz);
	while (!stop_running) {

		fds.fd = xtc_fd;
		fds.events = POLLIN | POLLHUP | POLLRDHUP;
		fds.revents = 0;

		i = poll(&fds, 1, MAX_POLL_WAIT);
		if (i == 0) {
			continue;
		} else if (i < 0) {
			error("poll(): %s", slurm_strerror(slurm_get_errno()));
			break;
		}
		if ((fds.revents & POLLIN) == 0)
			break;
		i = read(xtc_fd, buffer + buffer_off,
			 buffer_sz - buffer_off);

		debug3("read %d bytes", i);
		if (i == 0) {
			break;
		} else if (i < 0) {
			if (errno == EAGAIN)
				continue;
			error("read(): %s", slurm_strerror(slurm_get_errno()));
			break;
		}
		buffer_off += i;
		if (buffer_off + 1024 >= buffer_sz) {
			buffer_sz *= 2;
			buffer = xrealloc(buffer, buffer_sz);
		}

		/* NUL terminate the string to allow strchr to work
		 * buffer was expanded above to ensure there would be space
		 */
		buffer[buffer_off + 1] = '\0';
		while ((line_ptr = strchr(buffer, '\n')) != NULL) {
			event_type_t event = EVENT_INVALID;
			char *node_list = NULL;
			char *search = NULL;
			char *ptr = NULL;
			char *svptr = NULL;
			int token_idx = 0;

			*line_ptr = '\0';
			if (!strlen(buffer))
				goto advance_line;

			debug3("got line: %s", buffer);
			search = buffer;
			while ((ptr = strtok_r(search, "|", &svptr))) {
				search = NULL;
				if (token_idx == 2)
					event = _parse_event(ptr);
				if (token_idx == 4)
					node_list = xstrdup(ptr);

				token_idx++;
			}

			if (event == EVENT_NODE_FAILED ||
			    event == EVENT_NODE_UNAVAILABLE) {
				info("received event: %s, nodelist: %s",
				     event_description[event], node_list);
				_send_failed_nodes(node_list);
			}

			xfree(node_list);
			node_list = NULL;

		advance_line:
			*line_ptr = '\n';
			line_ptr++;
			for (ptr = buffer; *line_ptr; ptr++, line_ptr++)
				*ptr = *line_ptr;
			*ptr = *line_ptr;
			buffer_off = ptr - buffer;
		}



	}
	info("killing xtconsumer pid %d", xtc_pid);
	killpg(xtc_pid, SIGTERM);
	usleep(10000);
	killpg(xtc_pid, SIGKILL);
	waitpid(xtc_pid, &status, 0);
	close(xtc_fd);


#if 0
cleanup_break:
	if (node_list)
		xfree(node_list);
	break;
#endif
	xfree(buffer);
	return NULL;

}

/* _usage - print a message describing the command line arguments */
static void _usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s [OPTIONS]\n", prog_name);
	fprintf(stderr, "  -D         \t"
		"Run daemon in foreground.\n");
	fprintf(stderr, "  -h         \t"
		"Print this help message.\n");
	fprintf(stderr, "  -v         \t"
		"Verbose mode. Multiple -v's increase verbosity.\n");
	fprintf(stderr, "  -V         \t"
		"Print version information and exit.\n");
}

static void _parse_commandline(int argc, char **argv)
{
	int c = 0;

	opterr = 0;
	while ((c = getopt(argc, argv, "DhvV")) != -1)
		switch (c) {
		case 'D':
			foreground = 1;
			break;
		case 'h':
			_usage(argv[0]);
			exit(0);
			break;
		case 'v':
			debug_level++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		default:
			_usage(argv[0]);
			exit(1);
		}
}

static void _update_logging(void)
{

	/* Preserve execute line arguments (if any) */
	if (debug_level) {
		slurmsmwd_debug_level = MIN(
			(LOG_LEVEL_INFO + debug_level),
			(LOG_LEVEL_END - 1));
	}

	log_opts.stderr_level  = slurmsmwd_debug_level;
	log_opts.logfile_level = slurmsmwd_debug_level;
	log_opts.syslog_level  = slurmsmwd_debug_level;

	if (foreground)
		log_opts.syslog_level = LOG_LEVEL_QUIET;
	else {
		log_opts.stderr_level = LOG_LEVEL_QUIET;
		if (slurmsmwd_log_file)
			log_opts.syslog_level = LOG_LEVEL_QUIET;
	}

	log_alter(log_opts, SYSLOG_FACILITY_DAEMON, slurmsmwd_log_file);
	log_set_timefmt(slurmsmwd_log_fmt);
}

static void _reconfig(void)
{
	slurmsmwd_read_config();
	_update_logging();
}

/* Reset some signals to their default state to clear any
 * inherited signal states */
static void _default_sigaction(int sig)
{
	struct sigaction act;

	if (sigaction(sig, NULL, &act)) {
		error("sigaction(%d): %m", sig);
		return;
	}
	if (act.sa_handler != SIG_IGN)
		return;

	act.sa_handler = SIG_DFL;
	if (sigaction(sig, &act, NULL))
		error("sigaction(%d): %m", sig);
}

/* _signal_handler - Process daemon-wide signals */
static void *_signal_handler(void *no_data)
{
	int rc, sig;
	int sig_array[] = {SIGINT, SIGTERM, SIGHUP, SIGABRT, 0};
	sigset_t set;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	/* Make sure no required signals are ignored (possibly inherited) */
	_default_sigaction(SIGINT);
	_default_sigaction(SIGTERM);
	_default_sigaction(SIGHUP);
	_default_sigaction(SIGABRT);

	while (1) {
		xsignal_sigset_create(sig_array, &set);
		rc = sigwait(&set, &sig);
		if (rc == EINTR)
			continue;
		switch (sig) {
		case SIGHUP:	/* kill -1 */
			info("Reconfigure signal (SIGHUP) received");
			_reconfig();
			break;
		case SIGINT:	/* kill -2  or <CTRL-C> */
		case SIGTERM:	/* kill -15 */
			info("Terminate signal (SIGINT or SIGTERM) received");
			_shutdown_threads();
			return NULL;	/* Normal termination */
		case SIGABRT:	/* abort */
			info("SIGABRT received");
			abort();	/* Should terminate here */
			_shutdown_threads();
			return NULL;
		default:
			error("Invalid signal (%d) received", sig);
		}
	}

}

int main(int argc, char **argv)
{
	pthread_t processing_thread, signal_handler_thread;
	pthread_attr_t thread_attr;

	_parse_commandline(argc, argv);

	log_init(argv[0], log_opts, LOG_DAEMON, NULL);
	_reconfig();
	slurmsmwd_print_config();

	if (!foreground) {
		if (xdaemon())
			error("daemon(): %m");
	}
	if (create_pidfile("/var/run/slurmsmwd.pid", 0) < 0)
		fatal("Unable to create pidfile /var/run/slurmswmd.pid");

	slurm_mutex_init(&down_node_lock);

	/* Create attached thread for signal handling */
	if (xsignal_block(_sigarray) < 0)
		error("Unable to block signals");
	slurm_attr_init(&thread_attr);
	if (pthread_create(&signal_handler_thread, &thread_attr,
			   _signal_handler, NULL))
		fatal("pthread_create %m");
	slurm_attr_destroy(&thread_attr);

	slurm_attr_init(&thread_attr);
	if (pthread_create(&processing_thread, &thread_attr,
			   &_process_data, NULL))
		fatal("pthread_create %m");
	slurm_attr_destroy(&thread_attr);

	while (!stop_running) {
		slurm_attr_init(&thread_attr);
		if (pthread_create(&xtc_thread, &thread_attr,
				   &_xtconsumer_listen, NULL))
			fatal("pthread_create %m");
		slurm_attr_destroy(&thread_attr);
		pthread_join(xtc_thread, NULL);
	}

	pthread_join(processing_thread, NULL);
	slurm_mutex_destroy(&down_node_lock);
	return 0;
}

static int _start_xtconsumer(char **xtc_argv, pid_t *pid)
{
	int cc, i;
	pid_t cpid;
	int pfd[2] = { -1, -1 };

	if (access(xtconsumer_path, R_OK | X_OK) < 0) {
		error("Can not execute: %s", xtconsumer_path);
		return -1;
	}
	if (pipe(pfd) != 0) {
		error("pipe(): %s", slurm_strerror(slurm_get_errno()));
		return -1;
	}

	if ((cpid = fork()) == 0) {
		cc = sysconf(_SC_OPEN_MAX);
		dup2(pfd[1], STDERR_FILENO);
		dup2(pfd[1], STDOUT_FILENO);
		for (i = 0; i < cc; i++) {
			if ((i != STDERR_FILENO) && (i != STDOUT_FILENO))
				close(i);
		}
		setpgid(0, 0);
		execv(xtconsumer_path, xtc_argv);
		error("execv(): %s", slurm_strerror(slurm_get_errno()));
		_exit(127);
	} else if (cpid < 0) {
		close(pfd[0]);
		close(pfd[1]);
		error("fork(): %s", slurm_strerror(slurm_get_errno()));
		return -1;
	}
	*pid = cpid;
	close(pfd[1]);
	return pfd[0];
}
