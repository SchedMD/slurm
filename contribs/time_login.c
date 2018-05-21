/*****************************************************************************\
 *  This program is used to identify users for whom a pseudo-login takes 
 *  more than SU_WAIT_MSEC to complete. Either enter specific user names 
 *  on the execute line (e.g.. "time_login alice bob") or provide no input 
 *  on the execute line to test all users in the /etc/passwd file with a 
 *  UID greater than 100 (avoiding various system users). 
 *
 *  Users indentified for whom the pseudo-login takes too long will not 
 *  have their environment variables set by Moab on job submit, which 
 *  relies upon the srun "--get-user-env" option to get this information.
 *  See Slurm's env_array_user_default() code in src/common/env.c.
 *  This option is presently used only by Moab.
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define SU_WAIT_MSEC 8000
static void _parse_line(char *in_line, char **user_name, int *user_id);
static long int  _time_login(char *user_name);

main (int argc, char **argv)
{
	FILE *passwd_fd;
	char in_line[256], *user_name;
	int i, user_id;
	long int delta_t;

	if (geteuid() != (uid_t)0) {
		printf("need to run as user root\n");
		exit(1);
	}

	for (i=1; i<argc; i++) {
		delta_t = _time_login(argv[i]);
		printf("user %-8s time %ld usec\n", argv[i], delta_t);
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
		delta_t = _time_login(user_name);
		if (delta_t < ((SU_WAIT_MSEC * 0.8) * 1000))
			continue;
		printf("user %-8s time %ld usec\n", user_name, delta_t);
	}
	fclose(passwd_fd);
}

static void _parse_line(char *in_line, char **user_name, int *user_id)
{
	char *tok;

	*user_name = strtok(in_line, ":");
	(void) strtok(NULL, ":");
	tok = strtok(NULL, ":");
	if (tok)
		*user_id = atoi(tok);
	else {
		perror("strtok");
		*user_id = 0;
	}
}

static long int _time_login(char *user_name)
{
	FILE *su;
	char line[BUFSIZ];
	char name[BUFSIZ];
	char value[BUFSIZ];
	int fildes[2], found, fval, rc, timeleft;
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
#ifdef LOAD_ENV_NO_LOGIN
		execl("/bin/su", "su", user_name, "-c",
			"echo; echo; echo HELLO", NULL);
#else
		execl("/bin/su", "su", "-", user_name, "-c", 
			"echo; echo; echo HELLO", NULL);
#endif
		exit(1);
	}

	close(fildes[1]);
	if ((fval = fcntl(fildes[0], F_GETFL, 0)) >= 0)
		fcntl(fildes[0], F_SETFL, fval | O_NONBLOCK);
	su= fdopen(fildes[0], "r");

	gettimeofday(&begin, NULL);
	ufds.fd = fildes[0];
	ufds.events = POLLIN;
	found = 0;
	while (!found) {
		gettimeofday(&now, NULL);
		timeleft = SU_WAIT_MSEC;
		timeleft -= (now.tv_sec -  begin.tv_sec)  * 1000;
		timeleft -= (now.tv_usec - begin.tv_usec) / 1000;
		if (timeleft <= 0)
			break;
		if ((rc = poll(&ufds, 1, timeleft)) <= 0) {
			if (rc == 0)	/* timeout */
				break;
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
			if (!strncmp(line, "HELLO", 5)) {
				found = 1;
				break;
			}
		}
	}
	close(fildes[0]);
	waitpid(-1, NULL, WNOHANG);

	delta_t  = (now.tv_sec  - begin.tv_sec)  * 1000000;
	delta_t +=  now.tv_usec - begin.tv_usec;
	if (!found && (delta_t < (SU_WAIT_MSEC * 1000)))
		return (SU_WAIT_MSEC * 1000);
	return delta_t;
}
