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
//#include "src/slurmctld/slurmctld.h"
#include "src/partition_allocator/partition_allocator.h"
#include "src/common/slurm_protocol_api.h"

#ifdef HAVE_BGL_FILES
# include "src/plugins/select/bluegene/wrap_rm_api.h"
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

/* these are the typedefs that we will need to have  */
/* if we want the states on the fen in a bgl system */
  enum rm_partition_state {RM_PARTITION_FREE, 
			   RM_PARTITION_CONFIGURING,
			   RM_PARTITION_READY,
			   RM_PARTITION_BUSY,
			   RM_PARTITION_DEALLOCATING,
			   RM_PARTITION_ERROR,
			   RM_PARTITION_NAV};
  typedef enum status {STATUS_OK  = 0,
		       PARTITION_NOT_FOUND = -1,
		       JOB_NOT_FOUND = -2,
		       BP_NOT_FOUND = -3,
		       SWITCH_NOT_FOUND = -4,
		       JOB_ALREADY_DEFINED=-5,
		       CONNECTION_ERROR=-10,
		       INTERNAL_ERROR = -11,
		       INVALID_INPUT=-12,
		       INCOMPATIBLE_STATE=-13,
		       INCONSISTENT_DATA=-14,
  }status_t;
#endif

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
	bool commandline;
	bool parse;

	char *nodes;
	char *partition;
	
	int node_field_size;

} smap_parameters_t;

extern smap_parameters_t params;
extern int DIM_SIZE[PA_SYSTEM_DIMENSIONS];

void parse_command_line(int argc, char *argv[]);

extern pa_system_t *pa_system_ptr;
extern int quiet_flag;


void init_grid(node_info_msg_t *node_info_ptr);
extern int set_grid(int start, int end, int count);
extern int set_grid_bgl(int *start, int *end, int count, int set);
extern void print_grid(int dir);

void parse_command_line(int argc, char *argv[]);
void snprint_time(char *buf, size_t buf_size, time_t time);
void print_date();
void clear_window(WINDOW *win);

void get_slurm_part();
void get_bgl_part();
void get_job();
void get_command();

#endif
