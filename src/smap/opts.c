/****************************************************************************\
 *  opts.c - smap command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "src/smap/smap.h"
#include "src/common/proc_args.h"
#include "src/common/xstring.h"

/* FUNCTIONS */
static void _help(void);
static void _usage(void);

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char *argv[])
{
	int opt_char;
	int option_index;
	int tmp = 0;

	static struct option long_options[] = {
		{"commandline", no_argument, 0, 'c'},
		{"command", required_argument, 0, 'C'},
		{"display", required_argument, 0, 'D'},
		{"noheader", no_argument, 0, 'h'},
		{"iterate", required_argument, 0, 'i'},
		{"ionodes", required_argument, 0, 'I'},
		{"cluster", required_argument, 0, 'M'},
		{"clusters",required_argument, 0, 'M'},
		{"nodes", required_argument, 0, 'n'},
		{"quiet", no_argument, 0, 'Q'},
		{"resolve", required_argument, 0, 'R'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, OPT_LONG_HELP},
		{"usage", no_argument, 0, OPT_LONG_USAGE},
		{"show_hidden", no_argument, 0, 'H'},
		{NULL, 0, 0, 0}
	};

	memset(&params, 0, sizeof(params));

	while ((opt_char =
		getopt_long(argc, argv, "cC:D:hi:I:Hn:M:QR:vV",
			    long_options, &option_index)) != -1) {
		switch (opt_char) {
		case '?':
			fprintf(stderr,
				"Try \"smap --help\" for more information\n");
			exit(1);
			break;
		case 'c':
			params.commandline = TRUE;
			break;
		case 'C':
			params.command = xstrdup(optarg);
			break;
		case 'D':
			if (!strcmp(optarg, "j"))
				tmp = JOBS;
			else if (!strcmp(optarg, "s"))
				tmp = SLURMPART;
			else if (!strcmp(optarg, "b"))
				tmp = BGPART;
			else if (!strcmp(optarg, "c"))
				tmp = COMMANDS;
			else if (!strcmp(optarg, "r"))
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
		case 'I':
			/*
			 * confirm valid ionodelist entry (The 128 is
			 * a large number here to avoid having to do a
			 * lot more querying to figure out the correct
			 * pset size.  This number should be large enough.
			 */
			params.io_bit = bit_alloc(128);
			if (bit_unfmt(params.io_bit, optarg) == -1) {
				error("'%s' invalid entry for --ionodes",
				      optarg);
				exit(1);
			}
			break;
		case 'M':
			if (params.clusters)
				list_destroy(params.clusters);
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
		case 'R':
			params.commandline = TRUE;
			params.resolve = xstrdup(optarg);
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
	if (params.cluster_dims > 4)
		fatal("smap is unable to support more than four dimensions");
	params.cluster_base = hostlist_get_base(params.cluster_dims);
	params.cluster_flags = slurmdb_setup_cluster_flags();
}

extern void print_date(void)
{
	time_t now_time = time(NULL);

	if (params.commandline) {
		printf("%s", ctime(&now_time));
	} else {
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%s",
			  slurm_ctime(&now_time));
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

extern char *resolve_mp(char *desc, node_info_msg_t *node_info_ptr)
{
	char *ret_str = NULL;
#if defined HAVE_BG_FILES
	ba_mp_t *ba_mp = NULL;
	int i, start_pos;
	char *name;

	if (!desc) {
		ret_str = xstrdup("No Description given.\n");
		goto fini;
	}

	start_pos = strlen(desc) - params.cluster_dims;
	if (start_pos < 0) {
		ret_str = xstrdup_printf("Must enter %d coords to resolve.\n",
					 params.cluster_dims);
		goto fini;
	}

	if (desc[0] != 'R')
		name = desc+start_pos;
	else
		name = desc;

	if (node_info_ptr) {
		for (i=0; i<node_info_ptr->record_count; i++) {
			char *rack_mid, *node_geo;
			node_info_t *node_ptr = &(node_info_ptr->node_array[i]);

			if (!node_ptr->name || (node_ptr->name[0] == '\0'))
				continue;
			start_pos = strlen(node_ptr->name)
				- params.cluster_dims;
			node_geo = node_ptr->name+start_pos;

			slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
						  SELECT_NODEDATA_RACK_MP,
						  0, &rack_mid);
			if (!rack_mid)
				break;
			if (desc[0] != 'R') {
				if (!strcasecmp(name, node_geo))
					ret_str = xstrdup_printf(
						"%s resolves to %s\n",
						node_geo, rack_mid);
			} else if (!strcasecmp(name, rack_mid))
				ret_str = xstrdup_printf(
					"%s resolves to %s\n",
					rack_mid, node_geo);

			xfree(rack_mid);
			if (ret_str)
				return ret_str;
		}
		if (desc[0] != 'R')
			ret_str = xstrdup_printf("%s has no resolve\n", name);
		else
			ret_str = xstrdup_printf("%s has no resolve.\n", desc);
		return ret_str;
	}

	/* Quite any errors that could come our way here. */
	ba_configure_set_ba_debug_flags(0);

	bg_configure_ba_setup_wires();

	if (desc[0] != 'R') {
		ba_mp = bg_configure_str2ba_mp(name);
		if (ba_mp)
			ret_str = xstrdup_printf("%s resolves to %s\n",
						 ba_mp->coord_str, ba_mp->loc);
		else
			ret_str = xstrdup_printf("%s has no resolve\n",
						 name);
	} else {
		ba_mp = bg_configure_loc2ba_mp(desc);
		if (ba_mp)
			ret_str = xstrdup_printf("%s resolves to %s\n",
						 desc, ba_mp->coord_str);
		else
			ret_str = xstrdup_printf("%s has no resolve.\n", desc);
	}
fini:
#else
	ret_str = xstrdup("Must be physically on a BlueGene system for support "
			  "of resolve option.\n");
#endif
	return ret_str;
}

static void _usage(void)
{
#ifdef HAVE_BG
	printf("Usage: smap [-chQV] [-D bcjrs] [-i seconds] "
	       "[-n nodelist] [-i ionodelist] [-M cluster_name]\n");
#else
	printf("Usage: smap [-chQV] [-D jrs] [-i seconds] [-n nodelist] "
	       "[-M cluster_name]\n");
#endif
}

static void _help(void)
{
	printf("\
Usage: smap [OPTIONS]\n\
  -c, --commandline          output written with straight to the\n\
                             commandline.\n\
  -D, --display              set which display mode to use\n\
                             b = bluegene blocks\n\
                             c = set bluegene configuration\n\
                             j = jobs\n\
                             r = reservations\n\
                             s = slurm partitions\n\
  -h, --noheader             no headers on output\n\
  -H, --show_hidden          display hidden partitions and their jobs\n\
  -i, --iterate=seconds      specify an interation period\n\
  -I, --ionodes=[ionodes]    only show objects with these ionodes\n\
                             This should be used inconjuction with the -n\n\
                             option.  Only specify the ionode number range \n\
                             here.  Specify the node name with the -n option.\n\
                             This option is only valid on Bluegene systems,\n\
                             and only valid when querying blocks.\n\
  -M, --cluster=cluster_name cluster to issue commands to.  Default is\n\
                             current cluster.  cluster with no name will\n\
                             reset to default.\n\
  -n, --nodes=[nodes]        only show objects with these nodes.\n\
                             If querying to the ionode level use the -I\n\
                             option in conjunction with this option.\n\
  -R, --resolve              resolve an XYZ coord from a Rack/Midplane id \n\
                             or vice versa.\n\
                             (i.e. -R R101 for R/M input -R 101 for XYZ).\n\
  -V, --version              output version information and exit\n\
\nHelp options:\n\
  --help                     show this help message\n\
  --usage                    display brief usage message\n");
}
