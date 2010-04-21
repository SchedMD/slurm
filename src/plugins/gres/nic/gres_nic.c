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

#include <stdio.h>
#include <stdlib.h>

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
 * plugin_id        - unique id for this plugin, value of 100+
 * plugin_version   - specifies the version number of the plugin.
 * min_plug_version - specifies the minumum version number of incoming
 *                    messages that this plugin can accept
 */
const char	plugin_name[]		= "Gres NIC plugin";
const char	plugin_type[]		= "gres/nic";
const uint32_t	plugin_id		= 102;
const uint32_t	plugin_version		= 100;
const uint32_t	min_plug_version	= 100;

/* Gres configuration loaded/used by slurmd. Modify or expand as
 * additional information becomes available (e.g. topology). */
typedef struct nic_config {
	bool     loaded;
	uint32_t nic_cnt;
} nic_config_t;
static nic_config_t gres_config;

/* Gres state as used by slurmctld. Includes data from gres_config loaded
 * from slurmd, resources configured (may be more or less than actually found)
 * plus resource allocation information. */
typedef struct nic_status {
	uint32_t plugin_id;	/* Required for all gres plugin types */

	/* Actual hardware found */
	uint32_t nic_cnt_found;

	/* Configured resources via Gres parameter */
	uint32_t nic_cnt_config;

	/* Total resources available for allocation to jobs */
	uint32_t nic_cnt_avail;

	/* Resources currently allocated to jobs */
	uint32_t  nic_cnt_alloc;
	bitstr_t *nic_bit_alloc;
} nic_status_t;

/*
 * This will be the output for "--gres=help" option.
 * Called only by salloc, sbatch and srun.
 */
extern int help_msg(char *msg, int msg_size)
{
	char *response = "nic[:count[*cpu]]";
	int resp_len = strlen(response) + 1;

	if (msg_size < resp_len)
		return SLURM_ERROR;

	memcpy(msg, response, resp_len);
	return SLURM_SUCCESS;
}

/*
 * Get the current configuration of this resource (e.g. how many exist,
 *	their topology and any other required information).
 * Called only by slurmd.
 */
extern int load_node_config(void)
{
	/* FIXME: Need to flesh this out, probably using 
	 * http://svn.open-mpi.org/svn/hwloc/branches/libpci/
	 * We'll want to capture topology information as well
	 * as count. */
	gres_config.loaded  = true;
	gres_config.nic_cnt = 4;
	return SLURM_SUCCESS;
}

/*
 * Pack this node's current configuration.
 * Include the version number so that we can possibly un/pack differnt
 *	versions of the data structure.
 * Keep the pack and unpack functions in sync.
 * Called only by slurmd.
 */
extern int pack_node_config(Buf buffer)
{
	int rc = SLURM_SUCCESS;

	pack32(plugin_version, buffer);

	if (!gres_config.loaded)
		rc = load_node_config();

	/* Pack whatever node information is relevant to the slurmctld,
	 * including topology. */
	pack32(gres_config.nic_cnt, buffer);

	return rc;
}

/*
 * Unpack this node's current configuration.
 * Include the version number so that we can possibly un/pack differnt
 *	versions of the data structure.
 * Keep the pack and unpack functions in sync.
 * Called only by slurmctld.
 */
extern int unpack_node_config(Buf buffer)
{
	uint32_t version;

	if (!buffer) {
		/* The node failed to pack this gres info, likely due to
		 * inconsistent GresPlugins configuration. Set a reasonable
		 * default configuration. */
		gres_config.nic_cnt = NO_VAL;
		return SLURM_SUCCESS;
	}

	safe_unpack32(&version, buffer);

	if (version == plugin_version) {
		safe_unpack32(&gres_config.nic_cnt, buffer);
		info("nic_cnt=%u", gres_config.nic_cnt);
	} else {
		error("unpack_node_config error for %s, invalid version", 
		      plugin_name);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}

/*
 * Delete an element placed on gres_list by node_config_validate()
 * free associated memory
 */
extern int node_config_delete(nic_status_t *list_element)
{
	xassert(list_element);
	if (list_element->plugin_id != plugin_id)
		return SLURM_ERROR;	/* Record from other plugin */

	if (list_element->nic_bit_alloc)
		bit_free(list_element->nic_bit_alloc);
	xfree(list_element);
	return SLURM_SUCCESS;
}

/*
 * Validate a node's configuration and put a gres record onto a list
 * Called immediately after unpack_node_config().
 * IN node_name - name of the node for which the gres information applies
 * IN/OUT configured_res - Gres information suppled from slurm.conf,
 *		may be updated with actual configuration if FastSchedule=0
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int node_config_validate(char *node_name, char **configured_gres,
				List *gres_list, uint16_t fast_schedule,
				char **reason_down)
{
	ListIterator gres_iter;
	nic_status_t *gres_ptr;
	char *node_gres_config, *tok, *last = NULL;
	int32_t gres_config_cnt = -1;
	int rc = SLURM_SUCCESS;
	bool updated_config = false;

	xassert(*gres_list);
	gres_iter = list_iterator_create(*gres_list);
	if (gres_iter == NULL)
		fatal("list_iterator_create malloc failure");
	while ((gres_ptr = list_next(gres_iter))) {
		if (gres_ptr->plugin_id == plugin_id)
			break;
	}
	list_iterator_destroy(gres_iter);
	if (gres_ptr == NULL) {
		gres_ptr = xmalloc(sizeof(nic_status_t));
		list_append(*gres_list, gres_ptr);
		gres_ptr->plugin_id = plugin_id;
		gres_ptr->nic_cnt_found = gres_config.nic_cnt;
		updated_config = true;
	} else if (gres_ptr->nic_cnt_found != gres_config.nic_cnt) {
		info("%s count changed for node %s from %u to %u",
		     plugin_name, node_name, gres_config.nic_cnt,
		     gres_ptr->nic_cnt_found);
		gres_ptr->nic_cnt_found = gres_config.nic_cnt;
		updated_config = true;
	}

	/* If the resource isn't configured for use with this node,
	 * just return leaving nic_cnt_config=0, nic_cnt_avail=0, etc. */
	if ((*configured_gres == NULL) || (updated_config == false))
		return SLURM_SUCCESS;

	node_gres_config = xstrdup(*configured_gres);
	tok = strtok_r(node_gres_config, ",", &last);
	while (tok) {
		if (!strcmp(tok, "nic")) {
			gres_config_cnt = 1;
			break;
		}
		if (!strncmp(tok, "nic:", 4)) {
			gres_config_cnt = strtol(tok+4, &last, 10);
			if (last[0] == '\0')
				;
			else if ((last[0] == 'k') || (last[0] == 'K'))
				gres_config_cnt *= 1024;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	gres_ptr->nic_cnt_config = gres_config_cnt;
	xfree(node_gres_config);

	if (gres_config_cnt < 0)
		;	/* not configured for use */
	else if (fast_schedule == 0)
		gres_ptr->nic_cnt_avail = gres_ptr->nic_cnt_found;
	else
		gres_ptr->nic_cnt_avail = gres_ptr->nic_cnt_config;

	if (gres_ptr->nic_bit_alloc == NULL) {
		gres_ptr->nic_bit_alloc = bit_alloc(gres_ptr->nic_cnt_avail);
	} else if (gres_ptr->nic_cnt_avail > 
		   bit_size(gres_ptr->nic_bit_alloc)) {
		gres_ptr->nic_bit_alloc = bit_realloc(gres_ptr->nic_bit_alloc,
						      gres_ptr->nic_cnt_avail);
	}
	if (gres_ptr->nic_bit_alloc == NULL)
		fatal("bit_alloc: malloc failure");


	if ((fast_schedule < 2) && 
	    (gres_ptr->nic_cnt_found < gres_ptr->nic_cnt_config)) {
		*reason_down = "gres/nic count too small";
		rc = EINVAL;
	} else if ((fast_schedule == 0) && 
		   (gres_ptr->nic_cnt_found > gres_ptr->nic_cnt_config)) {
		/* need to rebuild configured_gres */
		char *new_configured_res = NULL;
		node_gres_config = xstrdup(*configured_gres);
		tok = strtok_r(node_gres_config, ",", &last);
		while (tok) {
			if (new_configured_res)
				xstrcat(new_configured_res, ",");
			if (strcmp(tok, "nic") && strncmp(tok, "nic:", 4)) {
				xstrcat(new_configured_res, tok);
			} else {
				xstrfmtcat(new_configured_res, "nic:%u",
					   gres_ptr->nic_cnt_found);
			}
			tok = strtok_r(NULL, ",", &last);
		}
		xfree(node_gres_config);
		xfree(*configured_gres);
		*configured_gres = new_configured_res;
	}

	return rc;
}
