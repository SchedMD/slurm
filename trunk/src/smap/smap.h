/****************************************************************************\
 *  smap.h - definitions used for smap data functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-2002-040.
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
\****************************************************************************/

#ifndef _SMAP_H
#define _SMAP_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pwd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curses.h>

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else				/* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif				/* HAVE_INTTYPES_H */

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

enum { JOBS, SLURMPART, BGLPART, COMMANDS };

/* Input parameters */
struct smap_parameters {
	//Both
	bool all_flag;
	bool no_header;

	char *format;
	char *sort;
	char *states;

	int iterate;
	int verbose;
	int display;

	bool long_output;

	char *nodes;
	char *partition;

	int node_field_size;

};

extern struct smap_parameters params;

extern void parse_command_line(int argc, char *argv[]);

typedef struct {
	char letter;
	int color;
	int indecies;
	int state;
} axis;

extern int quiet_flag;
extern int xcord;
extern int ycord;
extern int X;
extern int Y;
extern int Z;
extern int num_of_proc;

WINDOW *grid_win;
WINDOW *text_win;

time_t now;


axis ***grid;
axis *fill_in_value;

void clear_window(WINDOW * win);

void init_grid(node_info_msg_t * node_info_ptr);
int set_grid(int start, int end, int count);
int set_grid_bgl(int startx, int starty, int startz, int endx, int endy,
		 int endz, int count);
void print_grid(void);

void print_date(void);

void get_part(void);
void get_job(void);
void get_command(void);

#endif
