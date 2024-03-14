/*****************************************************************************\
 *  multi_prog.c - Find the argv array for each task when multi-prog is enabled.
 *
 *  NOTE: This code could be moved into the API if desired. That would mean the
 *  logic would be executed once per step instead of once per task. This would
 *  require substantial modifications to the srun, slurmd, slurmstepd, and
 *  communications logic; so we'll stick with the simple solution for now.
 *****************************************************************************
 *  Produced at National University of Defense Technology (China)
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>
 *  and
 *  Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>,
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
#include "multi_prog.h"

#define MAX_ARGC 128
#define _DEBUG   0

/*
 * Test if the specified rank is included in the supplied task range
 * IN rank    - this task's rank
 * IN spec    - a line from the configuration file
 * OUT offset - the task's offset within rank range of the configuration file
 * RET 1 if within range, 0 otherwise
 */
static int
_in_range(int rank, char* spec, int *offset)
{
	char* range;
	char* p;
	char* upper;
	int high_num, low_num, passed = 0;

	xassert(offset);

	if (spec[0] == '*' && spec[1] == '\0') {
		*offset = rank;
		return 1;
	}

	for (range = strtok (spec, ","); range != NULL;
			range = strtok (NULL, ",")) {
		p = range;
		while (*p != '\0' && isdigit (*p))
			p ++;
		if (*p == '\0') { /* single rank */
			if (rank == atoi (range)) {
				*offset = passed;
				return 1;
			}
			passed ++;

		} else if (*p == '-') { /* lower-upper */
			upper = ++ p;
			while (isdigit (*p))
				p ++;
			if (*p != '\0') {
				error ("Invalid task range specification (%s) "
					"ignored.", range);
				continue;
			};
			low_num  = atoi (range);
			high_num = atoi (upper);
			if ((rank >= low_num) && (rank <= high_num)) {
				*offset = passed + (rank - low_num);
				return 1;
			} else
				passed += (1 + high_num - low_num);

		} else {
			error ("Invalid task range specification (%s) ignored.",
				range);
		}
	}
	return 0;
}

/*
 * FIXME - It would be nice to parse the multi-prog array just once
 *	to retrieve the argv arrays for each task on this node, rather
 *	than calling multi_prog_get_argv once for each task.
 */
extern int multi_prog_get_argv(char *config_data, char **prog_env,
			       int task_rank, uint32_t *argc, char ***argv,
			       int global_argc, char **global_argv)
{
	char *line = NULL;
	int i, line_num = 0;
	int task_offset;
	char *p = NULL, *ptrptr = NULL;
	char *rank_spec = NULL, *args_spec = NULL;
	int prog_argc = 0;
	char **prog_argv = NULL;
	char *local_data = NULL;
	char *arg_buf = NULL;
	bool last_line_break = false, line_break = false;
	int line_len;

	prog_argv = xcalloc(MAX_ARGC, sizeof(char *));

	if (task_rank < 0) {
		error("Invalid task rank %d", task_rank);
		*argc = 1;
		*argv = prog_argv;
		return -1;
	}

	local_data = xstrdup(config_data);
	line = strtok_r(local_data, "\n", &ptrptr);
	while (line) {
		if (line_num > 0)
			line = strtok_r(NULL, "\n", &ptrptr);
		if (line == NULL) {
			error("No executable program specified for this task");
			goto fail;
		}
		line_num++;
		line_len = strlen(line);
		if ((line_len > 0) && (line[line_len - 1] == '\\'))
			line_break = true;
		else
			line_break = false;
		if (last_line_break) {
			last_line_break = line_break;
			continue;
		}
		last_line_break = line_break;
		p = line;
		while ((*p != '\0') && isspace (*p)) /* remove leading spaces */
			p++;

		if (*p == '#') /* only whole-line comments handled */
			continue;

		if (*p == '\0') /* blank line ignored */
			continue;

		rank_spec = p;

		while ((*p != '\0') && !isspace (*p))
			p++;
		if (*p == '\0') {
			error("Invalid MPMD configuration line %d", line_num);
			goto fail;
		}
		*p++ = '\0';

		if (!_in_range(task_rank, rank_spec, &task_offset))
			continue;

		/* skip all whitspace after the range spec */
		while ((*p != '\0') && isspace (*p))
			p++;

		args_spec = p;
		while (*args_spec != '\0') {
			/* Only simple quote and escape supported */
			if (arg_buf) {
				prog_argv[prog_argc++] = arg_buf;
				arg_buf=NULL;
			}
			if ((prog_argc + 1) >= MAX_ARGC) {
				info("Exceeded multi-prog argc limit");
				break;
			}
		CONT:	p = args_spec;
			while ((*args_spec != '\0') && (*args_spec != '\\') &&
			       (*args_spec != '%')  && (*args_spec != '\'') &&
			       !isspace(*args_spec)) {
				args_spec++;
			}
			xstrncat(arg_buf, p, (args_spec - p));
			if (*args_spec == '\0') {
				/* the last argument */
				break;

			} else if (*args_spec == '%') {
				args_spec++;
				if (*args_spec == 't') {
					/* task rank */
					xstrfmtcat(arg_buf, "%d", task_rank);
				} else if (*args_spec == 'o') {
					/* task offset */
					xstrfmtcat(arg_buf, "%d", task_offset);
				}
				args_spec++;
				goto CONT;

			} else if (*args_spec == '\\') {
				/* escape, just remove the backslash */
				args_spec++;
				if (*args_spec != '\0') {
					xstrcatchar(arg_buf, *args_spec);
					args_spec++;
				} else {
					line = strtok_r(NULL, "\n", &ptrptr);
					if (!line)
						break;
					line_num++;
					args_spec = line;
				}
				goto CONT;

			} else if (*args_spec == '\'') {
				/* single quote,
				 * preserve all characters quoted. */
				p = ++args_spec;
		LINE_BREAK:	while ((*args_spec != '\0') &&
				       (*args_spec != '\'')) {
					args_spec++;
				}
				if (*args_spec == '\0') {
					/* closing quote not found */
					if (*(args_spec - 1) == '\\') {
						line = strtok_r(NULL, "\n",
								&ptrptr);
						if (line) {
							line_num++;
							args_spec = line;
							goto LINE_BREAK;
						}
					}
					error("Program arguments specification format invalid: %s.",
					      prog_argv[prog_argc - 1]);
					goto fail;
				}
				xstrncat(arg_buf, p, (args_spec - p));
				args_spec++;
				goto CONT;

			} else {
				/* space */
				while ((*args_spec != '\0') &&
				       isspace(*args_spec)) {
					args_spec++;
				}
			}

		}
		if (arg_buf) {
			prog_argv[prog_argc++] = arg_buf;
			arg_buf = NULL;
		}

		for (i = 2; i < global_argc; i++) {
			if ((prog_argc + 1) >= MAX_ARGC) {
				info("Exceeded multi-prog argc limit");
				break;
			}
			prog_argv[prog_argc++] = xstrdup(global_argv[i]);
		}
		prog_argv[prog_argc] = NULL;

		*argc = prog_argc;
		*argv = prog_argv;
		xfree(local_data);
		return 0;
	}

	error("Program for task rank %d not specified.", task_rank);
fail:
	xfree(local_data);
	*argc = 1;
	prog_argv[0] = NULL;
	*argv = prog_argv;
	return -1;
}
