/*****************************************************************************\
 *  src/common/job_options.h  - Extra job options 
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifndef _JOB_OPTIONS_H
#define _JOB_OPTIONS_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/pack.h"

typedef struct job_options * job_options_t;	/* opaque data type */

struct job_option_info {
	int type;
	char *option;
	char *optarg;
};

/*
 *  Create generic job options container.
 */
job_options_t job_options_create (void);

/*
 *  Destroy container, freeing all data associated with options.
 */
void job_options_destroy (job_options_t opts);

/*
 *  Append option of type `type' and its argument to job options
 */
int job_options_append (job_options_t opts, int type, const char *opt, 
		        const char *optarg);

/*
 *  Pack all accumulated options into Buffer "buf"
 */
int job_options_pack (job_options_t opts, Buf buf);

/*
 *  Unpack options from buffer "buf" into options container opts.
 */
int job_options_unpack (job_options_t opts, Buf buf);

/*
 *  Reset internal options list iterator
 */
void job_options_iterator_reset (job_options_t opts);

/*
 *  Iterate over all job options
 */
const struct job_option_info * job_options_next (job_options_t opts);

#endif /* !_JOB_OPTIONS_H */
