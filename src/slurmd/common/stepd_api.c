/*****************************************************************************\
 *  src/slurmd/common/stepd_api.c - slurmstepd message API
 *  $Id: $
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <dirent.h>
#include <regex.h>
#include <inttypes.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/pack.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/list.h"
#include "src/slurmd/common/stepd_api.h"

static int
step_connect(step_loc_t step)
{
	int fd;
	int len;
	struct sockaddr_un addr;
	char *name = NULL;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	xstrfmtcat(name, "%s/%s_%u.%u", step.directory, step.nodename, 
		   step.jobid, step.stepid);
	strcpy(addr.sun_path, name);
	len = strlen(addr.sun_path) + sizeof(addr.sun_family);

	if (connect(fd, (struct sockaddr *) &addr, len) < 0) {
		xfree(name);
		close(fd);
		return -1;
	}

	xfree(name);
	return fd;
}

slurmstepd_state_t
stepd_state(step_loc_t step)
{
	int req	= REQUEST_STATE;
	int fd;
	slurmstepd_state_t status = SLURMSTEPD_NOT_RUNNING;

	fd = step_connect(step);
	if (fd == -1)
		return status;

	safe_write(fd, &req, sizeof(int));
	safe_read(fd, &status, sizeof(slurmstepd_state_t));

rwfail:
	close(fd);
	return status;
}

/*
 * Send a signal to the process group of a job step.
 */
int
stepd_signal(step_loc_t step, void *auth_cred, int signal)
{
	int req = REQUEST_SIGNAL_PROCESS_GROUP;
	int fd;
	Buf buf;
	int buf_len;
	int rc;

	fd = step_connect(step);
	if (fd == -1)
		return -1;
	safe_write(fd, &req, sizeof(int));

	/* pack auth credential */
	buf = init_buf(0);
	g_slurm_auth_pack(auth_cred, buf);
	buf_len = size_buf(buf);
	debug("buf_len = %d", buf_len);

	safe_write(fd, &signal, sizeof(int));
	safe_write(fd, &buf_len, sizeof(int));
	safe_write(fd, get_buf_data(buf), buf_len);

	/* Receive the return code */
	safe_read(fd, &rc, sizeof(int));

	free_buf(buf);
	close(fd);
	return rc;
rwfail:
	close(fd);
	return -1;
}

/*
 * Send a signal to a single task in a job step.
 */
int
stepd_signal_task_local(step_loc_t step, void *auth_cred,
			int signal, int ltaskid)
{
	int req = REQUEST_SIGNAL_PROCESS_GROUP;
	int fd;
	Buf buf;
	int buf_len;
	int rc;

	fd = step_connect(step);
	if (fd == -1)
		return -1;
	safe_write(fd, &req, sizeof(int));

	/* pack auth credential */
	buf = init_buf(0);
	g_slurm_auth_pack(auth_cred, buf);
	buf_len = size_buf(buf);
	debug("buf_len = %d", buf_len);

	safe_write(fd, &signal, sizeof(int));
	safe_write(fd, &ltaskid, sizeof(int));
	safe_write(fd, &buf_len, sizeof(int));
	safe_write(fd, get_buf_data(buf), buf_len);

	/* Receive the return code */
	safe_read(fd, &rc, sizeof(int));

	free_buf(buf);
	close(fd);
	return rc;
rwfail:
	close(fd);
	return -1;
}

/*
 * Send a signal to the proctrack container of a job step.
 */
int
stepd_signal_container(step_loc_t step, void *auth_cred, int signal)
{
	int req = REQUEST_SIGNAL_CONTAINER;
	int fd;
	Buf buf;
	int buf_len;
	int rc;

	fd = step_connect(step);
	if (fd == -1)
		return -1;
	safe_write(fd, &req, sizeof(int));

	/* pack auth credential */
	buf = init_buf(0);
	g_slurm_auth_pack(auth_cred, buf);
	buf_len = size_buf(buf);
	debug("buf_len = %d", buf_len);

	safe_write(fd, &signal, sizeof(int));
	safe_write(fd, &buf_len, sizeof(int));
	safe_write(fd, get_buf_data(buf), buf_len);

	/* Receive the return code */
	safe_read(fd, &rc, sizeof(int));

	free_buf(buf);
	close(fd);
	return rc;
rwfail:
	close(fd);
	return -1;
}


/*
 * Attach a client to a running job step.
 *
 * On success returns SLURM_SUCCESS and fills in resp->local_pids,
 * resp->gtids, resp->ntasks, and resp->executable.
 */
int
stepd_attach(step_loc_t step, slurm_addr *ioaddr, slurm_addr *respaddr,
	     void *auth_cred, slurm_cred_t job_cred,
	     reattach_tasks_response_msg_t  *resp)
{
	int req = REQUEST_ATTACH;
	int fd;
	Buf buf;
	int buf_len;
	int rc = SLURM_SUCCESS;

	fd = step_connect(step);
	if (fd == -1)
		return SLURM_ERROR;
	safe_write(fd, &req, sizeof(int));

	/* pack auth and job credentials */
	buf = init_buf(0);
	g_slurm_auth_pack(auth_cred, buf);
	slurm_cred_pack(job_cred, buf);
	buf_len = size_buf(buf);
	debug("buf_len = %d", buf_len);

	safe_write(fd, ioaddr, sizeof(slurm_addr));
	safe_write(fd, respaddr, sizeof(slurm_addr));
	safe_write(fd, &buf_len, sizeof(int));
	safe_write(fd, get_buf_data(buf), buf_len);

	/* Receive the return code */
	safe_read(fd, &rc, sizeof(int));

	if (rc == SLURM_SUCCESS) {
		/* Receive response info */
		uint32_t ntasks;
		int len;

		safe_read(fd, &ntasks, sizeof(uint32_t));
		resp->ntasks = ntasks;
		len = ntasks * sizeof(uint32_t);

		resp->local_pids = xmalloc(len);
		safe_read(fd, resp->local_pids, len);

		resp->gtids = xmalloc(len);
		safe_read(fd, resp->gtids, len);

		safe_read(fd, &len, sizeof(int));
		resp->executable_name = xmalloc(len);
		safe_read(fd, resp->executable_name, len);
	}

	free_buf(buf);
	close(fd);
	return rc;

rwfail:
	close(fd);
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

	xstrcat(pattern, "^");
	xstrcat(pattern, nodename);
	xstrcat(pattern, "_([[:digit:]]*)\\.([[:digit:]]*)$");

	if (regcomp(re, pattern, REG_EXTENDED) != 0) {
                error("sockname regex compilation failed\n");
                return -1;
        }

	xfree(pattern);
}

static int
_sockname_regex(regex_t *re, const char *filename,
		uint32_t *jobid, uint32_t *stepid)
{
        size_t nmatch = 5;
        regmatch_t pmatch[5];
        char *match;

	memset(pmatch, 0, sizeof(regmatch_t)*nmatch);
	if (regexec(re, filename, nmatch, pmatch, 0) == REG_NOMATCH) {
		return -1;
	}

	match = strndup(filename + pmatch[1].rm_so,
			(size_t)(pmatch[1].rm_eo - pmatch[1].rm_so));
	*jobid = (uint32_t)atoll(match);
	free(match);

	match = strndup(filename + pmatch[2].rm_so,
			(size_t)(pmatch[2].rm_eo - pmatch[2].rm_so));
	*stepid = (uint32_t)atoll(match);
	free(match);

	return 0;
}		     

/*
 * Scan for available running slurm step daemons by checking
 * "directory" for unix domain sockets with names beginning in "nodename".
 *
 * Returns a List of pointers to step_loc_t structures.
 */
List
stepd_available(const char *directory, const char *nodename)
{
	List l;
	DIR *dp;
	struct dirent *ent;
	regex_t re;
	struct stat stat_buf;

	l = list_create((ListDelF) &_free_step_loc_t);
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
		step_loc_t *loc;
		uint32_t jobid, stepid;

		if (_sockname_regex(&re, ent->d_name, &jobid, &stepid) == 0) {
			debug4("found jobid = %u, stepid = %u", jobid, stepid);
			loc = xmalloc(sizeof(step_loc_t));
			loc->directory = xstrdup(directory);
			loc->nodename = xstrdup(nodename);
			loc->jobid = jobid;
			loc->stepid = stepid;
			list_append(l, (void *)loc);
		}
	}

done2:
	closedir(dp);
done:
	regfree(&re);
	return l;
}

/*
 * Unlink all of the unix domain socket files for a given directory
 * and nodename.
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
		uint32_t jobid, stepid;
		if (_sockname_regex(&re, ent->d_name, &jobid, &stepid) == 0) {
			char *path;
			path = NULL;
			xstrfmtcat(path, "%s/%s", directory, ent->d_name);
			verbose("Unlinking stray socket %s", path);
			if (unlink(path) == -1) {
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
stepd_pid_in_container(step_loc_t step, pid_t pid)
{
	int req = REQUEST_PID_IN_CONTAINER;
	int fd;
	bool rc;

	fd = step_connect(step);
	if (fd == -1)
		return false;

	safe_write(fd, &req, sizeof(int));
	safe_write(fd, &pid, sizeof(pid_t));

	/* Receive the return code */
	safe_read(fd, &rc, sizeof(bool));

	debug("Leaving stepd_pid_in_container");
	close(fd);
	return rc;
rwfail:
	close(fd);
	return false;
}

/*
 * Return the process ID of the slurmstepd.
 */
pid_t
stepd_daemon_pid(step_loc_t step)
{
	int req	= REQUEST_DAEMON_PID;
	int fd;
	pid_t pid;

	fd = step_connect(step);
	if (fd == -1)
		return (pid_t)-1;
	safe_write(fd, &req, sizeof(int));
	safe_read(fd, &pid, sizeof(pid_t));

	close(fd);
	return pid;
rwfail:
	close(fd);
	return (pid_t)-1;
}
