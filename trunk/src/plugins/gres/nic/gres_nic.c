/*****************************************************************************\
 *  gres_nic.c - Support NICs as a generic resources.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#  if STDC_HEADERS
#    include <string.h>
#  endif
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif /* HAVE_SYS_TYPES_H */
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else /* ! HAVE_INTTYPES_H */
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#else /* ! HAVE_CONFIG_H */
#  include <sys/types.h>
#  include <unistd.h>
#  include <stdint.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_HWLOC
#  include <hwloc.h>
#endif /* HAVE_HWLOC */

#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/slurm_xlator.h"
#include "src/common/bitstring.h"
#include "src/common/gres.h"
#include "src/common/list.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for SLURM authentication) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_id        - unique id for this plugin, value of 100+
 * help_msg         - response for srun --gres=help
 * plugin_version   - specifies the version number of the plugin.
 */
const char	plugin_name[]		= "Gres NIC plugin";
const char	plugin_type[]		= "gres/nic";
const uint32_t	plugin_id		= 102;
const char	gres_name[]		= "nic";
const char	help_msg[]		= "nic[:count]";
const uint32_t	plugin_version		= 100;

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 */
extern int node_config_load(List gres_conf_list)
{
	int rc = SLURM_ERROR;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;

	xassert(gres_conf_list);
	iter = list_iterator_create(gres_conf_list);
	if (iter == NULL)
		fatal("list_iterator_create: malloc failure");
	while ((gres_slurmd_conf = list_next(iter))) {
		if (strcmp(gres_slurmd_conf->name, "nic") == 0) {
			gres_slurmd_conf->plugin_id = plugin_id;
			rc = SLURM_SUCCESS;
		}
	}
	list_iterator_destroy(iter);

	if (rc != SLURM_SUCCESS)
		fatal("%s failed to load configuration", plugin_name);
	return rc;
}
