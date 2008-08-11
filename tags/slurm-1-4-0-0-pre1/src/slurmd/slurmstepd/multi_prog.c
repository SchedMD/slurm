/*****************************************************************************\
 *  multi_prog.c - Find the argv array for each task when multi-prog is enabled.
 *
 *  NOTE: This code could be moved into the API if desired. That would mean the
 *  logic would be executed once per job instead of once per task. This would
 *  require substantial modifications to the srun, slurmd, slurmstepd, and
 *  communications logic; so we'll stick with the simple solution for now. 
 *****************************************************************************
 *  Produced at National University of Defense Technology (China)
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>
 *  and
 *  Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>,
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

/* substitute "%t" or "%o" in argument with task number or range offset */
static void
_sub_expression(char *args_spec, int task_rank, int task_offset)
{
	char tmp[BUF_SIZE];

	if (args_spec[0] == '%') {
		if (args_spec[1] == 't') {
			/* task rank */
			strcpy(tmp, &args_spec[2]);
			sprintf(args_spec, "%d%s", task_rank, tmp);
		} else if (args_spec[1] == 'o') {
			/* task offset */
			strcpy(tmp, &args_spec[2]);
			sprintf(args_spec, "%d%s", task_offset, tmp);
		}
	}
}

/*
 * FIXME - It would be nice to parse the multi-prog array just once
 *	to retrieve the argv arrays for each task on this node, rather
 *	than calling multi_prog_get_argv once for each task.
 */
extern int
multi_prog_get_argv(char *file_contents, char **prog_env, int task_rank,
		    int *argc, char ***argv)
{
	char *line = NULL;
	int line_num = 0;
	int task_offset;
	char *p = NULL, *s = NULL, *ptrptr = NULL;
	char *rank_spec = NULL, *args_spec = NULL;
	int prog_argc = 0;
	char **prog_argv = NULL;
	char *local_data = NULL;

	prog_argv = (char **)xmalloc(sizeof(char *) * 128);

	if (task_rank < 0) {
		error("Invalid task rank %d", task_rank);
		*argc = 1;
		*argv = prog_argv;
		return -1;
	}

	local_data = xstrdup(file_contents);

	line = strtok_r(local_data, "\n", &ptrptr);
	while (line) {
		if (line_num > 0)
			line = strtok_r(NULL, "\n", &ptrptr);
		if (line == NULL) {
			error("No executable program specified for this task");
			goto fail;
		}
		line_num ++;
		if (strlen (line) >= (BUF_SIZE - 1)) {
			error ("Line %d of configuration file too long", 
				line_num);
			goto fail;
		}
		
		p = line;
		while (*p != '\0' && isspace (*p)) /* remove leading spaces */
			p ++;
		
		if (*p == '#') /* only whole-line comments handled */
			continue;

		if (*p == '\0') /* blank line ignored */
			continue;
		
		rank_spec = p;

		while (*p != '\0' && !isspace (*p))
			p ++;
		if (*p == '\0') {
			error("Invalid configuration line: %s", line);
			goto fail;
		}
		*p ++ = '\0';

		if (!_in_range (task_rank, rank_spec, &task_offset))
			continue;

		/* skip all whitspace after the range spec */
		while(*p != '\0' && isspace (*p))
			p++;

		args_spec = p;
		while (*args_spec != '\0') { 
			/* Only simple quote and escape supported */
			prog_argv[prog_argc ++] = args_spec;
		CONT:	while (*args_spec != '\0' && *args_spec != '\\'
			&&     *args_spec != '%'
			&&     *args_spec != '\'' && !isspace (*args_spec)) {
			        args_spec ++;
		        }
			if (*args_spec == '\0') {
				/* the last argument */
				break;

			} else if (*args_spec == '%') {
				_sub_expression(args_spec, task_rank, 
					task_offset);
				args_spec ++;
				goto CONT;

			} else if (*args_spec == '\\') {
				/* escape, just remove the backslash */
				s = args_spec ++;
				p = args_spec;
				do {
					*s ++ = *p;
				} while (*p ++ != '\0');
				goto CONT;
				
			} else if (*args_spec == '\'') {
				/* single quote, 
				 * preserve all characters quoted. */
				p = args_spec + 1;
				while (*p != '\0' && *p != '\'') {
					/* remove quote */
					*args_spec ++ = *p ++;
				}
				if (*p == '\0') {
					/* closing quote not found */
					error("Program arguments specification"
						" format invalid: %s.", 
						prog_argv[prog_argc -1]);
					goto fail;
				}
				p ++; /* skip closing quote */
				s = args_spec;
				do {
					*s ++ = *p;
				} while (*p ++ != '\0');
				goto CONT;
				
			} else {
				/* space */
				*args_spec ++ = '\0';
				while (*args_spec != '\0' 
				&& isspace (*args_spec))
					args_spec ++;
			}

		}

		prog_argv[prog_argc] = NULL;

		*argc = prog_argc;
		*argv = prog_argv;
		/* FIXME - local_data is leaked */
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
