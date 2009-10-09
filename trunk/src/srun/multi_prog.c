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

#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/srun/debugger.h"
#include "src/srun/opt.h"

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
	char *ptrptr = NULL, *exec_path = NULL;
	int low_num, high_num, num, i;

	exec_path = _build_path(exec_name);
	if ((ranks[0] == '*') && (ranks[1] == '\0')) {
		low_num = 0;
		high_num = ntasks - 1;
		_set_range(low_num, high_num, exec_path);
		return;
	}

	ptrptr = ranks;
	for (i=0; i<ntasks; i++) {
		if (!isdigit(ptrptr[0]))
			goto invalid;

		num = strtol(ptrptr, &ptrptr, 10);

		if ((ptrptr[0] == ',') || (ptrptr[0] == '\0')) {
			low_num = MAX(0, num);
			high_num = MIN((ntasks-1), num);
			_set_range(low_num, high_num, exec_path);
		} else if (ptrptr[0] == '-') {
			low_num = MAX(0, num);
			num = strtol(ptrptr+1, &ptrptr, 10);
			if ((ptrptr[0] != ',') && (ptrptr[0] != '\0'))
				goto invalid; 
			high_num = MIN((ntasks-1), num);
			_set_range(low_num, high_num, exec_path);
		} else
			goto invalid;
		if (ptrptr[0] == '\0')
			break;
		ptrptr++;
	}
	return;

  invalid:
	error ("Invalid task range specification (%s) ignored.", ranks);
	return;
}

extern int
mpir_set_multi_name(int ntasks, const char *config_fname)
{
	FILE *config_fd;
	char line[256];
	char *ranks, *exec_name, *p, *ptrptr;
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
			error ("Line %d of configuration file %s too long", 
				line_num, config_fname);
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
			error("Line %d of configuration file %s is invalid", 
				line_num, config_fname);
			fclose(config_fd);
			return -1;
		}
		_set_exec_names(ranks, exec_name, ntasks);
	}
	fclose(config_fd);
	return 0;
}

extern void
mpir_init(int num_tasks)
{
	MPIR_proctable_size = num_tasks;
	MPIR_proctable = xmalloc(sizeof(MPIR_PROCDESC) * num_tasks);
	if (MPIR_proctable == NULL) {
		error("Unable to initialize MPIR_proctable: %m");
		exit(error_exit);
	}
}

extern void
mpir_cleanup(void)
{
	int i;

	for (i = 0; i < MPIR_proctable_size; i++) {
		xfree(MPIR_proctable[i].host_name);
		xfree(MPIR_proctable[i].executable_name);
	}
	xfree(MPIR_proctable);
}

extern void
mpir_set_executable_names(const char *executable_name)
{
	int i;

	for (i = 0; i < MPIR_proctable_size; i++) {
		MPIR_proctable[i].executable_name = xstrdup(executable_name);
		if (MPIR_proctable[i].executable_name == NULL) {
			error("Unable to set MPI_proctable executable_name:"
			      " %m");
			exit(error_exit);
		}
	}
}

extern void
mpir_dump_proctable()
{
	MPIR_PROCDESC *tv;
	int i;

	for (i = 0; i < MPIR_proctable_size; i++) {
		tv = &MPIR_proctable[i];
		if (!tv)
			break;
		info("task:%d, host:%s, pid:%d, executable:%s",
		     i, tv->host_name, tv->pid, tv->executable_name);
	}
}

static int
_update_task_mask(int low_num, int high_num, int ntasks, bitstr_t *task_mask)
{
	int i;

	if (low_num > high_num) {
		error("Invalid task range, %d-%d", low_num, high_num);
		return -1;
	}
	if (low_num < 0) {
		error("Invalid task id, %d < 0", low_num);
		return -1;
	}
	if (high_num >= ntasks) {
		error("Invalid task id, %d >= ntasks", high_num);
		return -1;
	}
	for (i=low_num; i<=high_num; i++) {
		if (bit_test(task_mask, i)) {
			error("Duplicate record for task %d", i);
			return -1;
		}
		bit_set(task_mask, i);
	}
	return 0;
}

static int
_validate_ranks(char *ranks, int ntasks, bitstr_t *task_mask)
{
	char *range = NULL, *p = NULL;
	char *ptrptr = NULL, *upper = NULL;
	int low_num, high_num;

	if (ranks[0] == '*' && ranks[1] == '\0') {
		low_num = 0;
		high_num = ntasks - 1;
		return _update_task_mask(low_num, high_num, ntasks, task_mask);
	}

	for (range = strtok_r(ranks, ",", &ptrptr); range != NULL;
			range = strtok_r(NULL, ",", &ptrptr)) {
		p = range;
		while (*p != '\0' && isdigit (*p))
			p ++;

		if (*p == '\0') { /* single rank */
			low_num  = atoi(range);
			high_num = low_num;
		} else if (*p == '-') { /* lower-upper */
			upper = ++ p;
			while (isdigit (*p))
				p ++;
			if (*p != '\0') {
				error ("Invalid task range specification");
				return -1;
			}
			low_num  = atoi(range);
			high_num = atoi(upper);
		} else {
			error ("Invalid task range specification (%s)",
				range);
			return -1;
		}

		if (_update_task_mask(low_num, high_num, ntasks, task_mask))
			return -1;
	}
	return 0;
}

/*
 * Verify that we have a valid executable program specified for each task
 *	when the --multi-prog option is used.
 *
 * Return 0 on success, -1 otherwise
 */
extern int
verify_multi_name(char *config_fname, int ntasks)
{
	FILE *config_fd;
	char line[256];
	char *ranks, *exec_name, *p, *ptrptr;
	int line_num = 0, i, rc = 0;
	bitstr_t *task_mask;

	if (ntasks <= 0) {
		error("Invalid task count %d", ntasks);
		return -1;
	}

	config_fd = fopen(config_fname, "r");
	if (config_fd == NULL) {
		error("Unable to open configuration file %s", config_fname);
		return -1;
	}

	task_mask = bit_alloc(ntasks);
	while (fgets(line, sizeof(line), config_fd)) {
		line_num ++;
		if (strlen (line) >= (sizeof(line) - 1)) {
			error ("Line %d of configuration file %s too long", 
				line_num, config_fname);
			rc = -1;
			goto fini;
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
			error("Line %d of configuration file %s invalid", 
				line_num, config_fname);
			rc = -1;
			goto fini;
		}
		if (_validate_ranks(ranks, ntasks, task_mask)) {
			error("Line %d of configuration file %s invalid", 
				line_num, config_fname);
			rc = -1;
			goto fini;
		}
	}

	for (i=0; i<ntasks; i++) {
		if (!bit_test(task_mask, i)) {
			error("Configuration file %s invalid, "
				"no record for task id %d", 
				config_fname, i);
			rc = -1;
			goto fini;
		}
	}

fini:	fclose(config_fd);
	bit_free(task_mask);
	return rc;
}
