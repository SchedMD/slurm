/*****************************************************************************\
 *  cli_filter.h - driver for the cli_filter plugin
 *****************************************************************************
 *  Copyright (C) 2017-2019 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory (cf, DISCLAIMER).
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#ifndef _CLI_FILTER_H
#define _CLI_FILTER_H

#include "slurm/slurm.h"
#include "src/common/slurm_opt.h"

/*
 * Initialize the cli filter plugin.
 *
 * Returns a SLURM errno.
 */
extern int cli_filter_plugin_init(void);

/*
 * Terminate the cli filter plugin. Free memory.
 *
 * Returns a SLURM errno.
 */
extern int cli_filter_plugin_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */


/*
 * Execute the setup_defaults() function in each cli filter plugin.
 * setup_defaults() is executed before the CLI performs option processing
 * and thus can be used to change default values in the CLI.
 * IN/OUT opt	- pointer to slurm_opt_t data structure. Value of pointer
 *		  cannot change, but OK to mutate some of the values within
 *		  the dereferenced memory using the appropriate argset
 *		  functions.
 * IN early	- is the setup_defaults() running in the early pass or not
 * RETURNs	- SLURM_SUCCESS if cli_filter processing should allow the
 *				CLI to continue execution
 *		- SLURM_ERROR	if any condition is determined that should
 *				cease processing of the CLI
 */
extern int cli_filter_plugin_setup_defaults(slurm_opt_t *opt, bool early);

/*
 * Execute the pre_submit() function in each cli filter plugin.
 * pre_submit() is executed after the CLI performs option processing
 * but before final option validation is performed.
 * IN/OUT opt	- pointer to slurm_opt_t data structure. Value of pointer
 *		  cannot change, but OK to mutate some of the values within
 *		  the dereferenced memory using the appropriate argset
 *		  functions.
 * IN offset	- hetjob offset (0 for first pack, 1 for second, and so on)
 * RETURNs	- SLURM_SUCCESS if cli_filter processing should allow the
 *				CLI to continue execution
 *		- SLURM_ERROR   if any condition is determined that should
 *				cease processing of the CLI
 */
extern int cli_filter_plugin_pre_submit(slurm_opt_t *opt, int offset);

/*
 * Execute the post_submit() function in each cli filter plugin.
 * post_submit() is executed after the CLI receives a response from the
 * controller with the jobid. This is primarily for logging. The plugin
 * should not attempt to read or modify any arguments at this stage as the
 * strings interface may not be stable. Instead this is meant to be used
 * primarily for logging purposes using data collected from the pre_submit()
 * hook.
 * IN offset	- hetjob offset (0 for first pack, 1 for second, and so on)
 * IN jobid	- job identifier back from the controller.
 * IN stepid	- step identifier (NOVAL if not set)
 */
extern void cli_filter_plugin_post_submit(int offset, uint32_t jobid,
					  uint32_t stepid);

#endif /* !_CLI_FILTER_H */
