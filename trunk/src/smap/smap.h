/****************************************************************************\
 *  smap.h - definitions used for smap data functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Moe Jette <jette1@llnl.gov>
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
#include <ncurses.h>

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

enum { JOBS, SLURMPART, BGLPART };

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

#define X 8
#define Y 4
#define Z 4
#define num_of_proc 128
typedef struct {
	char letter;
	int color;
	int indecies;
} axis;

#endif
