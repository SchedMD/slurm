/*****************************************************************************\
 *  sexec.c - execute program according to task rank
 *
 *  NOTE: This logic could be moved directly into slurmstepd if desired to 
 *  eliminate an extra exec() call, but could be more confusing to users.
 *****************************************************************************
 *  Produced at National University of Defense Technology (China)
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>
 *  UCRL-CODE-217948.
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

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#define BUF_SIZE 256

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
 
int
main(int argc, char** argv)
{
	FILE* conf_file;
	char line[BUF_SIZE];
	int line_num = 0;
	int task_rank, task_offset;
	char* p, *s;
	char* rank_spec, *prog_spec, *args_spec;
	int prog_argc;
	char* prog_argv[(BUF_SIZE - 4)/ 2];
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;

	log_init (xbasename(argv[0]), logopt, 0, NULL);

	if (argc != 2) {
		fatal ("Usage: sexec config_file\n");
		exit (-1);
	}

	p = getenv ("SLURM_PROCID");
        if (p == NULL)
                p = getenv ("PMI_RANK");
        if (p != NULL) {
                task_rank = atoi (p);
        } else {
		fatal ("Task rank unknown.");
		exit(-1);
	}

	conf_file = fopen (argv[1], "r");
	if (conf_file == NULL) {
		fatal ("Unable to open config_file \"%s\": %m", argv[1]);
		exit (-1);
	}
	
	while (fgets (line, BUF_SIZE, conf_file) != NULL) {
		line_num ++;
		if (strlen (line) >= (BUF_SIZE - 1)) {
			error ("Line %d of configuration file %s too long", 
				line_num, argv[1]);
			fclose (conf_file);
			exit (-1);
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
			fatal ("Invalid configuration line: %s", line);
			exit (-1);
		}
		*p ++ = '\0';

		if (!_in_range (task_rank, rank_spec, &task_offset))
			continue;

		while(*p != '\0' && isspace (*p))
			p ++;
		prog_spec = p;

		if (prog_spec[0] == '\0') {
			fatal ("Program for task rank %d not specified.", 
				task_rank);
			exit (-1);
		}
		
		while (*p != '\0' && !isspace (*p))
			p ++;
		*p ++ = '\0';

		while (*p != '\0' && isspace (*p))
			p ++;
		args_spec = p;

		prog_argv[0] = prog_spec; 
		prog_argc = 1;
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
					fatal ("Program arguments specification"
						" format invalid: %s.", 
						prog_argv[prog_argc -1]);
					exit (-1);
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
		if (execvp (prog_spec, prog_argv) == -1) {
			fatal ("Error executing program \"%s\": %m", prog_spec);
			exit(-1);
		}
	}

	fclose (conf_file);
	fatal ("Program for task rank %d not specified.", task_rank);
	return -1;
}
