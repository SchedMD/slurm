/*****************************************************************************\
 *  job_submit.h - driver for job_submit plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
\*****************************************************************************/

#ifndef _JOB_SUBMIT_H
#define _JOB_SUBMIT_H

#include "slurm/slurm.h"

/*
 * Initialize the job submit plugin.
 *
 * Returns a Slurm errno.
 */
extern int job_submit_plugin_init(void);

/*
 * Terminate the job submit plugin. Free memory.
 *
 * Returns a Slurm errno.
 */
extern int job_submit_plugin_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Perform reconfig, re-read any configuration files
 */
extern int job_submit_plugin_reconfig(void);

/*
 * Execute the job_submit() function in each job submit plugin.
 * If any plugin function returns anything other than SLURM_SUCCESS
 * then stop and forward it's return value.
 * IN job_desc - Job request specification
 * IN submit_uid - User issuing job submit request
 * OUT err_msg - Custom error message to the user, caller to xfree results
 */
extern int job_submit_plugin_submit(struct job_descriptor *job_desc,
				    uint32_t submit_uid, char **err_msg);

/*
 * Execute the job_modify() function in each job submit plugin.
 * This should be called 
 * If any plugin function returns anything other than SLURM_SUCCESS
 * then stop and forward it's return value.
 */
extern int job_submit_plugin_modify(struct job_descriptor *job_desc,
				    struct job_record *job_ptr,
				    uint32_t submit_uid);

#endif /* !_JOB_SUBMIT_H */
