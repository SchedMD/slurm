/*****************************************************************************\
 *  slurm_xlator.h - Definitions required to translate SLURM function names
 *  to aliases containing  a prefix of "slurm_".
 *
 *  This is required because some SLURM functions have common names 
 *  (e.g. "debug" and "info"). If a user application defines these functions 
 *  and uses SLURM APIs, they could link to the user function rather than 
 *  the SLURM function. By renaming the functions, inappropriate linking 
 *  should be avoided.
 *
 *  All SLURM functions referenced from the switch and auth plugins should
 *  be aliased here.
 *
 *  To use this header file:
 *  1. Add the strong_alias functions for the functions being exported
 *  2. Include the header file defining the functions (needed before
 *     strong_alias is executed)
 *  3. In the header file defining the functions, include
 *     "src/common/slurm_xlator.h" _before_ any "#ifndef" test. This insures
 *     that strong_alias() is always executed after the functions are defined.
 *  4. If the module containing the relevant function does not include its
 *     matching header (e.g. "log.c" does not include "log.h") then explicitly
 *     include "src/common/slurm_xlator.h"
 *  Note: Items 3 and 4 result in minimal changes to existing modules
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, Moe Jette <jette1@llnl.gov>
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
\*****************************************************************************/

#ifndef __SLURM_XLATOR_H__
#define __SLURM_XLATOR_H__

#include "src/common/log.h"

#define strong_alias(name, aliasname) \
extern __typeof (name) aliasname __attribute ((alias (#name)))

/* 
 * rename all functions via gcc alias
 */

/* log.[ch] functions */
strong_alias(fatal,	slurm_fatal);
strong_alias(error,	slurm_error);
strong_alias(info,	slurm_info);
strong_alias(verbose,	slurm_verbose);
strong_alias(debug,	slurm_debug);
strong_alias(debug2,	slurm_debug2);
strong_alias(debug3,	slurm_debug3);

#endif /*__SLURM_XLATOR_H__*/
