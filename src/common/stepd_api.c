/*****************************************************************************\
 *  src/common/stepd_api.c - slurmstepd message API
 *****************************************************************************
 *  Copyright (C) 2005-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include <dirent.h>
#include <grp.h>
#include <inttypes.h>
#include <netdb.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/stepd_api.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xregex.h"
#include "src/common/xstring.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/conn.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/jobacct_gather.h"

strong_alias(stepd_available, slurm_stepd_available);
strong_alias(stepd_connect, slurm_stepd_connect);
strong_alias(stepd_get_uid, slurm_stepd_get_uid);
strong_alias(stepd_add_extern_pid, slurm_stepd_add_extern_pid);
strong_alias(stepd_get_x11_display, slurm_stepd_get_x11_display);
strong_alias(stepd_getpw, slurm_stepd_getpw);
strong_alias(xfree_struct_passwd, slurm_xfree_struct_passwd);
strong_alias(stepd_getgr, slurm_stepd_getgr);
strong_alias(xfree_struct_group_array, slurm_xfree_struct_group_array);
strong_alias(stepd_gethostbyname, slurm_stepd_gethostbyname);
strong_alias(xfree_struct_hostent, slurm_xfree_struct_hostent);
strong_alias(stepd_get_namespace_fd, slurm_stepd_get_namespace_fd);

/*
 * Should be called when a connect() to a socket returns ECONNREFUSED.
 * Presumably the ECONNREFUSED means that nothing is attached to the listening
 * side of the unix domain socket.
 * If the socket is at least 10 minutes old, then unlink it.
 */
static void
_handle_stray_socket(const char *socket_name)
{
	struct stat buf;
	uid_t uid;
	time_t now;

	/* Only attempt to remove the stale socket if process is running
	   as root or the SlurmdUser. */
	if (getuid() && (getuid() != slurm_conf.slurmd_user_id))
		return;

	if (stat(socket_name, &buf) == -1) {
		debug3("_handle_stray_socket: unable to stat %s: %m",
			socket_name);
		return;
	}

	if ((uid = getuid()) != buf.st_uid) {
		debug3("_handle_stray_socket: socket %s is not owned by uid %u",
		       socket_name, uid);
		return;
	}

	now = time(NULL);
	if ((now - buf.st_mtime) > 600) {
		/* remove the socket */
		if (unlink(socket_name) == -1) {
			if (errno != ENOENT) {
				error("_handle_stray_socket: unable to clean up"
				      " stray socket %s: %m", socket_name);
			}
		} else {
			debug("Cleaned up stray socket %s", socket_name);
		}
	}
}

static void _handle_stray_script(const char *directory,
				 slurm_step_id_t *step_id)
{
	char *dir_path = NULL, *file_path = NULL;

	if (step_id->step_id != SLURM_BATCH_SCRIPT)
		return;

	xstrfmtcat(dir_path, "%s/job%05u", directory, step_id->job_id);
	xstrfmtcat(file_path, "%s/slurm_script", dir_path);
	info("%s: Purging vestigial job script %s", __func__, file_path);
	(void) unlink(file_path);
	(void) rmdir(dir_path);

	xfree(dir_path);
	xfree(file_path);
}

static int
_step_connect(const char *directory, const char *nodename,
	      slurm_step_id_t *step_id)
{
	int fd;
	int rc;
	char *name = NULL, *pos = NULL;

	xstrfmtcatat(name, &pos, "%s/%s_%u.%u", directory, nodename,
		     step_id->job_id, step_id->step_id);
	if (step_id->step_het_comp != NO_VAL)
		xstrfmtcatat(name, &pos, ".%u", step_id->step_het_comp);

	if ((rc = slurm_open_unix_stream(name, 0, &fd))) {
		/* Can indicate race condition at step termination */
		debug("%s: failed for %s: %s",
		      __func__, name, slurm_strerror(rc));
		if (errno == ECONNREFUSED && running_in_slurmd()) {
			_handle_stray_socket(name);
			_handle_stray_script(directory, step_id);
		}

		xfree(name);
		close(fd);
		return -1;
	}

	xfree(name);
	return fd;
}


static char *
_guess_nodename(void)
{
	char host[HOST_NAME_MAX];
	char *nodename = NULL;

	/* If we are in a step just grab it from the ENV */
	if ((nodename = getenv("SLURMD_NODENAME")))
		return xstrdup(nodename);

	if (gethostname_short(host, sizeof(host)) != 0)
		return NULL;
	nodename = slurm_conf_get_nodename(host);
	if (nodename == NULL)
		nodename = slurm_conf_get_aliased_nodename();
	if (nodename == NULL) /* if no match, try localhost */
		nodename = slurm_conf_get_nodename("localhost");
	/*
	 * If nothing above has given us a name, just return what
	 * gethostname_short. This is helpful for dynamic nodes.
	 */
	if (!nodename)
		nodename = xstrdup(host);

	return nodename;
}

/*
 * Connect to a slurmstepd process by way of its unix domain socket.
 *
 * Both "directory" and "nodename" may be null, in which case stepd_connect
 * will attempt to determine them on its own.  If you are using multiple
 * slurmd on one node (unusual outside of development environments), you
 * will get one of the local NodeNames more-or-less at random.
 *
 * Returns a file descriptor for the opened socket on success alongside the
 * protocol_version for the stepd, or -1 on error.
 */
extern int stepd_connect(const char *directory, const char *nodename,
			 slurm_step_id_t *step_id,
			 uint16_t *protocol_version)
{
	int req = SLURM_PROTOCOL_VERSION;
	int fd = -1;
	int rc;
	char *local_nodename = NULL;

	*protocol_version = 0;

	if (nodename == NULL) {
		if (!(local_nodename = _guess_nodename()))
			return -1;
		nodename = local_nodename;
	}
	if (directory == NULL) {
		slurm_conf_t *cf = slurm_conf_lock();
		directory = slurm_conf_expand_slurmd_path(cf->slurmd_spooldir,
							  nodename, NULL);
		slurm_conf_unlock();
	}

	/* Connect to the step */
	fd = _step_connect(directory, nodename, step_id);
	if (fd == -1)
		goto fail1;

	safe_write(fd, &req, sizeof(int));
	safe_read(fd, &rc, sizeof(int));
	if (rc < 0)
		goto rwfail;
	else if (rc)
		*protocol_version = rc;

	xfree(local_nodename);
	return fd;

rwfail:
	fd_close(&fd);
fail1:
	xfree(local_nodename);
	return fd;
}

extern sluid_t stepd_sluid(int fd, uint16_t protocol_version)
{
	int req = REQUEST_SLUID;
	sluid_t sluid = 0;

	safe_write(fd, &req, sizeof(int));
	safe_read(fd, &sluid, sizeof(sluid_t));
rwfail:
	return sluid;
}

/*
 * Retrieve a job step's current state.
 */
slurmstepd_state_t
stepd_state(int fd, uint16_t protocol_version)
{
	int req	= REQUEST_STATE;
	slurmstepd_state_t status = SLURMSTEPD_NOT_RUNNING;

	safe_write(fd, &req, sizeof(int));
	safe_read(fd, &status, sizeof(slurmstepd_state_t));
rwfail:
	return status;
}

/*
 * Send job notification message to a batch job
 */
int
stepd_notify_job(int fd, uint16_t protocol_version, char *message)
{
	int req = REQUEST_JOB_NOTIFY;
	int rc;

	safe_write(fd, &req, sizeof(int));
	if (message) {
		rc = strlen(message) + 1;
		safe_write(fd, &rc, sizeof(int));
		safe_write(fd, message, rc);
	} else {
		rc = 0;
		safe_write(fd, &rc, sizeof(int));
	}

	/* Receive the return code */
	safe_read(fd, &rc, sizeof(int));
	return rc;
 rwfail:
	return -1;
}

/*
 * Send a signal to the proctrack container of a job step.
 */
int stepd_signal_container(int fd, uint16_t protocol_version, int signal,
			   int flags, char *details, uid_t req_uid)
{
	int req = REQUEST_SIGNAL_CONTAINER, details_len = 0;
	int rc;
	int errnum = 0;

	safe_write(fd, &req, sizeof(int));
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_write(fd, &signal, sizeof(int));
		safe_write(fd, &flags, sizeof(int));
		if (details)
			details_len = strlen(details);
		safe_write(fd, &details_len, sizeof(int));
		safe_write(fd, details, details_len);
		safe_write(fd, &req_uid, sizeof(uid_t));
	} else {
		error("%s: invalid protocol_version %u",
		      __func__, protocol_version);
		goto rwfail;
	}

	/* Receive the return code and errno */
	safe_read(fd, &rc, sizeof(int));
	safe_read(fd, &errnum, sizeof(int));

	errno = errnum;
	return rc;
rwfail:
	return -1;
}

/*
 * Request to enter namespace of a job
 * -1 on error;
 */
extern int stepd_get_namespace_fd(int fd, uint16_t protocol_version)
{
	int req = REQUEST_GET_NS_FD;
	int ns_fd = 0;

	debug("entering %s", __func__);
	safe_write(fd, &req, sizeof(int));

	safe_read(fd, &ns_fd, sizeof(ns_fd));

	/*
	 * Receive the file descriptor of the namespace to be joined if valid fd
	 * is coming. Note that the number of ns_fd will not be the same
	 * returned from receive_fd_over_socket().  The number we got from the
	 * safe_read was the fd on the sender which will be different on our
	 * end.
	 */
	if (ns_fd > 0)
		ns_fd = receive_fd_over_socket(fd);

	return ns_fd;

rwfail:
	return -1;
}

/*
 * Request to get required information to enter the namespace of a job.
 *
 * On success returns number of elements in the fd_map list and populates the
 * list.
 * Returns SLURM_ERROR on failure
 */
extern int stepd_get_namespace_fds(int fd, list_t *fd_map,
				   uint16_t protocol_version)
{
	int req = REQUEST_GET_NS_FDS;
	unsigned int fd_count = 0;

	xassert(fd_map);

	debug("entering %s", __func__);
	safe_write(fd, &req, sizeof(req));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_read(fd, &fd_count, sizeof(fd_count));
		if (fd_count == 0)
			return fd_count;

		for (int i = 0; i < fd_count; i++) {
			ns_fd_map_t *tmp_map = xmalloc(sizeof(*tmp_map));
			safe_read(fd, &tmp_map->type, sizeof(tmp_map->type));
			tmp_map->fd = receive_fd_over_socket(fd);
			list_append(fd_map, tmp_map);
			tmp_map = NULL;
		}
	} else {
		error("%s: bad protocol version %hu",
		      __func__, protocol_version);
		goto rwfail;
	}

	return fd_count;

rwfail:
	list_destroy(fd_map);
	return SLURM_ERROR;
}

/*
 * Retrieves the BPF token fd from the connected socket, this socket needs to be
 * connected to the external slurmstepd as it is the only one capable of
 * providing it.
 *
 * Return SLURM_ERROR on failure and the BPF token fd on success.
 */
extern int stepd_get_bpf_token(int fd, uint16_t protocol_version)
{
	int req = REQUEST_GET_BPF_TOKEN;
	int token_fd = -1, bpf_fd = -1;
	int rc;

#ifndef HAVE_BPF_TOKENS
	error("Cannot request a BPF token as slurm is not compiled with support for it");
	return SLURM_ERROR;
#endif

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_write(fd, &req, sizeof(req));

		/*
		 * Receive rc that indicates whether token needs to be
		 * generated or not.
		 */
		safe_read(fd, &rc, sizeof(rc));
		if (rc == SLURM_ERROR) {
			error("Contacted a non-external step");
			goto rwfail;
		}

		if (rc == 0) { /* BPF token is generated */
			/* Receive bpf token fd */
			token_fd = receive_fd_over_socket(fd);
			if (token_fd < 0) {
				error("Problems receiving the BPF token fd");
				goto rwfail;
			}
		} else { /* Generate BPF token */

			/* Call fsopen for "bpf" */
			bpf_fd = cgroup_g_bpf_fsopen();
			if (bpf_fd < 0) {
				rc = SLURM_ERROR;
				safe_write(fd, &rc, sizeof(int));
				error("bpf fsopen failure");
				goto rwfail;
			}

			/* Send rc for fsopen and the fd itself */
			rc = SLURM_SUCCESS;
			safe_write(fd, &rc, sizeof(int));
			send_fd_over_socket(fd, bpf_fd);

			/* Receive rc indicating fsconfig success */
			safe_read(fd, &rc, sizeof(rc));
			if (rc != SLURM_SUCCESS) {
				error("bpf fsconfig failure");
				goto rwfail;
			}

			/* BPF token generation */
			token_fd = cgroup_g_bpf_create_token(bpf_fd);
			if (token_fd == SLURM_ERROR) {
				rc = SLURM_ERROR;
				safe_write(fd, &rc, sizeof(int));
				goto rwfail;
			}

			/* Send RC for token create as well as the token fd */
			rc = SLURM_SUCCESS;
			safe_write(fd, &rc, sizeof(int));
			send_fd_over_socket(fd, token_fd);

			/* This indicates proper token fd receive */
			safe_read(fd, &rc, sizeof(rc));
			if (rc != SLURM_SUCCESS) {
				error("Problems sending the bpf token fd");
				goto rwfail;
			}
		}
	} else {
		error("%s: bad protocol version %hu",
		      __func__, protocol_version);
		goto rwfail;
	}

	close(bpf_fd);
	return token_fd;

rwfail:
	if (bpf_fd > 0)
		close(bpf_fd);
	if (token_fd > 0)
		close(token_fd);
	return SLURM_ERROR;
}

/*
 * Attach a client to a running job step.
 *
 * On success returns SLURM_SUCCESS and fills in resp->local_pids,
 * resp->gtids, resp->ntasks, and resp->executable.
 */
extern int stepd_attach(int fd, uint16_t protocol_version, slurm_addr_t *ioaddr,
			slurm_addr_t *respaddr, char *cert, char *io_key,
			uid_t uid, reattach_tasks_response_msg_t *resp)
{
	int req = REQUEST_ATTACH;
	uint32_t io_key_len = strlen(io_key) + 1;
	uint32_t cert_len;
	int rc = SLURM_SUCCESS;

	if (protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		safe_write(fd, &req, sizeof(int)); /* needs to be first */

		if (cert) {
			cert_len = strlen(cert) + 1;
			safe_write(fd, &cert_len, sizeof(uint32_t));
			safe_write(fd, cert, cert_len);
		} else {
			cert_len = 0;
			safe_write(fd, &cert_len, sizeof(uint32_t));
		}

		safe_write(fd, ioaddr, sizeof(slurm_addr_t));
		safe_write(fd, respaddr, sizeof(slurm_addr_t));
		safe_write(fd, &io_key_len, sizeof(uint32_t));
		safe_write(fd, io_key, io_key_len);
		safe_write(fd, &uid, sizeof(uid_t));
		safe_write(fd, &protocol_version, sizeof(uint16_t));
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_write(fd, &req, sizeof(int));
		safe_write(fd, ioaddr, sizeof(slurm_addr_t));
		safe_write(fd, respaddr, sizeof(slurm_addr_t));
		safe_write(fd, &io_key_len, sizeof(uint32_t));
		safe_write(fd, io_key, io_key_len);
		safe_write(fd, &uid, sizeof(uid_t));
		safe_write(fd, &protocol_version, sizeof(uint16_t));
	} else
		goto rwfail;

	/* Receive the return code */
	safe_read(fd, &rc, sizeof(int));

	if (rc == SLURM_SUCCESS) {
		/* Receive response info */
		uint32_t ntasks;
		int len, i;

		safe_read(fd, &ntasks, sizeof(uint32_t));
		resp->ntasks = ntasks;
		len = ntasks * sizeof(uint32_t);

		resp->local_pids = xcalloc(ntasks, sizeof(uint32_t));
		safe_read(fd, resp->local_pids, len);

		resp->gtids = xcalloc(ntasks, sizeof(uint32_t));
		safe_read(fd, resp->gtids, len);

		resp->executable_names = xcalloc(ntasks, sizeof(char *));
		for (i = 0; i < ntasks; i++) {
			safe_read(fd, &len, sizeof(int));
			resp->executable_names[i] = xmalloc(len);
			safe_read(fd, resp->executable_names[i], len);
		}
	}

	return rc;
rwfail:
	return SLURM_ERROR;
}

static void
_free_step_loc_t(step_loc_t *loc)
{
	if (loc->directory)
		xfree(loc->directory);
	if (loc->nodename)
		xfree(loc->nodename);
	xfree(loc);
}

static int
_sockname_regex_init(regex_t *re, const char *nodename)
{
	char *pattern = NULL;
	int rc;

	xstrcat(pattern, "^");
	xstrcat(pattern, nodename);
	xstrcat(pattern,
		"_([[:digit:]]*)\\.([[:digit:]]*)\\.{0,1}([[:digit:]]*)$");

	if ((rc = regcomp(re, pattern, REG_EXTENDED))) {
		dump_regex_error(rc, re,
				 "sockname regex \"%s\" compilation failed",
				 pattern);
		return -1;
	}

	xfree(pattern);

	return 0;
}

static int
_sockname_regex(regex_t *re, const char *filename, slurm_step_id_t *step_id)
{
	size_t nmatch = 5;
	regmatch_t pmatch[5];
	char *match;
	size_t my_size;
	int rc;

	xassert(step_id);
	*step_id = SLURM_STEP_ID_INITIALIZER;

	memset(pmatch, 0, sizeof(regmatch_t)*nmatch);
	if ((rc = regexec(re, filename, nmatch, pmatch, 0))) {
		if (rc != REG_NOMATCH)
			dump_regex_error(rc, re, "regexc(%s)", filename);
		return -1;
	}

	match = xstrndup(filename + pmatch[1].rm_so,
			(size_t)(pmatch[1].rm_eo - pmatch[1].rm_so));
	step_id->job_id = slurm_atoul(match);
	xfree(match);

	match = xstrndup(filename + pmatch[2].rm_so,
			(size_t)(pmatch[2].rm_eo - pmatch[2].rm_so));
	step_id->step_id = slurm_atoul(match);
	xfree(match);

	/* If we have a size here we have a het_comp */
	if ((my_size = pmatch[3].rm_eo - pmatch[3].rm_so)) {
		match = xstrndup(filename + pmatch[3].rm_so, my_size);
		step_id->step_het_comp = slurm_atoul(match);
		xfree(match);
	}

	return 0;
}

/*
 * Scan for available running slurm step daemons by checking
 * "directory" for unix domain sockets with names beginning in "nodename".
 *
 * Both "directory" and "nodename" may be null, in which case stepd_available
 * will attempt to determine them on its own.  If you are using multiple
 * slurmd on one node (unusual outside of development environments), you
 * will get one of the local NodeNames more-or-less at random.
 *
 * Returns a list of pointers to step_loc_t structures.
 */
extern list_t *stepd_available(const char *directory, const char *nodename)
{
	list_t *l = NULL;
	DIR *dp;
	struct dirent *ent;
	regex_t re;
	struct stat stat_buf;
	char *local_nodename = NULL;

	if (nodename == NULL) {
		if (!(local_nodename = _guess_nodename())) {
			error("%s: Couldn't find nodename", __func__);
			return NULL;
		}
		nodename = local_nodename;
	}
	if (directory == NULL) {
		slurm_conf_t *cf = slurm_conf_lock();
		directory = slurm_conf_expand_slurmd_path(
			cf->slurmd_spooldir, nodename, NULL);
		slurm_conf_unlock();
	}

	l = list_create((ListDelF) _free_step_loc_t);
	if (_sockname_regex_init(&re, nodename) == -1)
		goto done;

	/*
	 * Make sure that "directory" exists and is a directory.
	 */
	if (stat(directory, &stat_buf) < 0) {
		error("Domain socket directory %s: %m", directory);
		goto done;
	} else if (!S_ISDIR(stat_buf.st_mode)) {
		error("%s is not a directory", directory);
		goto done;
	}

	if ((dp = opendir(directory)) == NULL) {
		error("Unable to open directory: %m");
		goto done;
	}

	while ((ent = readdir(dp)) != NULL) {
		step_loc_t *loc;
		slurm_step_id_t step_id;

		if (!_sockname_regex(&re, ent->d_name, &step_id)) {
			debug4("found %ps", &step_id);
			loc = xmalloc(sizeof(step_loc_t));
			loc->directory = xstrdup(directory);
			loc->nodename = xstrdup(nodename);
			loc->step_id = step_id;
			list_append(l, (void *)loc);
		}
	}

	closedir(dp);
done:
	xfree(local_nodename);
	regfree(&re);
	return l;
}

/*
 * Send the termination signal to all of the unix domain socket files
 * for a given directory and nodename, and then unlink the files.
 * Returns SLURM_ERROR if any sockets could not be unlinked.
 */
int
stepd_cleanup_sockets(const char *directory, const char *nodename)
{
	DIR *dp;
	struct dirent *ent;
	regex_t re;
	struct stat stat_buf;
	int rc = SLURM_SUCCESS;

	_sockname_regex_init(&re, nodename);

	/*
	 * Make sure that "directory" exists and is a directory.
	 */
	if (stat(directory, &stat_buf) < 0) {
		error("Domain socket directory %s: %m", directory);
		goto done;
	} else if (!S_ISDIR(stat_buf.st_mode)) {
		error("%s is not a directory", directory);
		goto done;
	}

	if ((dp = opendir(directory)) == NULL) {
		error("Unable to open directory: %m");
		goto done;
	}

	while ((ent = readdir(dp)) != NULL) {
		slurm_step_id_t step_id;
		if (!_sockname_regex(&re, ent->d_name, &step_id)) {
			char *path;
			int fd;
			uint16_t protocol_version;

			path = NULL;
			xstrfmtcat(path, "%s/%s", directory, ent->d_name);

			verbose("Cleaning up stray %ps", &step_id);

			/* signal the slurmstepd to terminate its step */
			fd = stepd_connect((char *) directory,
					   (char *) nodename,
					   &step_id,
					   &protocol_version);
			if (fd == -1) {
				debug("Unable to connect to socket %s", path);
			} else {
				if (stepd_signal_container(
					    fd, protocol_version, SIGKILL, 0,
					    NULL, getuid())
				    == -1) {
					debug("Error sending SIGKILL to %ps",
					      &step_id);
				}
				close(fd);
			}

			/* make sure that the socket has been removed */
			if (unlink(path) == -1 && errno != ENOENT) {
				error("Unable to clean up stray socket %s: %m",
				      path);
				rc = SLURM_ERROR;
			}
			xfree(path);
		}
	}

	closedir(dp);
done:
	regfree(&re);
	return rc;
}

/*
 * Return true if the process with process ID "pid" is found in
 * the proctrack container of the slurmstepd "step".
 */
bool
stepd_pid_in_container(int fd, uint16_t protocol_version, pid_t pid)
{
	int req = REQUEST_PID_IN_CONTAINER;
	bool rc;

	safe_write(fd, &req, sizeof(int));
	safe_write(fd, &pid, sizeof(pid_t));

	/* Receive the return code */
	safe_read(fd, &rc, sizeof(bool));

	debug("Leaving stepd_pid_in_container");
	return rc;
rwfail:
	return false;
}

/*
 * Add a pid to the "extern" step of a job, meaning add it to the
 * jobacct_gather and proctrack plugins.
 */
extern int stepd_add_extern_pid(int fd, uint16_t protocol_version, pid_t pid)
{
	int req = REQUEST_ADD_EXTERN_PID;
	int rc;

	safe_write(fd, &req, sizeof(int));
	safe_write(fd, &pid, sizeof(pid_t));

	/* Receive the return code */
	safe_read(fd, &rc, sizeof(int));

	debug("Leaving stepd_add_extern_pid");
	return rc;
rwfail:
	return SLURM_ERROR;
}

extern int stepd_get_x11_display(int fd, uint16_t protocol_version,
				 char **xauthority)
{
	int req = REQUEST_X11_DISPLAY;
	int display = 0, len = 0;

	*xauthority = NULL;

	safe_write(fd, &req, sizeof(int));

	/*
	 * Receive the display number,
	 * or zero if x11 forwarding is not setup
	 */
	safe_read(fd, &display, sizeof(int));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_read(fd, &len, sizeof(int));
		if (len) {
			*xauthority = xmalloc(len);
			safe_read(fd, *xauthority, len);
		}
	}

	debug("Leaving stepd_get_x11_display");
	return display;

rwfail:
	return 0;
}

/*
 *
 */
extern struct passwd *stepd_getpw(int fd, uint16_t protocol_version,
				  int mode, uid_t uid, const char *name)
{
	int req = REQUEST_GETPW;
	int found = 0;
	int len = 0;
	struct passwd *pwd = xmalloc(sizeof(struct passwd));

	safe_write(fd, &req, sizeof(int));

	safe_write(fd, &mode, sizeof(int));

	safe_write(fd, &uid, sizeof(uid_t));
	if (name) {
		len = strlen(name);
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, name, len);
	} else {
		safe_write(fd, &len, sizeof(int));
	}

	safe_read(fd, &found, sizeof(int));

	if (!found) {
		xfree(pwd);
		return NULL;
	}

	safe_read(fd, &len, sizeof(int));
	pwd->pw_name = xmalloc(len + 1);
	safe_read(fd, pwd->pw_name, len);

	safe_read(fd, &len, sizeof(int));
	pwd->pw_passwd = xmalloc(len + 1);
	safe_read(fd, pwd->pw_passwd, len);

	safe_read(fd, &pwd->pw_uid, sizeof(uid_t));
	safe_read(fd, &pwd->pw_gid, sizeof(gid_t));

	safe_read(fd, &len, sizeof(int));
	pwd->pw_gecos = xmalloc(len + 1);
	safe_read(fd, pwd->pw_gecos, len);

	safe_read(fd, &len, sizeof(int));
	pwd->pw_dir = xmalloc(len + 1);
	safe_read(fd, pwd->pw_dir, len);

	safe_read(fd, &len, sizeof(int));
	pwd->pw_shell = xmalloc(len + 1);
	safe_read(fd, pwd->pw_shell, len);

	debug("Leaving %s", __func__);
	return pwd;

rwfail:
	xfree_struct_passwd(pwd);
	return NULL;
}

extern void xfree_struct_passwd(struct passwd *pwd)
{
	if (!pwd)
		return;

	xfree(pwd->pw_name);
	xfree(pwd->pw_passwd);
	xfree(pwd->pw_gecos);
	xfree(pwd->pw_dir);
	xfree(pwd->pw_shell);
	xfree(pwd);
}

extern struct group **stepd_getgr(int fd, uint16_t protocol_version,
				  int mode, gid_t gid, const char *name)
{
	int req = REQUEST_GETGR;
	int found = 0;
	int len = 0;
	struct group **grps = NULL;

	safe_write(fd, &req, sizeof(int));

	safe_write(fd, &mode, sizeof(int));

	safe_write(fd, &gid, sizeof(gid_t));
	if (name) {
		len = strlen(name);
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, name, len);
	} else {
		safe_write(fd, &len, sizeof(int));
	}

	safe_read(fd, &found, sizeof(int));

	if (!found)
		return NULL;

	/* Add space for NULL termination of the array */
	grps = xcalloc(found + 1, sizeof(struct group *));

	for (int i = 0; i < found; i++) {
		grps[i] = xmalloc(sizeof(struct group));

		safe_read(fd, &len, sizeof(int));
		grps[i]->gr_name = xmalloc(len + 1);
		safe_read(fd, grps[i]->gr_name, len);

		safe_read(fd, &len, sizeof(int));
		grps[i]->gr_passwd = xmalloc(len + 1);
		safe_read(fd, grps[i]->gr_passwd, len);

		safe_read(fd, &grps[i]->gr_gid, sizeof(gid_t));

		/*
		 * In the current implementation, we define each group to
		 * only have a single member - that of the user running the
		 * job. (Since gr_mem is a NULL terminated array, allocate
		 * space for two elements.)
		 */
		grps[i]->gr_mem = xcalloc(2, sizeof(char *));
		safe_read(fd, &len, sizeof(int));
		grps[i]->gr_mem[0] = xmalloc(len + 1);
		safe_read(fd, grps[i]->gr_mem[0], len);
	}
	debug("Leaving %s", __func__);
	return grps;

rwfail:
	xfree_struct_group_array(grps);
	return NULL;
}

extern void xfree_struct_group_array(struct group **grps)
{
	for (int i = 0; grps && grps[i]; i++) {
		xfree(grps[i]->gr_name);
		xfree(grps[i]->gr_passwd);
		xfree(grps[i]->gr_mem[0]);
		xfree(grps[i]->gr_mem);
		xfree(grps[i]);
	}
	xfree(grps);
}

extern struct hostent *stepd_gethostbyname(int fd, uint16_t protocol_version,
					   int mode, const char *nodename)
{
	int req = REQUEST_GETHOST;
	int found = 0;
	int len = 0;
	int cnt = 0;
	struct hostent *host = NULL;

	safe_write(fd, &req, sizeof(int));

	safe_write(fd, &mode, sizeof(int));

	if (nodename) {
		len = strlen(nodename);
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, nodename, len);
	} else {
		safe_write(fd, &len, sizeof(int));
	}

	safe_read(fd, &found, sizeof(int));

	if (!found)
		return NULL;

	host = xmalloc(sizeof(struct hostent));

	safe_read(fd, &len, sizeof(int));
	host->h_name = xmalloc(len + 1);
	safe_read(fd, host->h_name, len);

	safe_read(fd, &cnt, sizeof(int));
	host->h_aliases = xcalloc(cnt + 1, sizeof(char *));
	for (int i = 0; i < cnt; i++) {
		safe_read(fd, &len, sizeof(int));
		host->h_aliases[i] = xmalloc(len + 1);
		safe_read(fd, host->h_aliases[i], len);
	}
	safe_read(fd, &host->h_addrtype, sizeof(int));
	safe_read(fd, &len, sizeof(int));
	host->h_length = len;

	/*
	 * In the current implementation, we define each host to
	 * only have a single address.
	 * (Since h_addr_list is a NULL terminated array, allocate
	 * space for two elements.)
	 */
	host->h_addr_list = xcalloc(2, sizeof(char *));
	host->h_addr_list[0] = xmalloc(len);
	safe_read(fd, host->h_addr_list[0], len);

	debug("Leaving %s", __func__);
	return host;

rwfail:
	xfree_struct_hostent(host);
	return NULL;

}

extern void xfree_struct_hostent(struct hostent *host)
{
	if (!host)
		return;
	xfree(host->h_name);
	for (int i = 0; host->h_aliases && host->h_aliases[i];
	     i++) {
		xfree(host->h_aliases[i]);
	}
	xfree(host->h_aliases);
	if (host->h_addr_list) {
		xfree(host->h_addr_list[0]);
		xfree(host->h_addr_list);
	}
	xfree(host);
}

/*
 * Return the process ID of the slurmstepd.
 */
pid_t
stepd_daemon_pid(int fd, uint16_t protocol_version)
{
	int req	= REQUEST_DAEMON_PID;
	pid_t pid;

	safe_write(fd, &req, sizeof(int));
	safe_read(fd, &pid, sizeof(pid_t));

	return pid;
rwfail:
	return (pid_t)-1;
}

/*
 * Suspend execution of the job step.  Only root or SlurmUser is
 * authorized to use this call. Since this activity includes a 'sleep 1'
 * in the slurmstepd, initiate the "suspend" in parallel.
 *
 * Returns SLURM_SUCCESS is successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
extern int
stepd_suspend(int fd, uint16_t protocol_version,
	      suspend_int_msg_t *susp_req, int phase)
{
	int req = REQUEST_STEP_SUSPEND;
	int rc = 0;
	int errnum = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (phase == 0) {
			safe_write(fd, &req, sizeof(int));
		} else {
			/* Receive the return code and errno */
			safe_read(fd, &rc, sizeof(int));
			safe_read(fd, &errnum, sizeof(int));
			errno = errnum;
		}
	}

	return rc;
rwfail:
	return -1;
}

/*
 * Resume execution of the job step that has been suspended by a
 * call to stepd_suspend().  Only root or SlurmUser is
 * authorized to use this call.
 *
 * Returns SLURM_SUCCESS is successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
extern int
stepd_resume(int fd, uint16_t protocol_version,
	     suspend_int_msg_t *susp_req, int phase)
{
	int req = REQUEST_STEP_RESUME;
	int rc = 0;
	int errnum = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (phase == 0) {
			safe_write(fd, &req, sizeof(int));
		} else {
			/* Receive the return code and errno */
			safe_read(fd, &rc, sizeof(int));
			safe_read(fd, &errnum, sizeof(int));
			errno = errnum;
		}
	}

	return rc;
rwfail:
	return -1;
}

extern int stepd_reconfig(int fd, uint16_t protocol_version, buf_t *reconf)
{
	int req = REQUEST_STEP_RECONFIGURE;
	int rc;
	int errnum = 0;

	safe_write(fd, &req, sizeof(int));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		int len = 0;
		if (reconf) {
			len = get_buf_offset(reconf);
			safe_write(fd, &len, sizeof(int));
			safe_write(fd, get_buf_data(reconf), len);
		} else {
			safe_write(fd, &len, sizeof(int));
		}
	}

	/* Receive the return code and errno */
	safe_read(fd, &rc, sizeof(int));
	safe_read(fd, &errnum, sizeof(int));

	errno = errnum;
	return rc;
rwfail:
	return -1;
}

/*
 * Terminate the job step.
 *
 * Returns SLURM_SUCCESS is successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
int
stepd_terminate(int fd, uint16_t protocol_version)
{
	int req = REQUEST_STEP_TERMINATE;
	int rc;
	int errnum = 0;

	safe_write(fd, &req, sizeof(int));

	/* Receive the return code and errno */
	safe_read(fd, &rc, sizeof(int));
	safe_read(fd, &errnum, sizeof(int));

	errno = errnum;
	return rc;
rwfail:
	return -1;
}

/*
 *
 * Returns SLURM_SUCCESS if successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
int
stepd_completion(int fd, uint16_t protocol_version, step_complete_msg_t *sent)
{
	int req = REQUEST_STEP_COMPLETION;
	int rc;
	int errnum = 0;
	buf_t *buffer;
	int len = 0;

	buffer = init_buf(0);

	debug("Entering stepd_completion for %ps, range_first = %d, range_last = %d",
	      &sent->step_id, sent->range_first, sent->range_last);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_write(fd, &req, sizeof(int));
		safe_write(fd, &sent->range_first, sizeof(int));
		safe_write(fd, &sent->range_last, sizeof(int));
		safe_write(fd, &sent->step_rc, sizeof(int));
		safe_write(fd, &sent->step_id.step_id, sizeof(uint32_t));
		safe_write(fd, &sent->send_to_stepmgr, sizeof(bool));

		/*
		 * We must not use setinfo over a pipe with slurmstepd here
		 * Indeed, slurmd does a large use of getinfo over a pipe
		 * with slurmstepd and doing the reverse can result in
		 * a deadlock scenario with slurmstepd :
		 * slurmd(lockforread,write)/slurmstepd(write,lockforread)
		 * Do pack/unpack instead to be sure of independances of
		 * slurmd and slurmstepd
		 */
		jobacctinfo_pack(sent->jobacct, protocol_version,
				 PROTOCOL_TYPE_SLURM, buffer);
		len = get_buf_offset(buffer);
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, get_buf_data(buffer), len);
		FREE_NULL_BUFFER(buffer);

		/* Receive the return code and errno */
		safe_read(fd, &rc, sizeof(int));
		safe_read(fd, &errnum, sizeof(int));
	} else {
		error("%s: bad protocol version %hu",
		      __func__, protocol_version);
		rc = SLURM_ERROR;
	}

	errno = errnum;
	return rc;

rwfail:
	FREE_NULL_BUFFER(buffer);
	return -1;
}

/*
 *
 * Returns jobacctinfo_t struct on success, NULL on error.
 * jobacctinfo_t must be freed after calling this function.
 */
int
stepd_stat_jobacct(int fd, uint16_t protocol_version,
		   slurm_step_id_t *sent, job_step_stat_t *resp)
{
	int req = REQUEST_STEP_STAT;
	int rc = SLURM_SUCCESS;
	int tasks = 0;

	/* NULL return indicates that accounting is disabled */
	if (!(resp->jobacct = jobacctinfo_create(NULL)))
		return rc;

	debug("Entering %s for %ps", __func__, sent);

	safe_write(fd, &req, sizeof(int));

	/* Do not attempt reading data until there is something to read.
	 * Avoid locking the jobacct_gather plugin early and creating
	 * possible deadlock. */
	if (wait_fd_readable(fd, 300))
		goto rwfail;

	/* Fill in the jobacct struct and return */
	rc = jobacctinfo_getinfo(resp->jobacct, JOBACCT_DATA_PIPE, &fd,
				 protocol_version);

	safe_read(fd, &tasks, sizeof(int));
	resp->num_tasks = tasks;

	return rc;
rwfail:
	error("gathering job accounting: %d", rc);
	jobacctinfo_destroy(resp->jobacct);
	resp->jobacct = NULL;
	return rc;
}

/*
 * List all of task process IDs and their local and global Slurm IDs.
 *
 * Returns SLURM_SUCCESS on success.  On error returns SLURM_ERROR
 * and sets errno.
 */
int
stepd_task_info(int fd, uint16_t protocol_version,
		slurmstepd_task_info_t **task_info,
		uint32_t *task_info_count)
{
	int req = REQUEST_STEP_TASK_INFO;
	slurmstepd_task_info_t *task = NULL;
	uint32_t ntasks;
	int i;

	safe_write(fd, &req, sizeof(int));

	safe_read(fd, &ntasks, sizeof(uint32_t));
	task = xcalloc(ntasks, sizeof(slurmstepd_task_info_t));
	for (i = 0; i < ntasks; i++) {
		safe_read(fd, &(task[i].id), sizeof(int));
		safe_read(fd, &(task[i].gtid), sizeof(uint32_t));
		safe_read(fd, &(task[i].pid), sizeof(pid_t));
		safe_read(fd, &(task[i].exited), sizeof(bool));
		safe_read(fd, &(task[i].estatus), sizeof(int));
	}

	if (ntasks == 0) {
		xfree(task);
		*task_info_count = 0;
		*task_info = NULL;
	} else {
		*task_info_count = ntasks;
		*task_info = task;
	}

	return SLURM_SUCCESS;
rwfail:
	xfree(task);
	*task_info_count = 0;
	*task_info = NULL;
	xfree(task);
	return SLURM_ERROR;
}

/*
 * List all of process IDs in the proctrack container.
 *
 * Returns SLURM_SUCCESS is successful.  On error returns SLURM_ERROR
 * and sets errno.
 */
int
stepd_list_pids(int fd, uint16_t protocol_version,
		uint32_t **pids_array, uint32_t *pids_count)
{
	int req = REQUEST_STEP_LIST_PIDS;
	uint32_t npids;
	uint32_t *pids = NULL;
	int i;

	safe_write(fd, &req, sizeof(int));

	/* read the pid list */
	safe_read(fd, &npids, sizeof(uint32_t));
	pids = xcalloc(npids, sizeof(uint32_t));
	for (i = 0; i < npids; i++) {
		safe_read(fd, &pids[i], sizeof(uint32_t));
	}

	if (npids == 0)
		xfree(pids);

	*pids_count = npids;
	*pids_array = pids;
	return SLURM_SUCCESS;

rwfail:
	xfree(pids);
	*pids_count = 0;
	*pids_array = NULL;
	return SLURM_ERROR;
}

/*
 * Get the memory limits of the step
 * Returns uid of the running step if successful.  On error returns -1.
 */
extern int stepd_get_mem_limits(int fd, uint16_t protocol_version,
				slurmstepd_mem_info_t *stepd_mem_info)
{
	int req = REQUEST_STEP_MEM_LIMITS;

	xassert(stepd_mem_info);
	memset(stepd_mem_info, 0, sizeof(slurmstepd_mem_info_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_write(fd, &req, sizeof(int));

		safe_read(fd, &stepd_mem_info->job_mem_limit, sizeof(uint32_t));
		safe_read(fd, &stepd_mem_info->step_mem_limit,
			  sizeof(uint32_t));
	}

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

/*
 * Get the uid of the step
 * Returns uid of the running step if successful.  On error returns -1.
 *
 * FIXME: BUG: On Linux, uid_t is uint32_t but this can return -1.
 */
extern uid_t stepd_get_uid(int fd, uint16_t protocol_version)
{
	int req = REQUEST_STEP_UID;
	uid_t uid = -1;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_write(fd, &req, sizeof(int));

		safe_read(fd, &uid, sizeof(uid_t));
	}

	return uid;
rwfail:
	return -1;
}

/*
 * Get the nodeid of the stepd
 * Returns nodeid of the running stepd if successful.  On error returns NO_VAL.
 */
extern uint32_t stepd_get_nodeid(int fd, uint16_t protocol_version)
{
	int req = REQUEST_STEP_NODEID;
	uint32_t nodeid = NO_VAL;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_write(fd, &req, sizeof(int));

		safe_read(fd, &nodeid, sizeof(uid_t));
	}

	return nodeid;
rwfail:
	return NO_VAL;
}

extern int stepd_relay_msg(int fd, slurm_msg_t *msg, uint16_t protocol_version)
{
	int req = msg->msg_type;
	uint32_t buf_size;

	safe_write(fd, &req, sizeof(int));

	buf_size = get_buf_offset(msg->buffer) - msg->body_offset;

	safe_write(fd, &msg->protocol_version, sizeof(uint16_t));
	send_fd_over_socket(fd, conn_g_get_fd(msg->conn));
	safe_write(fd, &buf_size, sizeof(uint32_t));
	safe_write(fd, &msg->buffer->head[msg->body_offset], buf_size);

	return 0;

rwfail:
	return -1;
}
