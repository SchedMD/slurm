/*****************************************************************************\
 *  xlate.c - translate #BSUB and #PBS options for sbatch
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2018 SchedMD LLC <https://www.schedmd.com>
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
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

#define _GNU_SOURCE

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>		/* getenv     */
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "src/common/cpu_frequency.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/util-net.h"

#include "src/sbatch/opt.h"

/* Wrapper functions */
static void _set_pbs_options(int argc, char **argv);
static void _set_bsub_options(int argc, char **argv);

static char *_xlate_pbs_mail_type(const char *arg);
static void _parse_pbs_resource_list(char *rl);

/*
 * set wrapper (ie. pbs, bsub) options from batch script
 *
 * Build an argv-style array of options from the script "body",
 * then pass the array to _set_*_options for further parsing.
 */
extern bool xlate_batch_script(const char *file, const void *body,
			       int size, int magic)
{
	char *magic_word;
	void (*wrp_func) (int,char**) = NULL;
	int magic_word_len;
	int argc;
	char **argv;
	void *state = NULL;
	char *line;
	char *option;
	char *ptr;
	int skipped = 0;
	int lineno = 0;
	int non_comments = 0;
	int i;
	bool found = false;

	/* Check what command it is */
	switch (magic) {
	case WRPR_BSUB:
		magic_word = "#BSUB";
		wrp_func = _set_bsub_options;
		break;
	case WRPR_PBS:
		magic_word = "#PBS";
		wrp_func = _set_pbs_options;
		break;

	default:
		return false;
	}

	magic_word_len = strlen(magic_word);
	/* getopt_long skips over the first argument, so fill it in */
	argc = 1;
	argv = xmalloc(sizeof(char *));
	argv[0] = "sbatch";

	while ((line = next_line(body, size, &state)) != NULL) {
		lineno++;
		if (xstrncmp(line, magic_word, magic_word_len) != 0) {
			if (line[0] != '#')
				non_comments++;
			xfree(line);
			if (non_comments > 100)
				break;
			continue;
		}

		/* Set found to be true since we found a valid command */
		found = true;
		/* this line starts with the magic word */
		ptr = line + magic_word_len;
		while ((option = get_argument(file, lineno, ptr, &skipped))) {
			debug2("Found in script, argument \"%s\"", option);
			argc += 1;
			xrealloc(argv, sizeof(char*) * argc);

			/* Only check the even options here (they are
			 * the - options) */
			if (magic == WRPR_BSUB && !(argc%2)) {
				/* Since Slurm doesn't allow long
				 * names with a single '-' we must
				 * translate before hand.
				 */
				if (!xstrcmp("-cwd", option)) {
					xfree(option);
					option = xstrdup("-c");
				}
			}

			argv[argc-1] = option;
			ptr += skipped;
		}
		xfree(line);
	}

	if ((argc > 0) && (wrp_func != NULL))
		wrp_func(argc, argv);

	for (i = 1; i < argc; i++)
		xfree(argv[i]);
	xfree(argv);

	return found;
}

static void _set_bsub_options(int argc, char **argv) {

	int opt_char, option_index = 0;
	char *bsub_opt_string = "+c:e:J:m:M:n:o:q:W:x";
	char *char_ptr;

	struct option bsub_long_options[] = {
		{"cwd", required_argument, 0, 'c'},
		{"error_file", required_argument, 0, 'e'},
		{"job_name", required_argument, 0, 'J'},
		{"hostname", required_argument, 0, 'm'},
		{"memory_limit", required_argument, 0, 'M'},
		{"output_file", required_argument, 0, 'o'},
		{"queue_name", required_argument, 0, 'q'},
		{"time", required_argument, 0, 'W'},
		{"exclusive", no_argument, 0, 'x'},
		{NULL, 0, 0, 0}
	};

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, bsub_opt_string,
				       bsub_long_options, &option_index))
	       != -1) {
		int xlate_val = 0;
		char *xlate_arg = NULL;

		switch (opt_char) {
		case 'c':
			xlate_val = 'D';
			xlate_arg = xstrdup(optarg);
			break;
		/* These options all have a direct correspondance. */
		case 'e':
		case 'J':
		case 'o':
			xlate_val = opt_char;
			xlate_arg = xstrdup(optarg);
			break;
		case 'm':
			xlate_val = 'w';
			xlate_arg = xstrdup(optarg);
			/*
			 * Since BSUB uses a list of space separated hosts,
			 * we need to replace the spaces with commas.
			 */
			while ((char_ptr = strstr(xlate_arg, " ")))
				*char_ptr = ',';
			break;
		case 'M':
			xlate_val = LONG_OPT_MEM_PER_CPU;
			xlate_arg = xstrdup(optarg);
			break;
		case 'n':
			/* Since it is valid in bsub to give a min and
			 * max task count we will only read the max if
			 * it exists.
			 */
			char_ptr = strstr(optarg, ",");
			if (char_ptr) {
				char_ptr++;
				if (!char_ptr[0]) {
					error("#BSUB -n format not correct "
					      "given: '%s'",
					      optarg);
					exit(error_exit);
				}
			} else
				char_ptr = optarg;

			xlate_val = 'n';
			xlate_arg = xstrdup(char_ptr);

			break;
		case 'q':
			xlate_val = 'p';
			xlate_arg = xstrdup(optarg);
			break;
		case 'W':
			xlate_val = 't';
			xlate_arg = xstrdup(optarg);
			break;
		case 'x':
			xlate_val = LONG_OPT_EXCLUSIVE;
			break;
		default:
			error("Unrecognized command line parameter %c",
			      opt_char);
			exit(error_exit);
		}

		if (xlate_val)
			slurm_process_option(&opt, xlate_val, xlate_arg,
					     false, false);
		xfree(xlate_arg);
	}


	if (optind < argc) {
		error("Invalid argument: %s", argv[optind]);
		exit(error_exit);
	}

}

static void _set_pbs_options(int argc, char **argv)
{
	int opt_char, option_index = 0;
	char *pbs_opt_string = "+a:A:c:C:e:hIj:J:k:l:m:M:N:o:p:q:r:S:t:u:v:VW:z";

	struct option pbs_long_options[] = {
		{"start_time", required_argument, 0, 'a'},
		{"account", required_argument, 0, 'A'},
		{"checkpoint", required_argument, 0, 'c'},
		{"working_dir", required_argument, 0, 'C'},
		{"error", required_argument, 0, 'e'},
		{"hold", no_argument, 0, 'h'},
		{"interactive", no_argument, 0, 'I'},
		{"join", optional_argument, 0, 'j'},
		{"job_array", required_argument, 0, 'J'},
		{"keep", required_argument, 0, 'k'},
		{"resource_list", required_argument, 0, 'l'},
		{"mail_options", required_argument, 0, 'm'},
		{"mail_user_list", required_argument, 0, 'M'},
		{"job_name", required_argument, 0, 'N'},
		{"out", required_argument, 0, 'o'},
		{"priority", required_argument, 0, 'p'},
		{"destination", required_argument, 0, 'q'},
		{"rerunable", required_argument, 0, 'r'},
		{"script_path", required_argument, 0, 'S'},
		{"array", required_argument, 0, 't'},
		{"running_user", required_argument, 0, 'u'},
		{"variable_list", required_argument, 0, 'v'},
		{"all_env", no_argument, 0, 'V'},
		{"attributes", required_argument, 0, 'W'},
		{"no_std", no_argument, 0, 'z'},
		{NULL, 0, 0, 0}
	};

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, pbs_opt_string,
				       pbs_long_options, &option_index))
	       != -1) {
		int xlate_val = 0;
		char *xlate_arg = NULL;

		switch (opt_char) {
		case 'a':
			xlate_val = 'b';
			xlate_arg = xstrdup(optarg);
			break;
		/* These options all have a direct correspondance. */
		case 'A':
		case 'e':
		case 'o':
			xlate_val = opt_char;
			xlate_arg = xstrdup(optarg);
			break;
		case 'c':
			break;
		case 'C':
			break;
		case 'h':
			xlate_val = 'H';
			break;
		case 'I':
			break;
		case 'j':
			break;
		case 'J':
		case 't':
			/* PBS Pro uses -J. Torque uses -t. */
			xlate_val = 'a';
			xlate_arg = xstrdup(optarg);
			break;
		case 'k':
			break;
		case 'l':
			_parse_pbs_resource_list(optarg);
			break;
		case 'm':
			if (!optarg) /* CLANG Fix */
				break;
			xlate_val = LONG_OPT_MAIL_TYPE;
			xlate_arg = _xlate_pbs_mail_type(optarg);
			break;
		case 'M':
			xlate_val = LONG_OPT_MAIL_USER;
			xlate_arg = xstrdup(optarg);
			break;
		case 'N':
			xlate_val = 'J';
			xlate_arg = xstrdup(optarg);
			break;
		case 'p':
			xlate_val = LONG_OPT_NICE;
			xlate_arg = xstrdup(optarg);
			break;
		case 'q':
			xlate_val = 'p';
			xlate_arg = xstrdup(optarg);
			break;
		case 'r':
			break;
		case 'S':
			break;
		case 'u':
			break;
		case 'v':
			xlate_val = LONG_OPT_EXPORT;
			xlate_arg = xstrdup(sbopt.export_env);
			if (xlate_arg)
				xstrcat(xlate_arg, ",");
			xstrcat(xlate_arg, optarg);
			break;
		case 'V':
			break;
		case 'W':
			if (!optarg) /* CLANG Fix */
				break;
			if (!xstrncasecmp(optarg, "umask=", 6)) {
				xlate_val = LONG_OPT_UMASK;
				xlate_arg = xstrdup(optarg+6);
			} else if (!xstrncasecmp(optarg, "depend=", 7)) {
				xlate_val = 'd';
				xlate_arg = xstrdup(optarg+7);
			} else {
				verbose("Ignored PBS attributes: %s", optarg);
			}
			break;
		case 'z':
			break;
		default:
			error("Unrecognized command line parameter %c",
			      opt_char);
			exit(error_exit);
		}

		if (xlate_val)
			slurm_process_option(&opt, xlate_val, xlate_arg,
					     false, false);
		xfree(xlate_arg);
	}

	if (optind < argc) {
		error("Invalid argument: %s", argv[optind]);
		exit(error_exit);
	}
}

static char *_get_pbs_node_name(char *node_options, int *i)
{
	int start = (*i);
	char *value = NULL;

	while (node_options[*i] &&
	       (node_options[*i] != '+') &&
	       (node_options[*i] != ':'))
		(*i)++;

	value = xmalloc((*i)-start+1);
	memcpy(value, node_options+start, (*i)-start);

	if (node_options[*i])
		(*i)++;

	return value;
}

static void _get_next_pbs_node_part(char *node_options, int *i)
{
	while (node_options[*i] &&
	       (node_options[*i] != '+') &&
	       (node_options[*i] != ':'))
		(*i)++;
	if (node_options[*i])
		(*i)++;
}

static void _parse_pbs_nodes_opts(char *node_opts)
{
	int i = 0;
	char *temp = NULL;
	int ppn = 0;
	int node_cnt = 0;
	hostlist_t hl = hostlist_create(NULL);

	while (node_opts[i]) {
		if (!xstrncmp(node_opts+i, "ppn=", 4)) {
			i+=4;
			ppn += strtol(node_opts+i, NULL, 10);
			_get_next_pbs_node_part(node_opts, &i);
		} else if (isdigit(node_opts[i])) {
			node_cnt += strtol(node_opts+i, NULL, 10);
			_get_next_pbs_node_part(node_opts, &i);
		} else if (isalpha(node_opts[i])) {
			temp = _get_pbs_node_name(node_opts, &i);
			hostlist_push_host(hl, temp);
			xfree(temp);
		} else
			i++;

	}

	if (!node_cnt)
		node_cnt = 1;
	else {
		char *nodes = xstrdup_printf("%d", node_cnt);
		slurm_process_option(&opt, 'N', nodes, false, false);
		xfree(nodes);
	}

	if (ppn) {
		char *ntasks;
		ppn *= node_cnt;
		ntasks = xstrdup_printf("%d", ppn);
		slurm_process_option(&opt, 'n', ntasks, false, false);
	}

	if (hostlist_count(hl) > 0) {
		char *nodelist = hostlist_ranged_string_xmalloc(hl);
		slurm_process_option(&opt, 'w', nodelist, false, false);
		xfree(nodelist);
	}

	hostlist_destroy(hl);
}

static void _get_next_pbs_option(char *pbs_options, int *i)
{
	while (pbs_options[*i] && pbs_options[*i] != ',')
		(*i)++;
	if (pbs_options[*i])
		(*i)++;
}

static char *_get_pbs_option_value(char *pbs_options, int *i, char sep)
{
	int start = (*i);
	char *value = NULL;

	while (pbs_options[*i] && pbs_options[*i] != sep)
		(*i)++;
	value = xmalloc((*i)-start+1);
	memcpy(value, pbs_options+start, (*i)-start);

	if (pbs_options[*i])
		(*i)++;

	return value;
}

static void _parse_pbs_resource_list(char *rl)
{
	int i = 0;
	int gpus = 0;
	char *temp = NULL;
	int pbs_pro_flag = 0;	/* Bits: select:1 ncpus:2 mpiprocs:4 */

	while (rl[i]) {
		if (!xstrncasecmp(rl+i, "accelerator=", 12)) {
			i += 12;
			if (!xstrncasecmp(rl+i, "true", 4) && (gpus < 1))
				gpus = 1;
			/* Also see "naccelerators=" below */
		} else if (!xstrncmp(rl+i, "arch=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "cput=", 5)) {
			i+=5;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for cput");
				exit(error_exit);
			}
			slurm_process_option(&opt, 't', temp, false, false);
			xfree(temp);
		} else if (!xstrncmp(rl+i, "file=", 5)) {
			int end = 0;

			i+=5;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for file");
				exit(error_exit);
			}
			end = strlen(temp) - 1;
			if (toupper(temp[end]) == 'B') {
				/* In Torque they do GB or MB on the
				 * end of size, we just want G or M so
				 * we will remove the b on the end
				 */
				temp[end] = '\0';
			}
			slurm_process_option(&opt, LONG_OPT_TMP, temp,
					     false, false);
			xfree(temp);
		} else if (!xstrncmp(rl+i, "host=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "mem=", 4)) {
			int end = 0;

			i+=4;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for mem");
				exit(error_exit);
			}
			end = strlen(temp) - 1;
			if (toupper(temp[end]) == 'B') {
				/* In Torque they do GB or MB on the
				 * end of size, we just want G or M so
				 * we will remove the b on the end
				 */
				temp[end] = '\0';
			}
			slurm_process_option(&opt, LONG_OPT_MEM, temp,
					     false, false);
			xfree(temp);
		} else if (!xstrncasecmp(rl+i, "mpiprocs=", 9)) {
			i += 9;
			temp = _get_pbs_option_value(rl, &i, ':');
			if (temp) {
				pbs_pro_flag |= 4;
				slurm_process_option(&opt,
						     LONG_OPT_NTASKSPERNODE,
						     temp, false, false);
				xfree(temp);
			}
#ifdef HAVE_NATIVE_CRAY
			/*
			 * NB: no "mppmem" here since it specifies per-PE memory units,
			 *     whereas Slurm uses per-node and per-CPU memory units.
			 */
		} else if (!xstrncmp(rl + i, "mppdepth=", 9)) {
			/* Cray: number of CPUs (threads) per processing element */
			i += 9;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (temp) {
				slurm_process_option(&opt, 'c', temp,
						     false, false);
			}
			xfree(temp);
		} else if (!xstrncmp(rl + i, "mppnodes=", 9)) {
			/* Cray `nodes' variant: hostlist without prefix */
			i += 9;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for mppnodes");
				exit(error_exit);
			}
			slurm_process_option(&opt, 'w', temp, false, false);
		} else if (!xstrncmp(rl + i, "mppnppn=", 8)) {
			/* Cray: number of processing elements per node */
			i += 8;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (temp)
				slurm_process_option(&opt,
						     LONG_OPT_NTASKSPERNODE,
						     temp, false, false);
			xfree(temp);
		} else if (!xstrncmp(rl + i, "mppwidth=", 9)) {
			/* Cray: task width (number of processing elements) */
			i += 9;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (temp)
				slurm_process_option(&opt, 'n', temp,
						     false, false);
			xfree(temp);
#endif /* HAVE_NATIVE_CRAY */
		} else if (!xstrncasecmp(rl+i, "naccelerators=", 14)) {
			i += 14;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (temp) {
				slurm_process_option(&opt, 'G', temp,
						     false, false);
				xfree(temp);
			}
		} else if (!xstrncasecmp(rl+i, "ncpus=", 6)) {
			i += 6;
			temp = _get_pbs_option_value(rl, &i, ':');
			if (temp) {
				pbs_pro_flag |= 2;
				slurm_process_option(&opt, LONG_OPT_MINCPUS,
						     temp, false, false);
				xfree(temp);
			}
		} else if (!xstrncmp(rl+i, "nice=", 5)) {
			i += 5;
			temp = _get_pbs_option_value(rl, &i, ',');
			slurm_process_option(&opt, LONG_OPT_NICE, temp,
					     false, false);
			xfree(temp);
		} else if (!xstrncmp(rl+i, "nodes=", 6)) {
			i+=6;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for nodes");
				exit(error_exit);
			}
			_parse_pbs_nodes_opts(temp);
			xfree(temp);
		} else if (!xstrncmp(rl+i, "opsys=", 6)) {
			i+=6;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "other=", 6)) {
			i+=6;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "pcput=", 6)) {
			i+=6;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for pcput");
				exit(error_exit);
			}
			slurm_process_option(&opt, 't', temp, false, false);
			xfree(temp);
		} else if (!xstrncmp(rl+i, "pmem=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "proc=", 5)) {
			i += 5;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (opt.constraint)
				xstrfmtcat(temp, ",%s", opt.constraint);
			slurm_process_option(&opt, 'C', temp, false, false);
			xfree(temp);
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "pvmem=", 6)) {
			i+=6;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncasecmp(rl+i, "select=", 7)) {
			i += 7;
			temp = _get_pbs_option_value(rl, &i, ':');
			if (temp) {
				pbs_pro_flag |= 1;
				slurm_process_option(&opt, 'N', temp, false, false);
				xfree(temp);
			}
		} else if (!xstrncmp(rl+i, "software=", 9)) {
			i+=9;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "vmem=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "walltime=", 9)) {
			i+=9;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for walltime");
				exit(error_exit);
			}
			slurm_process_option(&opt, 't', temp, false, false);
			xfree(temp);
		} else
			i++;
	}

	if ((pbs_pro_flag == 7) && (opt.pn_min_cpus > opt.ntasks_per_node)) {
		/* This logic will allocate the proper CPU count on each
		 * node if the CPU count per node is evenly divisible by
		 * the task count on each node. Slurm can't handle something
		 * like cpus_per_node=10 and ntasks_per_node=8 */
		int cpus_per_task = opt.pn_min_cpus / opt.ntasks_per_node;
		temp = xstrdup_printf("%d", cpus_per_task);
		slurm_process_option(&opt, 'c', temp, false, false);
		xfree(temp);
	}
	if (gpus > 0) {
		if (opt.gres)
			temp = xstrdup_printf("%s,gpu:%d", opt.gres, gpus);
		else
			temp = xstrdup_printf("gpu:%d", gpus);
		slurm_process_option(&opt, LONG_OPT_GRES, temp, false, false);
		xfree(temp);
	}
}

static char *_xlate_pbs_mail_type(const char *arg)
{
	char *xlated = NULL;

	if (strchr(arg, 'b') || strchr(arg, 'B'))
		xstrfmtcat(xlated, "%sBEGIN", (xlated ? "," : ""));
	if (strchr(arg, 'e') || strchr(arg, 'E'))
		xstrfmtcat(xlated, "%sEND", (xlated ? "," : ""));
	if (strchr(arg, 'a') || strchr(arg, 'A'))
		xstrfmtcat(xlated, "%sFAIL", (xlated ? "," : ""));

	if (strchr(arg, 'n') || strchr(arg, 'N')) {
		xfree(xlated);
		xlated = xstrdup("NONE");
	}

	return xlated;
}
