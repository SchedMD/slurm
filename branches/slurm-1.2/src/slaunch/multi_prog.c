/*****************************************************************************\
 *  multi_prog.c - executing program according to task rank
 *                 set MPIR_PROCDESC accordingly
 *
 *  NOTE: The logic could be eliminated if slurmstepd kept track of the 
 *  executable name for each task and returned that inforatmion in a new
 *  launch response message (with multiple executable names).
 *****************************************************************************
 *  Produced at National University of Defense Technology (China)
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>
 *  and
 *  Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slaunch/attach.h"

/* Given a program name, translate it to a fully qualified pathname
 * as needed based upon the PATH environment variable */
static char *
_build_path(char* fname)
{
	int i;
	char *path_env = NULL, *dir = NULL, *ptrptr = NULL;
	static char file_name[256], file_path[256];	/* return values */
	struct stat buf;

	/* make copy of file name (end at white space) */
	snprintf(file_name, sizeof(file_name), "%s", fname);
	for (i=0; i<sizeof(file_name); i++) {
		if (file_name[i] == '\0')
			break;
		if (!isspace(file_name[i]))
			continue;
		file_name[i] = '\0';
		break;
	}

	/* check if already absolute path */
	if (file_name[0] == '/')
		return file_name;

	/* search for the file using PATH environment variable */
	dir = getenv("PATH");
	if (!dir) {
		error("No PATH environment variable");
		return NULL;
	}
	path_env = xstrdup(dir);
	dir = strtok_r(path_env, ":", &ptrptr);
	while (dir) {
		snprintf(file_path, sizeof(file_path), "%s/%s", dir, file_name);
		if (stat(file_path, &buf) == 0)
			break;
		dir = strtok_r(NULL, ":", &ptrptr);
	}
	if (dir == NULL) {	/* not found */
		error("Could not find executable %s", file_name);
		snprintf(file_path, sizeof(file_path), "%s", file_name);
	}
	xfree(path_env);
	return file_path;
}

static void
_set_range(int low_num, int high_num, char *exec_name)
{
	int i;

	for (i=low_num; i<=high_num; i++) {
		MPIR_PROCDESC *tv;
		tv = &MPIR_proctable[i];
		if (tv->executable_name) {
			error("duplicate configuration for task %d ignored",
				i);
		} else
			tv->executable_name = xstrdup(exec_name);
	}
}

static void
_set_exec_names(char *ranks, char *exec_name, int ntasks)
{
	char *range = NULL, *p = NULL, *ptrptr = NULL;
	char *exec_path = NULL, *upper = NULL;
	int low_num, high_num;

	if (ranks[0] == '*' && ranks[1] == '\0') {
		low_num = 0;
		high_num = ntasks -1;
		_set_range(low_num, high_num, exec_name);
		return;
	}
	exec_path = _build_path(exec_name);

	for (range = strtok_r(ranks, ",", &ptrptr);
	     range != NULL;
	     range = strtok_r(NULL, ",", &ptrptr)) {
		p = range;
		while (*p != '\0' && isdigit (*p))
			p ++;

		if (*p == '\0') { /* single rank */
			low_num  = MAX(0, atoi(range));
			high_num = MIN((ntasks-1), atoi(range));
			_set_range(low_num, high_num, exec_path);
		} else if (*p == '-') { /* lower-upper */
			upper = ++ p;
			while (isdigit (*p))
				p ++;
			if (*p != '\0') {
				error ("Invalid task range specification (%s) "
					"ignored.", range);
				continue;
			}
			low_num  = MAX(0, atoi (range));
			high_num = MIN((ntasks-1), atoi(upper));
			_set_range(low_num, high_num, exec_path);
		} else {
			error ("Invalid task range specification (%s) ignored.",				range);
		}
	}
}

extern int
mpir_set_multi_name(int ntasks, const char *config_fname)
{
	FILE *config_fd;
	char line[256];
	char *ranks = NULL, *exec_name = NULL, *p = NULL, *ptrptr = NULL;
	int line_num = 0, i;

	for (i=0; i<ntasks; i++) {
		MPIR_PROCDESC *tv;
		tv = &MPIR_proctable[i];
		tv->executable_name = NULL;
	}

	config_fd = fopen(config_fname, "r");
	if (config_fd == NULL) {
		error("Unable to open configuration file %s", config_fname);
		return -1;
	}
	while (fgets(line, sizeof(line), config_fd)) {
		line_num ++;
		if (strlen (line) >= (sizeof(line) - 1)) {
			error ("Line %d of configuration file too long", 
				line_num);
			fclose(config_fd);
			return -1;
		} 
		p = line;
		while (*p != '\0' && isspace (*p)) /* remove leading spaces */
			p ++;
		
		if (*p == '#') /* only whole-line comments handled */
			continue;

		if (*p == '\0') /* blank line ignored */
			continue;

		ranks = strtok_r(p, " \t\n", &ptrptr);
		exec_name = strtok_r(NULL, " \t\n", &ptrptr);
		if (!ranks || !exec_name) {
			error("Line %d is invalid", line_num);
			fclose(config_fd);
			return -1;
		}
		_set_exec_names(ranks, exec_name, ntasks);
	}
	fclose(config_fd);
	return 0;
}
