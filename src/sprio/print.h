/*****************************************************************************\
 *  print.h - sprio print job definitions
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>
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

#ifndef _SPRIO_PRINT_H_
#define _SPRIO_PRINT_H_

#include "slurm/slurm.h"

#include "src/common/list.h"

#define FORMAT_STRING_SIZE 32

/*****************************************************************************
 * Format Structures
 *****************************************************************************/
typedef struct job_format {
	int (*function) (priority_factors_object_t *, int, bool, char*);
	uint32_t width;
	bool right_justify;
	char *suffix;
} job_format_t;

int print_jobs_array(List factors, List format);
int print_job_from_format(priority_factors_object_t * job, List list);

/*****************************************************************************
 * Job Line Format Options
 *****************************************************************************/
int job_format_add_function(List list, int width, bool right_justify,
			    char *suffix,
			    int (*function) (priority_factors_object_t *,
			    int, bool, char*));

#define job_format_add_job_id(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_job_id)
#define job_format_add_prefix(list,wid,right,suffix) \
	job_format_add_function(list,0,0,suffix,_print_job_prefix)
#define job_format_add_age_priority_normalized(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_age_priority_normalized)
#define job_format_add_age_priority_weighted(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_age_priority_weighted)
#define job_format_add_fs_priority_normalized(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_fs_priority_normalized)
#define job_format_add_fs_priority_weighted(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_fs_priority_weighted)
#define job_format_add_job_priority_normalized(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_priority_normalized)
#define job_format_add_job_priority_weighted(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_priority_weighted)
#define job_format_add_js_priority_normalized(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_js_priority_normalized)
#define job_format_add_js_priority_weighted(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_js_priority_weighted)
#define job_format_add_part_priority_normalized(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_part_priority_normalized)
#define job_format_add_part_priority_weighted(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_part_priority_weighted)
#define job_format_add_qos_priority_normalized(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_qos_priority_normalized)
#define job_format_add_qos_priority_weighted(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_qos_priority_weighted)
#define job_format_add_job_nice(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_nice)
#define job_format_add_user_name(list,wid,right,suffix) \
	job_format_add_function(list,wid,right,suffix,_print_job_user_name)

/*****************************************************************************
 * Job Line Print Functions
 *****************************************************************************/
int _print_job_job_id(priority_factors_object_t * job, int width,
		      bool right_justify, char* suffix);
int _print_job_prefix(priority_factors_object_t * job, int width,
		      bool right_justify, char* suffix);
int _print_age_priority_normalized(priority_factors_object_t * job, int width,
				   bool right_justify, char* suffix);
int _print_age_priority_weighted(priority_factors_object_t * job, int width,
				 bool right_justify, char* suffix);
int _print_fs_priority_normalized(priority_factors_object_t * job, int width,
				  bool right_justify, char* suffix);
int _print_fs_priority_weighted(priority_factors_object_t * job, int width,
				bool right_justify, char* suffix);
int _print_job_priority_normalized(priority_factors_object_t * job, int width,
				   bool right_justify, char* suffix);
int _print_job_priority_weighted(priority_factors_object_t * job, int width,
				 bool right_justify, char* suffix);
int _print_js_priority_normalized(priority_factors_object_t * job, int width,
				  bool right_justify, char* suffix);
int _print_js_priority_weighted(priority_factors_object_t * job, int width,
				bool right_justify, char* suffix);
int _print_part_priority_normalized(priority_factors_object_t * job, int width,
				    bool right_justify,	char* suffix);
int _print_part_priority_weighted(priority_factors_object_t * job, int width,
				  bool right_justify, char* suffix);
int _print_qos_priority_normalized(priority_factors_object_t * job, int width,
				   bool right_justify, char* suffix);
int _print_qos_priority_weighted(priority_factors_object_t * job, int width,
				 bool right_justify, char* suffix);
int _print_job_nice(priority_factors_object_t * job, int width,
		    bool right_justify, char* suffix);
int _print_job_user_name(priority_factors_object_t * job, int width,
			 bool right_justify, char* suffix);

#endif
