/****************************************************************************\
 *  opts.c - strigger command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"
#include "src/common/uid.h"

#include "src/strigger/strigger.h"

#define OPT_LONG_HELP      0x100
#define OPT_LONG_USAGE     0x101
#define OPT_LONG_SET       0x102
#define OPT_LONG_GET       0x103
#define OPT_LONG_CLEAR     0x104
#define OPT_LONG_USER      0x105
#define OPT_LONG_BLOCK_ERR 0x106
#define OPT_LONG_FRONT_END 0x107
#define OPT_LONG_FLAGS     0x108

/* getopt_long options, integers but not characters */

/* FUNCTIONS */
static void     _help( void );
static void     _init_options( void );
static void     _print_options( void );
static void     _usage( void );
static void     _validate_options( void );

struct strigger_parameters params;

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char *argv[])
{
	int opt_char;
	int option_index;
	uid_t some_uid;
	long tmp_l;
	static struct option long_options[] = {
		{"primary_slurmctld_failure",           no_argument, 0, 'a'},
		{"primary_slurmctld_resumed_operation", no_argument, 0, 'A'},
		{"primary_slurmctld_resumed_control",   no_argument, 0, 'b'},
		{"backup_slurmctld_failure",            no_argument, 0, 'B'},
		{"backup_slurmctld_resumed_operation",  no_argument, 0, 'c'},
		{"backup_slurmctld_assumed_control",    no_argument, 0, 'C'},
		{"down",                                no_argument, 0, 'd'},
		{"drained",                             no_argument, 0, 'D'},
		{"primary_slurmctld_acct_buffer_full",  no_argument, 0, 'e'},
		{"fini",                                no_argument, 0, 'f'},
		{"fail",                                no_argument, 0, 'F'},
		{"primary_slurmdbd_failure",            no_argument, 0, 'g'},
		{"primary_slurmdbd_resumed_operation",  no_argument, 0, 'G'},
		{"primary_database_failure",            no_argument, 0, 'h'},
		{"primary_database_resumed_operation",  no_argument, 0, 'H'},
		{"id",                            required_argument, 0, 'i'},
		{"idle",                                no_argument, 0, 'I'},
		{"jobid",                         required_argument, 0, 'j'},
		{"cluster",                       required_argument, 0, 'M'},
		{"clusters",                      required_argument, 0, 'M'},
		{"node",                          optional_argument, 0, 'n'},
		{"noheader",                            no_argument, 0, 'N'},
		{"offset",                        required_argument, 0, 'o'},
		{"program",                       required_argument, 0, 'p'},
		{"quiet",                               no_argument, 0, 'Q'},
		{"reconfig",                            no_argument, 0, 'r'},
		{"time",                                no_argument, 0, 't'},
		{"up",                                  no_argument, 0, 'u'},
		{"verbose",                             no_argument, 0, 'v'},
		{"version",                             no_argument, 0, 'V'},
		{"block_err", no_argument,       0, OPT_LONG_BLOCK_ERR},
		{"clear",     no_argument,       0, OPT_LONG_CLEAR},
		{"flags",     required_argument, 0, OPT_LONG_FLAGS},
		{"front_end", no_argument,       0, OPT_LONG_FRONT_END},
		{"get",       no_argument,       0, OPT_LONG_GET},
		{"help",      no_argument,       0, OPT_LONG_HELP},
		{"set",       no_argument,       0, OPT_LONG_SET},
		{"usage",     no_argument,       0, OPT_LONG_USAGE},
		{"user",      required_argument, 0, OPT_LONG_USER},
		{NULL,        0,                 0, 0}
	};

	_init_options();

	optind = 0;
	while ((opt_char = getopt_long(argc, argv,
				       "aAbBcCdDeFfgGhHi:Ij:M:n::No:p:QrtuvV",
				       long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, "Try \"strigger --help\" for "
				"more information\n");
			exit(1);
			break;
		case (int)'a':
			params.pri_ctld_fail = true;
			break;
		case (int)'A':
			params.pri_ctld_res_op = true;
			break;
		case (int)'b':
			params.pri_ctld_res_ctrl = true;
			break;
		case (int)'B':
			params.bu_ctld_fail = true;
			break;
		case (int)'c':
			params.bu_ctld_res_op = true;
			break;
		case (int)'C':
			params.bu_ctld_as_ctrl = true;
			break;
		case (int)'d':
			params.node_down = true;
			break;
		case (int)'D':
			params.node_drained = true;
			break;
		case (int)'e':
			params.pri_ctld_acct_buffer_full = true;
			break;
		case (int)'f':
			params.job_fini = true;
			break;
		case (int)'F':
			params.node_fail = true;
			break;
		case (int)'g':
			params.pri_dbd_fail = true;
			break;
		case (int)'G':
			params.pri_dbd_res_op = true;
			break;
		case (int)'h':
			params.pri_db_fail = true;
			break;
		case (int)'H':
			params.pri_db_res_op = true;
			break;
		case (int)'i':
			params.trigger_id = atoi(optarg);
			break;
		case (int)'I':
			params.node_idle = true;
			break;
		case (int)'j':
			tmp_l = atol(optarg);
			if (tmp_l <= 0) {
				error("Invalid jobid %s", optarg);
				exit(1);
			}
			params.job_id = tmp_l;
			break;
		case (int) 'M':
			if (params.clusters)
				list_destroy(params.clusters);
			if (!(params.clusters =
			      slurmdb_get_info_cluster(optarg))) {
				print_db_notok(optarg, 0);
				exit(1);
			}
			working_cluster_rec = list_peek(params.clusters);
			break;
		case (int)'n':
			xfree(params.node_id);
			if (optarg)
				params.node_id = xstrdup(optarg);
			else
				params.node_id = xstrdup("*");
			break;
		case (int)'N':
			params.no_header = true;
			break;
		case (int)'o':
			params.offset = atoi(optarg);
			break;
		case (int)'p':
			xfree(params.program);
			params.program = xstrdup(optarg);
			break;
		case (int)'Q':
			params.quiet = true;
			break;
		case (int)'r':
			params.reconfig = true;
			break;
		case (int)'t':
			params.time_limit = true;
			break;
		case (int)'u':
			params.node_up = true;
			break;
		case (int) 'v':
			params.verbose++;
			break;
		case (int) 'V':
			print_slurm_version();
			exit(0);
		case (int) OPT_LONG_BLOCK_ERR:
			params.block_err = true;
			break;
		case (int) OPT_LONG_HELP:
			_help();
			exit(0);
		case (int) OPT_LONG_USAGE:
			_usage();
			exit(0);
		case (int) OPT_LONG_CLEAR:
			params.mode_clear = true;
			break;
		case (int) OPT_LONG_FLAGS:
			if (!strncasecmp(optarg, "perm", 4))
				params.flags = TRIGGER_FLAG_PERM;
			else {
				error("Invalid flags %s", optarg);
				exit(1);
			}
			break;
		case (int) OPT_LONG_FRONT_END:
			params.front_end = true;
			break;
		case (int) OPT_LONG_GET:
			params.mode_get = true;
			break;
		case (int) OPT_LONG_SET:
			params.mode_set = true;
			break;
		case (int) OPT_LONG_USER:
			if ( uid_from_string( optarg, &some_uid ) != 0 ) {
				error("Invalid user %s", optarg);
				exit(1);
			}
			params.user_id = (uint32_t) some_uid;
			break;
		}
	}

	if (params.verbose)
		_print_options();
	_validate_options();
}

/* initialize the parameters */
static void _init_options( void )
{
	params.mode_set     = false;
	params.mode_get     = false;
	params.mode_clear   = false;

	params.block_err    = false;
	params.pri_ctld_fail = false;
	params.pri_ctld_res_op = false;
	params.pri_ctld_res_ctrl = false;
	params.pri_ctld_acct_buffer_full = false;
	params.bu_ctld_fail = false;
	params.bu_ctld_res_op = false;
	params.bu_ctld_as_ctrl = false;
	params.flags        = 0;
	params.front_end    = false;
	params.no_header    = false;
	params.node_down    = false;
	params.node_drained = false;
	params.node_fail    = false;
	params.node_idle    = false;
	params.trigger_id   = 0;
	params.job_fini     = false;
	params.pri_dbd_fail = false;
	params.pri_dbd_res_op = false;
	params.pri_db_fail = false;
	params.pri_db_res_op = false;
	params.job_id       = 0;
	params.node_id      = NULL;
	params.offset       = 0;
	params.program      = NULL;
	params.quiet        = false;
	params.reconfig     = false;
	params.time_limit   = false;
	params.node_up      = false;
	params.user_id      = NO_VAL;
	params.verbose      = 0;
}

/* print the parameters specified */
static void _print_options( void )
{
	verbose("-----------------------------");
	verbose("set          = %s", params.mode_set ? "true" : "false");
	verbose("get          = %s", params.mode_get ? "true" : "false");
	verbose("clear        = %s", params.mode_clear ? "true" : "false");
	verbose("block_err    = %s", params.block_err ? "true" : "false");
	verbose("flags        = %u", params.flags);
	verbose("front_end    = %s", params.front_end ? "true" : "false");
	verbose("job_id       = %u", params.job_id);
	verbose("job_fini     = %s", params.job_fini ? "true" : "false");
	verbose("no_header    = %s", params.no_header ? "true" : "false");
	verbose("node_down    = %s", params.node_down ? "true" : "false");
	verbose("node_drained = %s", params.node_drained ? "true" : "false");
	verbose("node_fail    = %s", params.node_fail ? "true" : "false");
	verbose("node_idle    = %s", params.node_idle ? "true" : "false");
	verbose("node_up      = %s", params.node_up ? "true" : "false");
	verbose("node         = %s", params.node_id);
	verbose("offset       = %d secs", params.offset);
	verbose("program      = %s", params.program);
	verbose("quiet        = %s", params.quiet ? "true" : "false");
	verbose("reconfig     = %s", params.reconfig ? "true" : "false");
	verbose("time_limit   = %s", params.time_limit ? "true" : "false");
	verbose("trigger_id   = %u", params.trigger_id);
	if (params.user_id == NO_VAL)
		verbose("user_id      = N/A");
	else
		verbose("user_id      = %u", params.user_id);
	verbose("verbose      = %d", params.verbose);
	verbose("primary_slurmctld_failure            = %s",
		params.pri_ctld_fail ? "true" : "false");
	verbose("primary_slurmctld_resumed_operation  = %s",
		params.pri_ctld_res_op ? "true" : "false");
	verbose("primary_slurmctld_resumed_control    = %s",
		params.pri_ctld_res_ctrl ? "true" : "false");
	verbose("primary_slurmctld_acct_buffer_full   = %s",
		params.pri_ctld_acct_buffer_full ? "true" : "false");
	verbose("backup_slurmctld_failure             = %s",
		params.bu_ctld_fail ? "true" : "false");
	verbose("backup_slurmctld_resumed_operation   = %s",
		params.bu_ctld_res_op ? "true" : "false");
	verbose("backup_slurmctld_as_ctrl             = %s",
		params.bu_ctld_as_ctrl ? "true" : "false");
	verbose("primary_slurmdbd_failure             = %s",
		params.pri_dbd_fail ? "true" : "false");
	verbose("primary_slurmdbd_resumed_operation   = %s",
		params.pri_dbd_res_op ? "true" : "false");
	verbose("primary_database_failure             = %s",
		params.pri_db_fail ? "true" : "false");
	verbose("primary_database_resumed_operation   = %s",
		params.pri_db_res_op ? "true" : "false");
	verbose("-----------------------------");
}

static void _validate_options( void )
{
	if ((params.mode_set + params.mode_get + params.mode_clear) != 1) {
		error("You must use exactly one of the following options: "
			"--set, --get or --clear");
		exit(1);
	}

	if (params.mode_clear && (params.user_id == NO_VAL) &&
	    (params.trigger_id == 0) && (params.job_id == 0)) {
		error("You must specify a --id, --jobid, or --user to clear");
		exit(1);
	}

	if (params.mode_set &&
	    ((params.node_down + params.node_drained + params.node_fail +
	      params.node_idle + params.node_up + params.reconfig +
	      params.job_fini  + params.time_limit + params.block_err +
     	      params.pri_ctld_fail  + params.pri_ctld_res_op  +
	      params.pri_ctld_res_ctrl  + params.pri_ctld_acct_buffer_full  +
 	      params.bu_ctld_fail + params.bu_ctld_res_op  +
	      params.bu_ctld_as_ctrl  + params.pri_dbd_fail  +
	      params.pri_dbd_res_op  + params.pri_db_fail  +
	      params.pri_db_res_op) == 0)) {
		error("You must specify a trigger (--block_err, --down, --up, "
			"--reconfig, --time, --fini,)\n"
			"--primary_slurmctld_failure,\n"
			"--primary_slurmctld_resumed_operation,\n"
			"--primary_slurmctld_resumed_control,\n"
	  		"--primary_slurmctld_acct_buffer_full,\n"
			"--backup_slurmctld_failure,\n"
			"--backup_slurmctld_resumed_operation,\n"
			"--backup_slurmctld_as_ctrl,\n"
			"--primary_slurmdbd_failure,\n"
			"--primary_slurmdbd_resumed_operation,\n"
			"--primary_database_failure, or \n"
			"--primary_database_resumed_operation)\n");
		exit(1);
	}

	if (params.mode_set && (params.program == NULL)) {
		error("You must specify a --program value");
		exit(1);
	}

	if (((params.job_fini + params.time_limit) != 0)
	&&  (params.job_id == 0)) {
		error("You must specify a --jobid value");
		exit(1);
	}

	if (params.program && (params.program[0] != '/')) {
		error("The --program value must start with \"/\"");
		exit(1);
	}

	if (params.program) {
		int i;
		struct stat buf;
		char *program = xstrdup(params.program);

		for (i = 0; program[i]; i++) {
			if (isspace(program[i])) {
				program[i] = '\0';
				break;
			}
		}

		if (stat(program, &buf)) {
			error("Invalid --program value, file not found");
			exit(1);
		}
		if (!S_ISREG(buf.st_mode)) {
			error("Invalid --program value, not regular file");
			exit(1);
		}
		xfree(program);
	}

	if ((params.offset < -32000) || (params.offset > 32000)) {
		error("The --offset parameter must be between +/-32000");
		exit(1);
	}
}

static void _usage( void )
{
	printf("Usage: strigger [--set | --get | --clear | --version] "
	       "[-aAbBcCdDefFgGhHiIjnNopQrtuv]\n");
}

static void _help( void )
{
	printf ("\
Usage: strigger [--set | --get | --clear] [OPTIONS]\n\
      --set           create a trigger\n\
      --get           get trigger information\n\
      --clear         delete a trigger\n\n\
      --block_err     trigger event on BlueGene block error\n\
      --front_end     trigger event on FrontEnd node state changes\n\
  -a, --primary_slurmctld_failure\n\
                      trigger event when primary slurmctld fails\n\
  -A, --primary_slurmctld_resumed_operation\n\
                      trigger event on primary slurmctld resumed operation\n\
                      after failure\n\
  -b, --primary_slurmctld_resumed_control\n\
                      trigger event on primary slurmctld resumed control\n\
  -B, --backup_slurmctld_failure\n\
                      trigger event when backup slurmctld fails\n\
  -c, --backup_slurmctld_resumed_operation\n\
                      trigger event when backup slurmctld resumed operation\n\
                      after failure\n\
  -C, --backup_slurmctld_assumed_control\n\
                      trigger event when backup slurmctld assumed control\n\
  -d, --down          trigger event when node goes DOWN\n\
  -D, --drained       trigger event when node becomes DRAINED\n\
  -e, --primary_slurmctld_acct_buffer_full\n\
                      trigger event when primary slurmctld acct buffer full\n\
  -F, --fail          trigger event when node is expected to FAIL\n\
  -f, --fini          trigger event when job finishes\n\
      --flags=perm    trigger event flag (perm = permanent)\n\n\
  -g, --primary_slurmdbd_failure\n\
                      trigger when primary slurmdbd fails\n\
  -G, --primary_slurmdbd_resumed_operation\n\
                      trigger when primary slurmdbd resumed operation after\n\
                      failure\n\
  -h, --primary_database_failure\n\
                      trigger when primary database fails\n\
  -H, --primary_database_resumed_operation\n\
                      trigger when primary database resumed operation after\n\
                      failure\n\
  -i, --id=#          a trigger's ID number\n\
  -I, --idle          trigger event when node remains IDLE\n\
  -j, --jobid=#       trigger related to specific jobid\n\
  -M, --cluster=name  cluster to issue commands to.  Default is\n\
                      current cluster.  cluster with no name will\n\
                      reset to default.\n\
  -n, --node[=host]   trigger related to specific node, all nodes by default\n\
  -N, --noheader      Do not print the message header\n\
  -o, --offset=#      trigger's offset time from event, negative to preceed\n\
  -p, --program=path  pathname of program to execute when triggered\n\
  -Q, --quiet         quiet mode (suppress informational messages)\n\
  -r, --reconfig      trigger event on configuration changes\n\
  -t, --time          trigger event on job's time limit\n\
  -u, --up            trigger event when node returned to service from DOWN \n\
                      state\n\
      --user          a user name or ID to filter triggers by\n\
  -v, --verbose       print detailed event logging\n\
  -V, --version       print version information and exit\n\
\nHelp options:\n\
  --help              show this help message\n\
  --usage             display brief usage message\n");
}
