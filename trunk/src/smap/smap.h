/****************************************************************************\
 *  smap.h - definitions used for smap data functions
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
\****************************************************************************/

#ifndef _SMAP_H
#define _SMAP_H

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else				/* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif				/* HAVE_INTTYPES_H */

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
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

#include <stdlib.h>
#include <pwd.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/plugins/select/bluegene/block_allocator/block_allocator.h"
#include "src/common/slurm_protocol_api.h"

#include "src/plugins/select/bluegene/wrap_rm_api.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP	0x100
#define OPT_LONG_USAGE	0x101
#define OPT_LONG_HIDE	0x102

enum { JOBS, SLURMPART, BGPART, COMMANDS };

//typedef void (*sighandler_t) (int);

/* Input parameters */
typedef struct {
	bool all_flag;
	bool no_header;

	char *format;
	char *sort;
	char *states;

	int iterate;
	int verbose;
	int display;

	bool long_output;
	bool commandline;
	bool parse;

	char *nodes;
	char *partition;
	
	int node_field_size;

} smap_parameters_t;

extern WINDOW *grid_win;
extern WINDOW *text_win;

extern int main_xcord;
extern int main_ycord;

extern smap_parameters_t params;
extern int text_line_cnt;

extern void parse_command_line(int argc, char *argv[]);

extern ba_system_t *ba_system_ptr;
extern int quiet_flag;


extern void init_grid(node_info_msg_t *node_info_ptr);
extern int set_grid(int start, int end, int count);
extern int set_grid_bg(int *start, int *end, int count, int set);
extern void print_grid(int dir);

extern void parse_command_line(int argc, char *argv[]);
extern void print_date();
extern void clear_window(WINDOW *win);

extern void get_slurm_part();
extern void get_bg_part();
extern void get_job();
extern void get_command();

#endif
