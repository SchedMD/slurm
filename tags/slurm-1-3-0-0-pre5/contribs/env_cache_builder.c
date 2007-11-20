/*****************************************************************************\
 *  This program is used to build an environment variable cache file for use
 *  with the srun/sbatch --get-user-env option, which is used by Moab to launch
 *  user jobs. srun/sbatch will first attempt to load the user's current 
 *  environment by executing "su - <user> -c env". If that fails to complete
 *  in a relatively short period of time (currently 8 seconds), srun/sbatch
 *  will attempt to load the user's environment from a cache file located 
 *  in the directory StateSaveLocation with a name of the sort "env_<user>".
 *  If that fails, then abort the job request.
 *
 *  This program can accept a space delimited list of individual users to have
 *  cache files created (e.g. "cache_build alice bob chuck"). If no argument
 *  is given, cache files will be created for all users.
 *
 *  This program must execute as user root. 
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define DEBUG 0
#define SU_WAIT_MSEC 8000

static long int	_build_cache(char *user_name, char *cache_dir);
static int	_get_cache_dir(char *buffer, int buf_size);
static void	_parse_line(char *in_line, char **user_name, int *user_id);

main (int argc, char **argv)
{
	FILE *passwd_fd;
	char cache_dir[256], in_line[256], *user_name;
	int i, user_id;
	long int delta_t;

	if (geteuid() != (uid_t)0) {
		printf("Need to run as user root\n");
		exit(1);
	}

	if (_get_cache_dir(cache_dir, sizeof(cache_dir)))
		exit(1);
	strncat(cache_dir, "/env_cache", sizeof(cache_dir));
	if (mkdir(cache_dir, 0500) && (errno != EEXIST)) {
		printf("Could not create cache directory %s: %s", cache_dir,
			strerror(errno));
		exit(1);
	}
#if DEBUG
	printf("cache_dir=%s\n", cache_dir);
#endif

	for (i=1; i<argc; i++) {
		delta_t = _build_cache(argv[i], cache_dir);
#if DEBUG
		printf("user %-8s time %ld usec\n", argv[i], delta_t);
#endif
	}
	if (i > 1)
		exit(0);

	passwd_fd = fopen("/etc/passwd", "r");
	if (!passwd_fd) {
		perror("fopen(/etc/passwd)");
		exit(1);
	}

	while (fgets(in_line, sizeof(in_line), passwd_fd)) {
		_parse_line(in_line, &user_name, &user_id);
		if (user_id <= 100)
			continue;
		delta_t = _build_cache(user_name, cache_dir);
#if DEBUG
		if (delta_t < ((SU_WAIT_MSEC * 0.8) * 1000))
			continue;
		printf("user %-8s time %ld usec\n", user_name, delta_t);
#endif
	}
	fclose(passwd_fd);
}

/* Given a line from /etc/passwd, return the user_name and user_id */
static void _parse_line(char *in_line, char **user_name, int *user_id)
{
	char *tok;

	*user_name = strtok(in_line, ":");
	(void) strtok(NULL, ":");
	tok = strtok(NULL, ":");
	if (tok)
		*user_id = atoi(tok);
	else {
		printf("error parsing /etc/passwd: %s\n", in_line);
		*user_id = 0;
	}
}

/* For a given user_name, get his environment variable by executing
 * "su - <user_name> -c env" and store the result in 
 * cache_dir/env_<user_name>
 * Returns time to perform the operation in usec
 */
static long int _build_cache(char *user_name, char *cache_dir)
{
	FILE *su, *cache;
	char line[BUFSIZ], name[BUFSIZ], value[BUFSIZ], out_file[BUFSIZ];
	char *starttoken = "XXXXSLURMSTARTPARSINGHEREXXXX";
	char *stoptoken  = "XXXXSLURMSTOPPARSINGHEREXXXXX";
	int fildes[2], found, fval, len, rc, timeleft;
	pid_t child;
	struct timeval begin, now;
	struct pollfd ufds;
	long int delta_t;

	if (pipe(fildes) < 0) {
		perror("pipe");
		return -1;
	}

	child = fork();
	if (child == -1) {
		perror("fork");
		return -1;
	}
	if (child == 0) {
		close(0);
		open("/dev/null", O_RDONLY);
		dup2(fildes[1], 1);
		close(2);
		open("/dev/null", O_WRONLY);
		snprintf(line, sizeof(line),
			 "echo; echo; echo; echo %s; env; echo %s",
			 starttoken, stoptoken);
#ifdef LOAD_ENV_NO_LOGIN
		execl("/bin/su", "su", user_name, "-c", line, NULL);
#else
		execl("/bin/su", "su", "-", user_name, "-c", line, NULL);
#endif
		exit(1);
	}

	close(fildes[1]);
	if ((fval = fcntl(fildes[0], F_GETFL, 0)) >= 0)
		fcntl(fildes[0], F_SETFL, fval | O_NONBLOCK);
	su = fdopen(fildes[0], "r");

	gettimeofday(&begin, NULL);
	ufds.fd = fildes[0];
	ufds.events = POLLIN;

	/* First look for the start token in the output */
	len = strlen(starttoken);
	found = 0;
	while (!found) {
		gettimeofday(&now, NULL);
		timeleft = SU_WAIT_MSEC * 10;
		timeleft -= (now.tv_sec -  begin.tv_sec)  * 1000;
		timeleft -= (now.tv_usec - begin.tv_usec) / 1000;
		if (timeleft <= 0) {
#if DEBUG
			printf("timeout1\n");
#endif
			break;
		}
		if ((rc = poll(&ufds, 1, timeleft)) <= 0) {
			if (rc == 0) {
#if DEBUG
				printf("timeout2\n");
#endif
				break;
			}
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			perror("poll");
			break;
		}
		if (!(ufds.revents & POLLIN)) {
			perror("POLLERR|POLLHUP");
			break;
		}
		while (fgets(line, BUFSIZ, su)) {
			if (!strncmp(line, starttoken, len)) {
				found = 1;
				break;
			}
		}
	}
	if (!found) {
		printf("Failed to get current user environment variables "
			"for %s\n", user_name);
		close(fildes[0]);
		gettimeofday(&now, NULL);
		delta_t  = now.tv_sec -  begin.tv_sec * 1000000;
		delta_t += now.tv_usec - begin.tv_usec;
		if (delta_t < (SU_WAIT_MSEC * 1000))
			return (SU_WAIT_MSEC * 1000);
		return delta_t;
	}

	snprintf(out_file, sizeof(out_file), "%s/%s", cache_dir, user_name);
	cache = fopen(out_file, "w");
	if (!cache) {
		printf("Could not create cache file %s: %s\n", out_file, 
			strerror(errno));
	}

	len = strlen(stoptoken);
	found = 0;
	while (!found && cache) {
		gettimeofday(&now, NULL);
		timeleft = SU_WAIT_MSEC * 10;
		timeleft -= (now.tv_sec -  begin.tv_sec)  * 1000;
		timeleft -= (now.tv_usec - begin.tv_usec) / 1000;
		if (timeleft <= 0) {
#if DEBUG
			printf("timeout3\n");
#endif
			break;
		}
		if ((rc = poll(&ufds, 1, timeleft)) <= 0) {
			if (rc == 0) {
#if DEBUG
				printf("timeout4\n");
#endif
				break;
			}
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			perror("poll");
			break;
		}
		if (!(ufds.revents & POLLIN)) {
			perror("POLLERR|POLLHUP");
			break;
		}
		/* stop at the line containing the stoptoken string */
		if ((fgets(line, BUFSIZ, su) == 0) ||
		    (!strncmp(line, stoptoken, len))) {
			found = 1;
			break;
		}

		if (fputs(line, cache) == EOF) {
			printf("Could not write cache file %s: %s\n", 
				out_file, strerror(errno));
			found = 1;	/* quit now */
		}
	}
	close(fildes[0]);
	if (cache)
		fclose(cache);
	waitpid(-1, NULL, WNOHANG);

	gettimeofday(&now, NULL);
	delta_t  = (now.tv_sec  - begin.tv_sec)  * 1000000;
	delta_t +=  now.tv_usec - begin.tv_usec;
	if (!found) {
		printf("Failed to get current user environment variables "
			"for %s\n", user_name);
		if (delta_t < (SU_WAIT_MSEC * 1000))
			return (SU_WAIT_MSEC * 1000);
	}
	return delta_t;
}

/* Get configured StateSaveLocation. 
 * User environment variable caches get created there.
 * Returns 0 on success, -1 on error. */
static int _get_cache_dir(char *buffer, int buf_size)
{
	FILE *scontrol;
	int fildes[2];
	pid_t child, fval;
	char line[BUFSIZ], *fname;

	if (pipe(fildes) < 0) {
		perror("pipe");
		return -1;
	}

	child = fork();
	if (child == -1) {
		perror("fork");
		return -1;
	}
	if (child == 0) {
		close(0);
		open("/dev/null", O_RDONLY);
		dup2(fildes[1], 1);
		close(2);
		open("/dev/null", O_WRONLY);
		execlp("scontrol", "scontrol", "show", "config", NULL);
		perror("execl(scontrol)");
		return -1;
	}

	close(fildes[1]);
	scontrol = fdopen(fildes[0], "r");

	buffer[0] = '\0';
	while (fgets(line, BUFSIZ, scontrol)) {
		if (strncmp(line, "StateSaveLocation", 17))
			continue;
		fname = strchr(line, '\n');
		if (fname)
			fname[0] = '\0';
		fname = strchr(line, '/');
		if (fname)
			strncpy(buffer, fname, buf_size);
		break;
	}
	close(fildes[0]);
	if (!buffer[0]) {
		printf("Failed to get StateSaveLocation\n");
		close(fildes[0]);
		return -1;
	}

	waitpid(-1, NULL, WNOHANG);
	return 0;
}
