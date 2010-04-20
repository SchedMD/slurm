/*****************************************************************************\
 *  gres_gpu.c - Support GPUs as a generic resources.
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

#include <stdio.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/slurm_xlator.h"

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
 * plugin_version   - specifies the version number of the plugin.
 * min_plug_version - specifies the minumum version number of incoming
 *                    messages that this plugin can accept
 */
const char plugin_name[]       	= "Gres GPU plugin";
const char plugin_type[]       	= "gres/gpu";
const uint32_t plugin_version   = 100;
const uint32_t min_plug_version = 100;

/* Currently loaded configuration */
static uint32_t gres_cnt = 0;

/* This will be the output for "srun --gres=help" */
extern int help_msg (char *msg, int msg_size)
{
	char *response = "gpu[:count[*cpu]]";
	int resp_len = strlen(response) + 1;

	if (msg_size < resp_len)
		return SLURM_ERROR;

	memcpy(msg, response, resp_len);
	return SLURM_SUCCESS;
}

/* Get the current configuration of this resource (e.g. how many exist,
 *	their topology and any other required information). */
extern int load_node_config(void)
{
	/* FIXME: Need to flesh this out, probably using 
	 * http://svn.open-mpi.org/svn/hwloc/branches/libpci/
	 * We'll want to capture topology information as well
	 * as count. */
	gres_cnt = 2;
	return SLURM_SUCCESS;
}

/* Pack this node's current configuration.
 * Include the version number so that we can possibly un/pack differnt
 * versions of the data structure. */
extern int pack_node_config(Buf buffer)
{
	pack32(plugin_version, buffer);
	/* FIXME: Pack whatever information is available, including topology */
	pack32(gres_cnt, buffer);
	return SLURM_SUCCESS;
}

/* Unpack this node's current configuration.
 * Include the version number so that we can possibly un/pack differnt
 * versions of the data structure. */
extern int unpack_node_config(Buf buffer)
{
	uint32_t version;

	safe_unpack32(&version, buffer);
	if (version == plugin_version) {
		safe_unpack32(&gres_cnt, buffer);
	} else {
		error("unpack_node_config error for %s, invalid version", 
		      plugin_name);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}
