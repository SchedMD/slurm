/*****************************************************************************\
 *  On the cluster's control host as user root, execute:
 *    make -f /dev/null env_cache_builder
 *    ./env_cache_builder
 *****************************************************************************
 *  This program is used to build an environment variable cache file for use
 *  with the srun/sbatch --get-user-env option, which is used by Moab to launch
 *  user jobs. srun/sbatch will first attempt to load the user's current 
 *  environment by executing "su - <user> -c env". If that fails to complete
 *  in a relatively short period of time (currently 3 seconds), srun/sbatch
 *  will attempt to load the user's environment from a cache file located 
 *  in the directory StateSaveLocation with a name of the sort "env_<user>".
 *  If that fails, then abort the job request.
 *
 *  This program can accept a space delimited list of individual users to have
 *  cache files created (e.g. "cache_build alice bob chuck"). If no argument
 *  is given, cache files will be created for all users in the "/etc/passwd"
 *  file. If you see "ERROR" in the output, it means that the user's 
 *  environment could not be loaded automatically, typically because their
 *  dot files spawn some other shell. You must explicitly login as the user,
 *  execute "env" and write the output to a file having the same name as the
 *  user in a subdirectory of the configured StateSaveLocation named "env_cache"
 *  (e.g. "/tmp/slurm/env_cache/alice"). The file is only needed on the node
 *  where the Moab daemon executes, typically the control host.
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  LLNL-CODE-402394.
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
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define _DEBUG 0
#define SU_WAIT_MSEC 8000

static long int	_build_cache(char *user_name, char *cache_dir);
static int	_get_cache_dir(char *buffer, int buf_size);
static void	_log_failures(int failures, char *cache_dir);
static int	_parse_line(char *in_line, char **user_name, int *user_id);

char *env_loc = NULL;

main (int argc, char **argv)
{
	FILE *passwd_fd;
	char cache_dir[256], in_line[256], *user_name;
	int i, failures = 0, user_cnt = 0, user_id;
	long int delta_t;
	struct stat buf;

	if (geteuid() != (uid_t) 0) {
		printf("Need to run as user root\n");
		exit(1);
	}

	if (_get_cache_dir(cache_dir, sizeof(cache_dir)))
		exit(1);
	strncat(cache_dir, "/env_cache", sizeof(cache_dir));
	if (mkdir(cache_dir, 0500) && (errno != EEXIST)) {
		printf("Could not create cache directory %s: %s\n", cache_dir,
			strerror(errno));
		exit(1);
	}

	if (stat("/bgl", &buf) == 0) {
		printf("BlueGene Note: Execute only a a front-end node, "
			"not the service node\n");
		printf("               User logins to the service node are "
			"disabled\n\n");
	}
	if (stat("/bin/su", &buf)) {
		printf("Could not locate command: /bin/su\n");
		exit(1);
	}
	if (stat("/bin/echo", &buf)) {
		printf("Could not locate command: /bin/echo\n");
		exit(1);
	}
	if (stat("/bin/env", &buf) == 0)
		env_loc = "/bin/env";
	else if (stat("/usr/bin/env", &buf) == 0)
		env_loc = "/usr/bin/env";
	else {
		printf("Could not location command: env\n");
		exit(1);
	}

	printf("Building user environment cache files for Moab/Slurm.\n");
	printf("This will take a while.\n\n");

	for (i=1; i<argc; i++) {
		delta_t = _build_cache(argv[i], cache_dir);
		if (delta_t == -1)
			failures++;
		if (delta_t < ((SU_WAIT_MSEC * 0.8) * 1000))
			continue;
		printf("WARNING: user %-8s time %ld usec\n", argv[i], delta_t);
	}
	if (i > 1) {
		_log_failures(failures, cache_dir);
		exit(0);
	}

	passwd_fd = fopen("/etc/passwd", "r");
	if (!passwd_fd) {
		perror("fopen(/etc/passwd)");
		exit(1);
	}

	while (fgets(in_line, sizeof(in_line), passwd_fd)) {
		if (_parse_line(in_line, &user_name, &user_id) < 0)
			continue;
		if (user_id <= 100)
			continue;
		delta_t = _build_cache(user_name, cache_dir);
		if (delta_t == -1)
			failures++;
		user_cnt++;
		if ((user_cnt % 100) == 0)
			printf("Processed %d users...\n", user_cnt);
		if (delta_t < ((SU_WAIT_MSEC * 0.8) * 1000))
			continue;
		printf("WARNING: user %-8s time %ld usec\n", user_name, delta_t);
	}
	fclose(passwd_fd);
	_log_failures(failures, cache_dir);
}

static void _log_failures(int failures, char *cache_dir)
{
	if (failures) {
		printf("\n");
		printf("Some user environments could not be loaded.\n");
		printf("Manually run 'env' for those %d users.\n",
			failures);
		printf("Write the output to a file with the same name as "
			"the user in the\n %s directory\n", cache_dir);
	} else {
		printf("\n");
		printf("All user environments successfully loaded.\n");
		printf("Files written to the %s directory\n", cache_dir);
	}
}

/* Given a line from /etc/passwd, sets the user_name and user_id
 * RET -1 if user can't login, 0 otherwise */
static int _parse_line(char *in_line, char **user_name, int *user_id)
{
	char *tok, *shell;
	
	/* user name */
	*user_name = strtok(in_line, ":");

	(void) strtok(NULL, ":");

	/* uid */
	tok = strtok(NULL, ":");
	if (tok)
		*user_id = atoi(tok);
	else {
		printf("ERROR: parsing /etc/passwd: %s\n", in_line);
		*user_id = 0;
	}

	(void) strtok(NULL, ":");	/* gid */
	(void) strtok(NULL, ":");	/* name */
	(void) strtok(NULL, ":");	/* home */

	shell = strtok(NULL, ":");
	if (shell) {
		tok = strchr(shell, '\n');
		if (tok)
			tok[0] = '\0';
		if ((strcmp(shell, "/sbin/nologin") == 0) ||
		    (strcmp(shell, "/bin/false") == 0))
			return -1;
	}

	return 0;

}

/* For a given user_name, get his environment variable by executing
 * "su - <user_name> -c env" and store the result in 
 * cache_dir/env_<user_name>
 * Returns time to perform the operation in usec or -1 on error
 */
static long int _build_cache(char *user_name, char *cache_dir)
{
	FILE *cache;
	char *line, *last, out_file[BUFSIZ], buffer[64 * 1024];
	char *starttoken = "XXXXSLURMSTARTPARSINGHEREXXXX";
	char *stoptoken  = "XXXXSLURMSTOPPARSINGHEREXXXXX";
	int fildes[2], found, fval, len, rc, timeleft;
	int buf_read, buf_rem;
	pid_t child;
	struct timeval begin, now;
	struct pollfd ufds;
	long int delta_t;

	gettimeofday(&begin, NULL);

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
		snprintf(buffer, sizeof(buffer),
			 "/bin/echo; /bin/echo; /bin/echo; "
			 "/bin/echo %s; %s; /bin/echo %s",
			 starttoken, env_loc, stoptoken);
#ifdef LOAD_ENV_NO_LOGIN
		execl("/bin/su", "su", user_name, "-c", buffer, NULL);
#else
		execl("/bin/su", "su", "-", user_name, "-c", buffer, NULL);
#endif
		exit(1);
	}

	close(fildes[1]);
	if ((fval = fcntl(fildes[0], F_GETFL, 0)) >= 0)
		fcntl(fildes[0], F_SETFL, fval | O_NONBLOCK);

	ufds.fd = fildes[0];
	ufds.events = POLLIN;
	ufds.revents = 0;

	/* Read all of the output from /bin/su into buffer */
	found = 0;
	buf_read = 0;
	bzero(buffer, sizeof(buffer));
	while (1) {
		gettimeofday(&now, NULL);
		timeleft = SU_WAIT_MSEC * 10;
		timeleft -= (now.tv_sec -  begin.tv_sec)  * 1000;
		timeleft -= (now.tv_usec - begin.tv_usec) / 1000;
		if (timeleft <= 0) {
#if _DEBUG
			printf("timeout1 for %s\n", user_name);
#endif
			break;
		}
		if ((rc = poll(&ufds, 1, timeleft)) <= 0) {
			if (rc == 0) {
#if _DEBUG
				printf("timeout2 for %s\n, user_name");
#endif
				break;
			}
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			perror("poll");
			break;
		}
		if (!(ufds.revents & POLLIN)) {
			if (ufds.revents & POLLHUP) {	/* EOF */
#if _DEBUG
				printf("POLLHUP for %s\n", user_name);
#endif
				found = 1;		/* success */
			} else if (ufds.revents & POLLERR) {
				printf("ERROR: POLLERR for %s\n", user_name);
			} else {
				printf("ERROR: poll() revents=%d for %s\n", 
					ufds.revents, user_name);
			}
			break;
		}
		buf_rem = sizeof(buffer) - buf_read;
		if (buf_rem == 0) {
			printf("ERROR: buffer overflow for %s\n", user_name);
			break;
		}
		rc = read(fildes[0], &buffer[buf_read], buf_rem);
		if (rc > 0)
			buf_read += rc;
		else if (rc == 0) {	/* EOF */
#if _DEBUG
			printf("EOF for %s\n", user_name);
#endif
			found = 1;	/* success */
			break;
		} else {		/* error */
			perror("read");
			break;
		}
	}
	close(fildes[0]);
	if (!found) {
		printf("***ERROR: Failed to load current user environment "
			"variables for %s\n", user_name);
		return -1;
	}

	/* First look for the start token in the output */
	len = strlen(starttoken);
	found = 0;
	line = strtok_r(buffer, "\n", &last);
	while (!found && line) {
		if (!strncmp(line, starttoken, len)) {
			found = 1;
			break;
		}
		line = strtok_r(NULL, "\n", &last);
	}
	if (!found) {
		printf("***ERROR: Failed to get current user environment "
			"variables for %s\n", user_name);
		return -1;
	}

	snprintf(out_file, sizeof(out_file), "%s/%s", cache_dir, user_name);
	cache = fopen(out_file, "w");
	if (!cache) {
		printf("ERROR: Could not create cache file %s for %s: %s\n", 
			out_file, user_name, strerror(errno));
		return -1;
	}
	chmod(out_file, 0600);

	/* Process environment variables until we find the stop token */
	len = strlen(stoptoken);
	found = 0;
	line = strtok_r(NULL, "\n", &last);
	while (!found && line) {
		if (!strncmp(line, stoptoken, len)) {
			found = 1;
			break;
		}
		if (fprintf(cache, "%s\n",line) < 0) {
			printf("ERROR: Could not write cache file %s "
				"for %s: %s\n", 
				out_file, user_name, strerror(errno));
			found = 1;	/* quit now */
		}
		line = strtok_r(NULL, "\n", &last);
	}
	fclose(cache);
	waitpid(-1, NULL, WNOHANG);

	gettimeofday(&now, NULL);
	delta_t  = (now.tv_sec  - begin.tv_sec)  * 1000000;
	delta_t +=  now.tv_usec - begin.tv_usec;
	if (!found) {
		printf("***ERROR: Failed to write all user environment "
			"variables for %s\n", user_name);
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
		printf("ERROR: Failed to get StateSaveLocation\n");
		close(fildes[0]);
		return -1;
	}

	waitpid(-1, NULL, WNOHANG);
	return 0;
}
