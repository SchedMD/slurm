/****************************************************************************\
 *  smap.h - definitions used for smap data functions
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
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

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pwd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

#ifdef HAVE_BGL_FILES
# include "rm_api.h"
#else
  typedef char *   pm_partition_id_t; 
  typedef int      rm_connection_type_t;
  typedef int      rm_partition_mode_t;
  typedef int      rm_partition_state_t;
  typedef uint16_t rm_partition_t;
  typedef char *   rm_BGL_t;
  typedef char *   rm_component_id_t;
  typedef rm_component_id_t rm_bp_id_t;
  typedef int      rm_BP_state_t;
  typedef int      status_t;
#endif

 
#include "src/common/macros.h"
#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"
#include "src/partition_allocator/partition_allocator.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP  0x100
#define OPT_LONG_USAGE 0x101
#define OPT_LONG_HIDE	0x102

enum { JOBS, SLURMPART, BGLPART, COMMANDS };

typedef void (*sighandler_t) (int);

/* Input parameters */
typedef struct {
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

} smap_parameters_t;

extern smap_parameters_t params;
extern int DIM_SIZE[PA_SYSTEM_DIMENSIONS];

#ifdef HAVE_BGL_FILES
rm_BGL_t *bgl;
#endif		

void parse_command_line(int argc, char *argv[]);

extern pa_system_t *pa_system_ptr;
extern int quiet_flag;


void init_grid(node_info_msg_t * node_info_ptr);
extern int set_grid(int start, int end, int count);
extern int set_grid_bgl(int startx, int starty, int startz, 
		int endx, int endy, int endz, int count, bool lower);
extern void print_grid(void);

void parse_command_line(int argc, char *argv[]);
void snprint_time(char *buf, size_t buf_size, time_t time);
void print_date();

void get_slurm_part();
void get_bgl_part();
void get_job();
void get_command();

#endif
