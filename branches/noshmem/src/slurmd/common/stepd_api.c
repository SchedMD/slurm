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
		printf("connect to server socket %s FAILED!\n", name);
		xfree(name);
		exit(2);
	}

	xfree(name);
	return fd;
}

int
stepd_status(step_loc_t step)
{
	int req	= REQUEST_STATUS;
	int fd;
	int status = 0;

	fd = step_connect(step);
	if (fd = -1)
		return -1;

	write(fd, &req, sizeof(int));
	read(fd, &status, sizeof(int));

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
	write(fd, &req, sizeof(int));

	/* pack auth credential */
	buf = init_buf(0);
	g_slurm_auth_pack(auth_cred, buf);
	buf_len = size_buf(buf);
	debug("buf_len = %d", buf_len);

	write(fd, &signal, sizeof(int));
	write(fd, &buf_len, sizeof(int));
	write(fd, get_buf_data(buf), buf_len);

	/* Receive the return code */
	read(fd, &rc, sizeof(int));

	free_buf(buf);
	return rc;
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
	write(fd, &req, sizeof(int));

	/* pack auth credential */
	buf = init_buf(0);
	g_slurm_auth_pack(auth_cred, buf);
	buf_len = size_buf(buf);
	debug("buf_len = %d", buf_len);

	write(fd, &signal, sizeof(int));
	write(fd, &ltaskid, sizeof(int));
	write(fd, &buf_len, sizeof(int));
	write(fd, get_buf_data(buf), buf_len);

	/* Receive the return code */
	read(fd, &rc, sizeof(int));

	free_buf(buf);
	return rc;
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
	write(fd, &req, sizeof(int));

	/* pack auth credential */
	buf = init_buf(0);
	g_slurm_auth_pack(auth_cred, buf);
	buf_len = size_buf(buf);
	debug("buf_len = %d", buf_len);

	write(fd, &signal, sizeof(int));
	write(fd, &buf_len, sizeof(int));
	write(fd, get_buf_data(buf), buf_len);

	/* Receive the return code */
	read(fd, &rc, sizeof(int));

	free_buf(buf);
	return rc;
}

int
stepd_attach(step_loc_t step, slurm_addr *ioaddr, slurm_addr *respaddr,
	     void *auth_cred, slurm_cred_t job_cred)
{
	int req = REQUEST_ATTACH;
	int fd;
	Buf buf;
	int buf_len;
	int rc;

	fd = step_connect(step);
	write(fd, &req, sizeof(int));

	/* pack auth and job credentials */
	buf = init_buf(0);
	g_slurm_auth_pack(auth_cred, buf);
	slurm_cred_pack(job_cred, buf);
	buf_len = size_buf(buf);
	debug("buf_len = %d", buf_len);

	write(fd, ioaddr, sizeof(slurm_addr));
	write(fd, respaddr, sizeof(slurm_addr));
	write(fd, &buf_len, sizeof(int));
	write(fd, get_buf_data(buf), buf_len);

	/* Receive the return code */
	read(fd, &rc, sizeof(int));

	free_buf(buf);
	return rc;
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
		printf("regexec failed: No match\n");
		return -1;
	}

	match = strndup(filename + pmatch[1].rm_so,
			(size_t)(pmatch[1].rm_eo - pmatch[1].rm_so));
	*jobid = (uint32_t)atol(match);
	free(match);

	match = strndup(filename + pmatch[2].rm_so,
			(size_t)(pmatch[2].rm_eo - pmatch[2].rm_so));
	*stepid = (uint32_t)atol(match);
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

		debug("  ent = \"%s\"", ent->d_name);
		if (_sockname_regex(&re, ent->d_name, &jobid, &stepid) == 0) {
			debug("    jobid = %u, stepid = %u", jobid, stepid);
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
