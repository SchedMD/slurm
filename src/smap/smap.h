/****************************************************************************\
 *  smap.h - definitions used for smap data functions
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
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
\****************************************************************************/

#ifndef _SMAP_H
#define _SMAP_H

#include "config.h"

#define _GNU_SOURCE

/*
 * The following define is necessary for OS X 10.6. The apple supplied
 * ncurses.h sets NCURSES_OPAQUE to 1 and then we can't get to the WINDOW
 * flags.
 */
#if defined(__APPLE__)
#  define NCURSES_OPAQUE 0
#endif

#if HAVE_CURSES_H
#  include <curses.h>
#endif
#if HAVE_NCURSES_H
#  include <ncurses.h>
#  ifndef HAVE_CURSES_H
#     define HAVE_CURSES_H
#  endif
#endif

/*
 * On some systems (read AIX), curses.h includes term.h which does this
 *    #define lines cur_term-> _c3
 * This makes the symbol "lines" unusable. There is a similar #define
 * "columns", "bell", "tone", "pulse", "hangup" and many, many more!!
 */
#ifdef lines
#  undef lines
#endif

#ifndef SYSTEM_DIMENSIONS
#  define SYSTEM_DIMENSIONS 1
#endif

#include <ctype.h>
#include <getopt.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP	0x100
#define OPT_LONG_USAGE	0x101

enum { JOBS, RESERVATIONS, PARTITION };

/* Input parameters */
typedef struct {
	bool all_flag;
	List clusters;
	int cluster_base;
	uint16_t cluster_dims;
	uint32_t cluster_flags;
	bool commandline;
	int display;
	int iterate;
	bool no_header;
	hostlist_t hl;
	int verbose;
} smap_parameters_t;

/*
 * smap_node_t: node within the allocation system.
 */
typedef struct {
	/* coordinates of midplane */
	uint16_t *coord;
	/* coordinates on display screen */
	int grid_xcord, grid_ycord;
	/* color of letter used in smap */
	int color;
	/* midplane index used for easy look up of the miplane */
	int index;
	/* letter used in smap */
	char letter;
	uint32_t state;
} smap_node_t;

typedef struct {
	int node_cnt;
	smap_node_t **grid;
} smap_system_t;

extern WINDOW *grid_win;
extern WINDOW *text_win;

extern int *dim_size;
extern char letters[62]; /* complete list of letters used in smap */
extern char colors[6]; /* index into colors used for smap */

extern int main_xcord;
extern int main_ycord;

extern smap_parameters_t params;
extern int text_line_cnt;

extern smap_system_t *smap_system_ptr;
extern int quiet_flag;

extern void init_grid(node_info_msg_t *node_info_ptr, int cols);
extern void update_grid(node_info_msg_t *node_info_ptr);
extern void clear_grid(void);
extern void free_grid(void);
extern int *get_cluster_dims(node_info_msg_t *node_info_ptr);
extern void set_grid_inx(int start, int end, int count);
extern void print_grid(void);
bitstr_t *get_requested_node_bitmap(void);

extern void parse_command_line(int argc, char **argv);
extern void print_date(void);
extern void clear_window(WINDOW *win);

extern void get_slurm_part(void);
extern void get_job(void);
extern void get_reservation(void);

#endif
