/****************************************************************************\
 *  opts.c - smap command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "src/smap/smap.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_time.h"
#include "src/common/xstring.h"

/* FUNCTIONS */
static void _help(void);
static void _usage(void);

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char **argv)
{
	int opt_char;
	int option_index;
	int tmp = 0;

	static struct option long_options[] = {
		{"commandline", no_argument, 0, 'c'},
		{"display", required_argument, 0, 'D'},
		{"noheader", no_argument, 0, 'h'},
		{"iterate", required_argument, 0, 'i'},
		{"cluster", required_argument, 0, 'M'},
		{"clusters",required_argument, 0, 'M'},
		{"nodes", required_argument, 0, 'n'},
		{"quiet", no_argument, 0, 'Q'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, OPT_LONG_HELP},
		{"usage", no_argument, 0, OPT_LONG_USAGE},
		{"show_hidden", no_argument, 0, 'H'},
		{NULL, 0, 0, 0}
	};

	memset(&params, 0, sizeof(params));

	while ((opt_char =
		getopt_long(argc, argv, "cD:hHi:M:n:QvV",
			    long_options, &option_index)) != -1) {
		switch (opt_char) {
		case '?':
			fprintf(stderr,
				"Try \"smap --help\" for more information\n");
			exit(1);
			break;
		case 'c':
			params.commandline = true;
			break;
		case 'D':
			if (!xstrcmp(optarg, "j"))
				tmp = JOBS;
			else if (!xstrcmp(optarg, "s"))
				tmp = PARTITION;
			else if (!xstrcmp(optarg, "r"))
				tmp = RESERVATIONS;

			params.display = tmp;
			break;
		case 'h':
			params.no_header = true;
			break;
		case 'H':
			params.all_flag = true;
			break;
		case 'i':
			params.iterate = atoi(optarg);
			if (params.iterate <= 0) {
				error("Error: --iterate=%s", optarg);
				exit(1);
			}
			break;
		case 'M':
			FREE_NULL_LIST(params.clusters);
			if (!(params.clusters =
			      slurmdb_get_info_cluster(optarg))) {
				print_db_notok(optarg, 0);
				exit(1);
			}
			working_cluster_rec = list_peek(params.clusters);
			break;
		case 'n':
			/*
			 * confirm valid nodelist entry
			 */
			params.hl = hostlist_create(optarg);
			if (!params.hl) {
				error("'%s' invalid entry for --nodes",
				      optarg);
				exit(1);
			}
			break;
		case 'Q':
			quiet_flag = 1;
			break;
		case 'v':
			params.verbose++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
		case OPT_LONG_HELP:
			_help();
			exit(0);
		case OPT_LONG_USAGE:
			_usage();
			exit(0);
		}
	}

	params.cluster_dims = slurmdb_setup_cluster_dims();
	if (params.cluster_dims > 3)
		fatal("smap is unable to support more than three dimensions");
	params.cluster_base = hostlist_get_base(params.cluster_dims);
	params.cluster_flags = slurmdb_setup_cluster_flags();
}

extern void print_date(void)
{
	time_t now_time = time(NULL);

	if (params.commandline) {
		printf("%s", slurm_ctime(&now_time));
	} else {
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%s",
			  slurm_ctime2(&now_time));
		main_ycord++;
	}
}

extern void clear_window(WINDOW *win)
{
	int x, y;
	for (x = 0; x < getmaxx(win); x++)
		for (y = 0; y < getmaxy(win); y++) {
			mvwaddch(win, y, x, ' ');
		}
	wmove(win, 1, 1);
	wnoutrefresh(win);
}

static void _usage(void)
{
	printf("Usage: smap [-chQV] [-D jrs] [-i seconds] [-n nodelist] "
	       "[-M cluster_name]\n");
}

static void _help(void)
{
	printf("\
Usage: smap [OPTIONS]\n\
  -c, --commandline          output written with straight to the\n\
                             commandline.\n\
  -D, --display              set which display mode to use\n\
                             j = jobs\n\
                             r = reservations\n\
                             s = partitions\n\
  -h, --noheader             no headers on output\n\
  -H, --show_hidden          display hidden partitions and their jobs\n\
  -i, --iterate=seconds      specify an interation period\n\
  -M, --cluster=cluster_name cluster to issue commands to.  Default is\n\
                             current cluster.  cluster with no name will\n\
                             reset to default.\n\
                             NOTE: SlurmDBD must be up.\n\
  -n, --nodes=[nodes]        only show objects with these nodes.\n\
  -V, --version              output version information and exit\n\
\nHelp options:\n\
  --help                     show this help message\n\
  --usage                    display brief usage message\n");
}
