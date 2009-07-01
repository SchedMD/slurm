/*****************************************************************************\
 *  licenses.h - Definitions for handling cluster-wide consumable resources
 *****************************************************************************
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
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

#ifndef _LICENSES_H
#define _LICENSES_H

#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"

typedef struct licenses {
	char *		name;		/* name associated with a license */
	uint16_t	total;		/* total license configued */
	uint16_t	used;		/* used licenses */
} licenses_t;

extern List license_list;


/* Initialize licenses on this system based upon slurm.conf */
extern int license_init(char *licenses);

/* Update licenses on this system based upon slurm.conf.
 * Preserve all previously allocated licenses */
extern int license_update(char *licenses);

/* Free memory associated with licenses on this system */
extern void license_free(void);

/* Free a license_t record (for use by list_destroy) */
extern void license_free_rec(void *x);

/*
 * license_job_get - Get the licenses required for a job
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS or failure code
 */ 
extern int license_job_get(struct job_record *job_ptr);

/*
 * license_job_return - Return the licenses allocated to a job
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS or failure code
 */ 
extern int license_job_return(struct job_record *job_ptr);

/*
 * license_job_test - Test if the licenses required for a job are available
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS, EAGAIN (not available now), SLURM_ERROR (never runnable)
 */ 
extern int license_job_test(struct job_record *job_ptr);

/*
 * license_job_validate - Test if the licenses required by a job are valid
 * IN licenses - required licenses
 * OUT valid - true if required licenses are valid and a sufficient number
 *             are configured (though not necessarily available now)
 * RET license_list, must be destroyed by caller
 */ 
extern List license_job_validate(char *licenses, bool *valid);

#endif /* !_LICENSES_H */
