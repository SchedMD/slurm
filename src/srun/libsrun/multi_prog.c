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

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"

#include "debugger.h"
#include "multi_prog.h"
#include "opt.h"

static void
_set_range(int low_num, int high_num, char *exec_name, bool ignore_duplicates)
{
	int i;

	for (i = low_num; i <= high_num; i++) {
		MPIR_PROCDESC *tv;
		tv = &MPIR_proctable[i];
		if (tv->executable_name == NULL) {
			tv->executable_name = xstrdup(exec_name);
		} else if (!ignore_duplicates) {
			error("duplicate configuration for task %d ignored",
			      i);
		}
	}
}

static void _set_exec_names(char *ranks, char *exec_name, int ntasks)
{
	char *ptrptr = NULL;
	int low_num, high_num, num, i;

	if ((ranks[0] == '*') && (ranks[1] == '\0')) {
		low_num = 0;
		high_num = ntasks - 1;
		_set_range(low_num, high_num, exec_name, true);
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
			_set_range(low_num, high_num, exec_name, false);
		} else if (ptrptr[0] == '-') {
			low_num = MAX(0, num);
			num = strtol(ptrptr+1, &ptrptr, 10);
			if ((ptrptr[0] != ',') && (ptrptr[0] != '\0'))
				goto invalid;
			high_num = MIN((ntasks-1), num);
			_set_range(low_num, high_num, exec_name, false);
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

extern int mpir_set_multi_name(int ntasks, const char *config_fname)
{
	FILE *config_fd;
	char line[BUF_SIZE];
	char *ranks, *exec_name, *p, *ptrptr;
	int line_num = 0;
	bool last_line_break = false, line_break = false;
	int line_len;
	int i;

	for (i = 0; i < ntasks; i++) {
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
		line_len = strlen(line);
		if (line_len >= (sizeof(line) - 1)) {
			error ("Line %d of configuration file %s too long",
				line_num, config_fname);
			fclose(config_fd);
			return -1;
		}
		if ((line_len > 0 && line[line_len - 1] == '\\') ||  /* EOF */
		    (line_len > 1 && line[line_len - 2] == '\\' &&
				     line[line_len - 1] == '\n'))
			line_break = true;
		else
			line_break = false;

		if (last_line_break) {
			last_line_break = line_break;
			continue;
		}
		last_line_break = line_break;
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

extern void mpir_set_executable_names(const char *executable_name,
				      uint32_t task_offset,
				      uint32_t task_count)
{
	int i;

	if (task_offset == NO_VAL)
		task_offset = 0;
	xassert((task_offset + task_count) <= MPIR_proctable_size);
	for (i = task_offset; i < (task_offset + task_count); i++) {
		MPIR_proctable[i].executable_name = xstrdup(executable_name);
		// info("NAME[%d]:%s", i, executable_name);
	}
}

extern void
mpir_dump_proctable(void)
{
	MPIR_PROCDESC *tv;
	int i;

	for (i = 0; i < MPIR_proctable_size; i++) {
		tv = &MPIR_proctable[i];
		info("task:%d, host:%s, pid:%d, executable:%s",
		     i, tv->host_name, tv->pid, tv->executable_name);
	}
}

static int
_update_task_mask(int low_num, int high_num, slurm_opt_t *opt_local,
		  bitstr_t **task_mask, bool ignore_duplicates)
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
	if (high_num >= opt_local->ntasks) {
		static bool i_set_ntasks = false;
		if (opt_local->ntasks_set && !i_set_ntasks) {
			error("Invalid task id, %d >= ntasks", high_num);
			return -1;
		} else {
			opt_local->ntasks = high_num + 1;
			opt_local->ntasks_set = true;
			i_set_ntasks = true;
			bit_realloc((*task_mask), opt_local->ntasks);
		}
	}
	for (i=low_num; i<=high_num; i++) {
		if (bit_test((*task_mask), i)) {
			if (ignore_duplicates)
				continue;
			error("Duplicate record for task %d", i);
			return -1;
		}
		bit_set((*task_mask), i);
	}
	return 0;
}

static int
_validate_ranks(char *ranks, slurm_opt_t *opt_local, bitstr_t **task_mask)
{
	static bool has_asterisk = false;
	char *range = NULL, *p = NULL;
	char *ptrptr = NULL, *upper = NULL;
	int low_num, high_num;

	if (ranks[0] == '*' && ranks[1] == '\0') {
		low_num = 0;
		high_num = opt_local->ntasks - 1;
		opt_local->ntasks_set = true; /* do not allow to change later */
		has_asterisk = true;	/* must be last MPMD spec line */
		opt_local->srun_opt->multi_prog_cmds++;
		return _update_task_mask(low_num, high_num, opt_local,
					 task_mask, true);
	}

	for (range = strtok_r(ranks, ",", &ptrptr); range != NULL;
			range = strtok_r(NULL, ",", &ptrptr)) {
		/*
		 * Non-contiguous tasks are split into multiple commands
		 * in the mpmd_set so count each token separately
		 */
		opt_local->srun_opt->multi_prog_cmds++;
		p = range;
		while (*p != '\0' && isdigit (*p))
			p ++;

		if (has_asterisk) {
			error("Task range specification with asterisk must "
			      "be last");
			return -1;
		} else if (*p == '\0') { /* single rank */
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

		if (_update_task_mask(low_num, high_num, opt_local,
				      task_mask, false))
			return -1;
	}
	return 0;
}

/*
 * Verify that we have a valid executable program specified for each task
 *	when the --multi-prog option is used.
 * IN config_name - MPMD configuration file name
 * IN/OUT opt_local - slurm options
 * RET 0 on success, -1 otherwise
 */
extern int
verify_multi_name(char *config_fname, slurm_opt_t *opt_local)
{
	FILE *config_fd;
	char line[BUF_SIZE];
	char *ranks, *exec_name, *p, *ptrptr, *fullpath = NULL;
	int line_num = 0, i, rc = 0;
	bool last_line_break = false, line_break = false;
	int line_len;
	bitstr_t *task_mask;

	if (opt_local->ntasks <= 0) {
		error("Invalid task count %d", opt_local->ntasks);
		return -1;
	}

	opt_local->srun_opt->multi_prog_cmds = 0;

	config_fd = fopen(config_fname, "r");
	if (config_fd == NULL) {
		error("Unable to open configuration file %s", config_fname);
		return -1;
	}

	task_mask = bit_alloc(opt_local->ntasks);
	while (fgets(line, sizeof(line), config_fd)) {
		line_num++;
		line_len = strlen(line);
		if (line_len >= (sizeof(line) - 1)) {
			error ("Line %d of configuration file %s too long",
				line_num, config_fname);
			rc = -1;
			goto fini;
		}
		if ((line_len > 0 && line[line_len - 1] == '\\') ||  /* EOF */
		    (line_len > 1 && line[line_len - 2] == '\\' &&
				     line[line_len - 1] == '\n'))
			line_break = true;
		else
			line_break = false;
		if (last_line_break) {
			last_line_break = line_break;
			continue;
		}
		last_line_break = line_break;
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
		if (_validate_ranks(ranks, opt_local, &task_mask)) {
			error("Line %d of configuration file %s invalid",
				line_num, config_fname);
			rc = -1;
			goto fini;
		}
		if (opt_local->srun_opt->test_exec &&
		    !(fullpath = search_path(
			      opt_local->chdir, exec_name, true, X_OK, true))) {
			error("Line %d of configuration file %s, program %s not executable",
			      line_num, config_fname, exec_name);
			rc = -1;
			goto fini;
		}
		xfree(fullpath);
	}

	for (i = 0; i < opt_local->ntasks; i++) {
		if (!bit_test(task_mask, i)) {
			error("Configuration file %s invalid, "
				"no record for task id %d",
				config_fname, i);
			rc = -1;
			goto fini;
		}
	}

fini:	fclose(config_fd);
	FREE_NULL_BITMAP(task_mask);
	return rc;
}
