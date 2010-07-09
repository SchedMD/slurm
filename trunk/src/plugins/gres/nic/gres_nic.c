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
 * min_plug_version - specifies the minumum version number of incoming
 *                    messages that this plugin can accept
 */
const char	plugin_name[]		= "Gres NIC plugin";
const char	plugin_type[]		= "gres/nic";
const uint32_t	plugin_id		= 102;
const char	gres_name[]		= "nic";
const char	help_msg[]		= "nic[:count[*cpu]]";

const uint32_t	plugin_version		= 100;
const uint32_t	min_plug_version	= 100;

/* Gres configuration loaded/used by slurmd. Modify or expand as
 * additional information becomes available (e.g. topology). */
typedef struct nic_config {
	bool       loaded;		/* flag if structure is loaded */
	uint32_t   nic_cnt;		/* total count of NICs on system */
} nic_config_t;
static nic_config_t gres_config;

/* Gres node state as used by slurmctld. Includes data from gres_config loaded
 * from slurmd, resources configured (may be more or less than actually found)
 * plus resource allocation information. */
typedef struct nic_node_state {
	/* Actual hardware found */
	uint32_t nic_cnt_found;

	/* Configured resources via Gres parameter */
	uint32_t nic_cnt_config;

	/* Total resources available for allocation to jobs */
	uint32_t nic_cnt_avail;

	/* Resources currently allocated to jobs */
	uint32_t  nic_cnt_alloc;
	bitstr_t *nic_bit_alloc;
} nic_node_state_t;

/* Gres job state as used by slurmctld. */
typedef struct nic_job_state {
	/* Count of resources needed */
	uint32_t nic_cnt_alloc;

	/* If 0 then nic_cnt_alloc is per node,
	 * if 1 then nic_cnt_alloc is per CPU */
	uint8_t  nic_cnt_mult;

	/* Resources currently allocated to job on each node */
	uint32_t node_cnt;
	bitstr_t **nic_bit_alloc;

	/* Resources currently allocated to job steps on each node.
	 * This will be a subset of resources allocated to the job.
	 * nic_bit_step_alloc is a subset of nic_bit_alloc */
	bitstr_t **nic_bit_step_alloc;
} nic_job_state_t;

/* Gres job step state as used by slurmctld. */
typedef struct nic_step_state {
	/* Count of resources needed */
	uint32_t nic_cnt_alloc;

	/* If 0 then nic_cnt_alloc is per node,
	 * if 1 then nic_cnt_alloc is per CPU */
	uint8_t  nic_cnt_mult;

	/* Resources currently allocated to the job step on each node */
	uint32_t node_cnt;
	bitstr_t **nic_bit_alloc;
} nic_step_state_t;

/* Purge node configuration state removed */
static void _purge_old_node_config(void)
{
	gres_config.loaded 	= false;
	gres_config.nic_cnt	= 0;
}

/* Purge all allocated memory when the plugin is removed */
extern int fini(void)
{
	_purge_old_node_config();
	return SLURM_SUCCESS;
}

/* We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf. */
extern int node_config_load(List gres_conf_list)
{
	int rc = SLURM_ERROR;
	ListIterator iter;
	gres_conf_t *gres_conf;

	xassert(gres_conf_list);
	gres_config.loaded  = false;
	gres_config.nic_cnt = 0;

	iter = list_iterator_create(gres_conf_list);
	if (iter == NULL)
		fatal("list_iterator_create: malloc failure");
	while ((gres_conf = list_next(iter))) {
		if (strcmp(gres_conf->name, "nic"))
			continue;
		gres_config.loaded   = true;
		gres_config.nic_cnt += gres_conf->count;
		rc = SLURM_SUCCESS;
	}
	list_iterator_destroy(iter);

	if (rc != SLURM_SUCCESS)
		fatal("%s failed to load configuration", plugin_name);
	return rc;
}

/*
 * Pack this node's current configuration.
 * Include the version number so that we can possibly un/pack differnt
 *	versions of the data structure.
 * Keep the pack and unpack functions in sync.
 * Called only by slurmd.
 */
extern int node_config_pack(Buf buffer)
{
	int rc = SLURM_SUCCESS;

	pack32(plugin_version, buffer);

	if (!gres_config.loaded)
		fatal("%s failed to load configuration", plugin_name);

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
extern int node_config_unpack(Buf buffer)
{
	uint32_t version;

	_purge_old_node_config();
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
		/* info("nic_cnt=%u", gres_config.nic_cnt); */
	} else {
		error("node_config_unpack error for %s, invalid version", 
		      plugin_name);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;

unpack_error:
	_purge_old_node_config();
	return SLURM_ERROR;
}

/*
 * Delete an element placed on gres_list by node_config_validate()
 * free associated memory
 */
extern void node_config_delete(void *gres_data)
{
	nic_node_state_t *gres_ptr;

	xassert(gres_data);
	gres_ptr = (nic_node_state_t *) gres_data;
	FREE_NULL_BITMAP(gres_ptr->nic_bit_alloc);
	xfree(gres_ptr);
}

extern int node_config_init(char *node_name, char *orig_config,
			    void **gres_data)
{
	int rc = SLURM_SUCCESS;
	nic_node_state_t *gres_ptr;
	char *node_gres_config, *tok, *last = NULL;
	int32_t gres_config_cnt = 0;
	bool updated_config = false;

	xassert(gres_data);
	gres_ptr = (nic_node_state_t *) *gres_data;
	if (gres_ptr == NULL) {
		gres_ptr = xmalloc(sizeof(nic_node_state_t));
		*gres_data = gres_ptr;
		gres_ptr->nic_cnt_config = NO_VAL;
		gres_ptr->nic_cnt_found  = NO_VAL;
		updated_config = true;
	}

	/* If the resource isn't configured for use with this node,
	 * just return leaving nic_cnt_config=0, nic_cnt_avail=0, etc. */
	if ((orig_config == NULL) || (orig_config[0] == '\0') ||
	    (updated_config == false))
		return rc;

	node_gres_config = xstrdup(orig_config);
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
	xfree(node_gres_config);

	gres_ptr->nic_cnt_config = gres_config_cnt;
	gres_ptr->nic_cnt_avail  = gres_config_cnt;
	if (gres_ptr->nic_bit_alloc == NULL) {
		gres_ptr->nic_bit_alloc = bit_alloc(gres_ptr->nic_cnt_avail);
	} else if (gres_ptr->nic_cnt_avail > 
		   bit_size(gres_ptr->nic_bit_alloc)) {
		gres_ptr->nic_bit_alloc = bit_realloc(gres_ptr->nic_bit_alloc,
						      gres_ptr->nic_cnt_avail);
	}
	if (gres_ptr->nic_bit_alloc == NULL)
		fatal("bit_alloc: malloc failure");

	return rc;
}

/*
 * Validate a node's configuration and put a gres record onto a list
 * Called immediately after node_config_unpack().
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT new_config - Updated gres info from slurm.conf if FastSchedule=0
 * IN/OUT gres_data - Gres record for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int node_config_validate(char *node_name, 
				char *orig_config, char **new_config,
				void **gres_data, uint16_t fast_schedule,
				char **reason_down)
{
	int rc = SLURM_SUCCESS;
	nic_node_state_t *gres_ptr;
	char *node_gres_config, *tok, *last = NULL;
	uint32_t gres_config_cnt = 0;
	bool updated_config = false;

	xassert(gres_data);
	gres_ptr = (nic_node_state_t *) *gres_data;
	if (gres_ptr == NULL) {
		gres_ptr = xmalloc(sizeof(nic_node_state_t));
		*gres_data = gres_ptr;
		gres_ptr->nic_cnt_config = NO_VAL;
		gres_ptr->nic_cnt_found  = gres_config.nic_cnt;
		updated_config = true;
	} else if (gres_ptr->nic_cnt_found != gres_config.nic_cnt) {
		if (gres_ptr->nic_cnt_found != NO_VAL) {
			info("%s count changed for node %s from %u to %u",
			     plugin_name, node_name, gres_config.nic_cnt,
			     gres_ptr->nic_cnt_found);
		}
		gres_ptr->nic_cnt_found = gres_config.nic_cnt;
		updated_config = true;
	}
	if (updated_config == false)
		return SLURM_SUCCESS;

	if ((orig_config == NULL) || (orig_config[0] == '\0'))
		gres_ptr->nic_cnt_config = 0;
	else if (gres_ptr->nic_cnt_config == NO_VAL) {
		node_gres_config = xstrdup(orig_config);
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
		xfree(node_gres_config);
		gres_ptr->nic_cnt_config = gres_config_cnt;
	}

	if ((gres_ptr->nic_cnt_config == 0) || (fast_schedule > 0))
		gres_ptr->nic_cnt_avail = gres_ptr->nic_cnt_config;
	else
		gres_ptr->nic_cnt_avail = gres_ptr->nic_cnt_found;

	if (gres_ptr->nic_bit_alloc == NULL) {
		gres_ptr->nic_bit_alloc = bit_alloc(gres_ptr->nic_cnt_avail);
	} else if (gres_ptr->nic_cnt_avail > 
		   bit_size(gres_ptr->nic_bit_alloc)) {
		gres_ptr->nic_bit_alloc = bit_realloc(gres_ptr->nic_bit_alloc,
						      gres_ptr->nic_cnt_avail);
		if (gres_ptr->nic_bit_alloc == NULL)
			fatal("bit_alloc: malloc failure");
	}

	if ((fast_schedule < 2) && 
	    (gres_ptr->nic_cnt_found < gres_ptr->nic_cnt_config)) {
		if (reason_down && (*reason_down == NULL))
			*reason_down = xstrdup("gres/nic count too low");
		rc = EINVAL;
	} else if ((fast_schedule == 0) && 
		   (gres_ptr->nic_cnt_found > gres_ptr->nic_cnt_config)) {
		/* need to rebuild new_config */
		char *new_configured_res = NULL;
		if (*new_config)
			node_gres_config = xstrdup(*new_config);
		else
			node_gres_config = xstrdup(orig_config);
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
		xfree(*new_config);
		*new_config = new_configured_res;
	}

	return rc;
}

/*
 * Note that a node's configuration has been modified.
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT new_config - Updated gres info from slurm.conf if FastSchedule=0
 * IN/OUT gres_data - Gres record for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 */
extern int node_reconfig(char *node_name, char *orig_config, char **new_config,
			 void **gres_data, uint16_t fast_schedule)
{
	int rc = SLURM_SUCCESS;
	nic_node_state_t *gres_ptr;
	char *node_gres_config = NULL, *tok = NULL, *last = NULL;
	int32_t gres_config_cnt = 0;

	xassert(gres_data);
	gres_ptr = (nic_node_state_t *) *gres_data;
	if (gres_ptr == NULL) {
		gres_ptr = xmalloc(sizeof(nic_node_state_t));
		*gres_data = gres_ptr;
		gres_ptr->nic_cnt_config = NO_VAL;
		gres_ptr->nic_cnt_found  = NO_VAL;
	}

	if (orig_config) {
		node_gres_config = xstrdup(orig_config);
		tok = strtok_r(node_gres_config, ",", &last);
	}
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

	if ((gres_ptr->nic_cnt_config == 0) || (fast_schedule > 0) ||
	    (gres_ptr->nic_cnt_found == NO_VAL))
		gres_ptr->nic_cnt_avail = gres_ptr->nic_cnt_config;
	else
		gres_ptr->nic_cnt_avail = gres_ptr->nic_cnt_found;

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
	    (gres_ptr->nic_cnt_found != NO_VAL) &&
	    (gres_ptr->nic_cnt_found <  gres_ptr->nic_cnt_config)) {
		/* Do not set node DOWN, but give the node 
		 * a chance to register with more resources */
		gres_ptr->nic_cnt_found = NO_VAL;
	} else if ((fast_schedule == 0) &&
		   (gres_ptr->nic_cnt_found != NO_VAL) &&
		   (gres_ptr->nic_cnt_found >  gres_ptr->nic_cnt_config)) {
		/* need to rebuild new_config */
		char *new_configured_res = NULL;
		if (*new_config)
			node_gres_config = xstrdup(*new_config);
		else
			node_gres_config = xstrdup(orig_config);
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
		xfree(*new_config);
		*new_config = new_configured_res;
	}

	return rc;
}

extern int node_state_pack(void *gres_data, Buf buffer)
{
	nic_node_state_t *gres_ptr = (nic_node_state_t *) gres_data;

	pack32(gres_ptr->nic_cnt_avail,  buffer);
	pack32(gres_ptr->nic_cnt_alloc,  buffer);
	pack_bit_str(gres_ptr->nic_bit_alloc, buffer);

	return SLURM_SUCCESS;
}

extern int node_state_unpack(void **gres_data, Buf buffer)
{
	nic_node_state_t *gres_ptr;

	gres_ptr = xmalloc(sizeof(nic_node_state_t));

	gres_ptr->nic_cnt_found = NO_VAL;
	if (buffer) {
		safe_unpack32(&gres_ptr->nic_cnt_avail,  buffer);
		safe_unpack32(&gres_ptr->nic_cnt_alloc,  buffer);
		unpack_bit_str(&gres_ptr->nic_bit_alloc, buffer);
		if (gres_ptr->nic_bit_alloc == NULL)
			goto unpack_error;
		if (gres_ptr->nic_cnt_avail != 
		    bit_size(gres_ptr->nic_bit_alloc)) {
			gres_ptr->nic_bit_alloc =
					bit_realloc(gres_ptr->nic_bit_alloc,
						    gres_ptr->nic_cnt_avail);
			if (gres_ptr->nic_bit_alloc == NULL)
				goto unpack_error;
		}
		if (gres_ptr->nic_cnt_alloc != 
		    bit_set_count(gres_ptr->nic_bit_alloc)) {
			error("%s node_state_unpack bit count inconsistent",
			      plugin_name);
			goto unpack_error;
		}
	}

	*gres_data = gres_ptr;
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_BITMAP(gres_ptr->nic_bit_alloc);
	xfree(gres_ptr);
	*gres_data = NULL;
	return SLURM_ERROR;
}

extern void *node_state_dup(void *gres_data)
{
	nic_node_state_t *gres_ptr = (nic_node_state_t *) gres_data;
	nic_node_state_t *new_gres;

	if (gres_ptr == NULL)
		return NULL;

	new_gres = xmalloc(sizeof(nic_node_state_t));
	new_gres->nic_cnt_found  = gres_ptr->nic_cnt_found;
	new_gres->nic_cnt_config = gres_ptr->nic_cnt_config;
	new_gres->nic_cnt_avail  = gres_ptr->nic_cnt_avail;
	new_gres->nic_cnt_alloc  = gres_ptr->nic_cnt_alloc;
	new_gres->nic_bit_alloc  = bit_copy(gres_ptr->nic_bit_alloc);

	return new_gres;
}

extern void node_state_dealloc(void *gres_data)
{
	nic_node_state_t *gres_ptr = (nic_node_state_t *) gres_data;

	gres_ptr->nic_cnt_alloc = 0;
	if (gres_ptr->nic_bit_alloc) {
		int i = bit_size(gres_ptr->nic_bit_alloc) - 1;
		if (i > 0)
			bit_nclear(gres_ptr->nic_bit_alloc, 0, i);
	}
}

extern int node_state_realloc(void *job_gres_data, int node_offset,
			      void *node_gres_data)
{
	nic_job_state_t  *job_gres_ptr  = (nic_job_state_t *)  job_gres_data;
	nic_node_state_t *node_gres_ptr = (nic_node_state_t *) node_gres_data;
	int i, job_bit_size, node_bit_size;

	xassert(job_gres_ptr);
	xassert(node_gres_ptr);

	if (node_offset >= job_gres_ptr->node_cnt) {
		error("%s job node offset is bad (%d >= %u)",
		      plugin_name, node_offset, job_gres_ptr->node_cnt);
		return EINVAL;
	}

	if ((job_gres_ptr->nic_bit_alloc == NULL) ||
	    (job_gres_ptr->nic_bit_alloc[node_offset] == NULL)) {
		error("%s job bit_alloc is NULL", plugin_name);
		return EINVAL;
	}

	if (node_gres_ptr->nic_bit_alloc == NULL) {
		error("%s node bit_alloc is NULL", plugin_name);
		return EINVAL;
	}

	job_bit_size  = bit_size(job_gres_ptr->nic_bit_alloc[node_offset]);
	node_bit_size = bit_size(node_gres_ptr->nic_bit_alloc);
	if (job_bit_size > node_bit_size) {
		error("%s job/node bit size mismatch (%d != %d)",
		      plugin_name, job_bit_size, node_bit_size);
		/* Node needs to register with more resources, expand
		 * node's bitmap now so we can merge the data */
		node_gres_ptr->nic_bit_alloc =
				bit_realloc(node_gres_ptr->nic_bit_alloc,
					    job_bit_size);
		if (node_gres_ptr->nic_bit_alloc == NULL)
			fatal("bit_realloc: malloc failure");
		node_bit_size = job_bit_size;
	}
	if (job_bit_size < node_bit_size) {
		error("%s job/node bit size mismatch (%d != %d)",
		      plugin_name, job_bit_size, node_bit_size);
		/* Update what we can */
		node_bit_size = MIN(job_bit_size, node_bit_size);
		for (i=0; i<node_bit_size; i++) {
			if (!bit_test(job_gres_ptr->nic_bit_alloc[node_offset],
				      i))
				continue;
			node_gres_ptr->nic_cnt_alloc++;
			bit_set(node_gres_ptr->nic_bit_alloc, i);
		}
	} else {
		node_gres_ptr->nic_cnt_alloc += bit_set_count(job_gres_ptr->
							      nic_bit_alloc
							      [node_offset]);
		bit_or(node_gres_ptr->nic_bit_alloc,
		       job_gres_ptr->nic_bit_alloc[node_offset]);
	}

	return SLURM_SUCCESS;
}

extern void node_state_log(void *gres_data, char *node_name)
{
	nic_node_state_t *gres_ptr;

	xassert(gres_data);
	gres_ptr = (nic_node_state_t *) gres_data;
	info("%s state for %s", plugin_name, node_name);
	info("  nic_cnt found:%u configured:%u avail:%u alloc:%u",
	     gres_ptr->nic_cnt_found, gres_ptr->nic_cnt_config,
	     gres_ptr->nic_cnt_avail, gres_ptr->nic_cnt_alloc);
	if (gres_ptr->nic_bit_alloc) {
		char tmp_str[128];
		bit_fmt(tmp_str, sizeof(tmp_str), gres_ptr->nic_bit_alloc);
		info("  nic_bit_alloc:%s", tmp_str);
	} else {
		info("  nic_bit_alloc:NULL");
	}
}

extern int job_state_validate(char *config, void **gres_data)
{
	char *last = NULL;
	nic_job_state_t *gres_ptr;
	uint32_t cnt;
	uint8_t mult = 0;

	if (!strcmp(config, "nic")) {
		cnt = 1;
	} else if (!strncmp(config, "nic:", 4)) {
		cnt = strtol(config+4, &last, 10);
		if (last[0] == '\0')
			;
		else if ((last[0] == 'k') || (last[0] == 'K'))
			cnt *= 1024;
		else if (!strcasecmp(last, "*cpu"))
			mult = 1;
		else
			return SLURM_ERROR;
		if (cnt == 0)
			return SLURM_ERROR;
	} else
		return SLURM_ERROR;

	gres_ptr = xmalloc(sizeof(nic_job_state_t));
	gres_ptr->nic_cnt_alloc = cnt;
	gres_ptr->nic_cnt_mult  = mult;
	*gres_data = gres_ptr;
	return SLURM_SUCCESS;
}

extern void job_state_delete(void *gres_data)
{
	int i;
	nic_job_state_t *gres_ptr = (nic_job_state_t *) gres_data;

	if (gres_ptr == NULL)
		return;

	if (gres_ptr->nic_bit_alloc) {
		for (i=0; i<gres_ptr->node_cnt; i++)
			FREE_NULL_BITMAP(gres_ptr->nic_bit_alloc[i]);
		xfree(gres_ptr->nic_bit_alloc);
	}
	if (gres_ptr->nic_bit_step_alloc) {
		for (i=0; i<gres_ptr->node_cnt; i++)
			FREE_NULL_BITMAP(gres_ptr->nic_bit_step_alloc[i]);
		xfree(gres_ptr->nic_bit_step_alloc);
	}
	xfree(gres_ptr);
}

extern void *job_state_dup(void *gres_data)
{

	int i;
	nic_job_state_t *gres_ptr = (nic_job_state_t *) gres_data;
	nic_job_state_t *new_gres_ptr;

	if (gres_ptr == NULL)
		return NULL;

	new_gres_ptr = xmalloc(sizeof(nic_job_state_t));
	new_gres_ptr->nic_cnt_alloc	= gres_ptr->nic_cnt_alloc;
	new_gres_ptr->nic_cnt_mult	= gres_ptr->nic_cnt_mult;
	new_gres_ptr->node_cnt		= gres_ptr->node_cnt;
	new_gres_ptr->nic_bit_alloc	= xmalloc(sizeof(bitstr_t *) *
						  gres_ptr->node_cnt);
	for (i=0; i<gres_ptr->node_cnt; i++) {
		if (gres_ptr->nic_bit_alloc[i] == NULL)
			continue;
		new_gres_ptr->nic_bit_alloc[i] = bit_copy(gres_ptr->
							  nic_bit_alloc[i]);
	}
	return new_gres_ptr;
}

extern int job_state_pack(void *gres_data, Buf buffer)
{
	int i;
	nic_job_state_t *gres_ptr = (nic_job_state_t *) gres_data;

	pack32(gres_ptr->nic_cnt_alloc, buffer);
	pack8 (gres_ptr->nic_cnt_mult,  buffer);

	pack32(gres_ptr->node_cnt,      buffer);
	for (i=0; i<gres_ptr->node_cnt; i++)
		pack_bit_str(gres_ptr->nic_bit_alloc[i], buffer);

	return SLURM_SUCCESS;
}

extern int job_state_unpack(void **gres_data, Buf buffer)
{
	int i;
	nic_job_state_t *gres_ptr;

	gres_ptr = xmalloc(sizeof(nic_job_state_t));

	if (buffer) {
		safe_unpack32(&gres_ptr->nic_cnt_alloc,  buffer);
		safe_unpack8 (&gres_ptr->nic_cnt_mult,   buffer);

		safe_unpack32(&gres_ptr->node_cnt,       buffer);
		gres_ptr->nic_bit_alloc = xmalloc(sizeof(bitstr_t *) *
						  gres_ptr->node_cnt);
		for (i=0; i<gres_ptr->node_cnt; i++)
			unpack_bit_str(&gres_ptr->nic_bit_alloc[i], buffer);
	}

	*gres_data = gres_ptr;
	return SLURM_SUCCESS;

unpack_error:
	error("Unpacking %s job state info", plugin_name);
	if (gres_ptr->nic_bit_alloc) {
		for (i=0; i<gres_ptr->node_cnt; i++)
			FREE_NULL_BITMAP(gres_ptr->nic_bit_alloc[i]);
		xfree(gres_ptr->nic_bit_alloc);
	}
	xfree(gres_ptr);
	*gres_data = NULL;
	return SLURM_ERROR;
}

extern uint32_t job_test(void *job_gres_data, void *node_gres_data,
			 bool use_total_gres)
{
	uint32_t gres_avail;
	nic_job_state_t  *job_gres_ptr  = (nic_job_state_t *)  job_gres_data;
	nic_node_state_t *node_gres_ptr = (nic_node_state_t *) node_gres_data;

	gres_avail = node_gres_ptr->nic_cnt_avail;
	if (!use_total_gres)
		gres_avail -= node_gres_ptr->nic_cnt_alloc;

	if (job_gres_ptr->nic_cnt_mult == 0) {
		/* per node gres limit */
		if (job_gres_ptr->nic_cnt_alloc > gres_avail)
			return (uint32_t) 0;
		return NO_VAL;
	} else {
		/* per CPU gres limit */
		return (uint32_t) (gres_avail / job_gres_ptr->nic_cnt_alloc);
	}
}

extern int job_alloc(void *job_gres_data, void *node_gres_data,
		     int node_cnt, int node_offset, uint32_t cpu_cnt)
{
	int i;
	uint32_t gres_cnt;
	nic_job_state_t  *job_gres_ptr  = (nic_job_state_t *)  job_gres_data;
	nic_node_state_t *node_gres_ptr = (nic_node_state_t *) node_gres_data;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->nic_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_cnt);
	xassert(node_offset >= 0);
	xassert(job_gres_ptr);
	xassert(node_gres_ptr);
	xassert(node_gres_ptr->nic_bit_alloc);
	if (job_gres_ptr->node_cnt == 0) {
		job_gres_ptr->node_cnt = node_cnt;
		if (job_gres_ptr->nic_bit_alloc) {
			error("%s: node_cnt==0 and bit_alloc is set",
			      plugin_name);
			xfree(job_gres_ptr->nic_bit_alloc);
		}
		job_gres_ptr->nic_bit_alloc = xmalloc(sizeof(bitstr_t *) *
						      node_cnt);
	} else if (job_gres_ptr->node_cnt < node_cnt) {
		error("%s: node_cnt increase from %u to %d",
		      plugin_name, job_gres_ptr->node_cnt, node_cnt);
		if (node_offset >= job_gres_ptr->node_cnt)
			return SLURM_ERROR;
	} else if (job_gres_ptr->node_cnt > node_cnt) {
		error("%s: node_cnt decrease from %u to %d",
		      plugin_name, job_gres_ptr->node_cnt, node_cnt);
	}

	/*
	 * Check that sufficient resources exist on this node
	 */
	if (job_gres_ptr->nic_cnt_mult == 0)
		gres_cnt = job_gres_ptr->nic_cnt_alloc;
	else
		gres_cnt = (job_gres_ptr->nic_cnt_alloc * cpu_cnt);
	i =  node_gres_ptr->nic_cnt_alloc + gres_cnt;
	i -= node_gres_ptr->nic_cnt_avail;
	if (i > 0) {
		error("%s: overallocated resources by %d", plugin_name, i);
		/* proceed with request, give job what's available */
	}

	/*
	 * Select the specific resources to use for this job.
	 * We'll need to add topology information in the future
	 */
	if (job_gres_ptr->nic_bit_alloc[node_offset]) {
		/* Resuming a suspended job, resources already allocated */
		debug("%s: job's bit_alloc is already set for node %d",
		      plugin_name, node_offset);
		gres_cnt = MIN(bit_size(node_gres_ptr->nic_bit_alloc),
			       bit_size(job_gres_ptr->
					nic_bit_alloc[node_offset]));
		for (i=0; i<gres_cnt; i++) {
			if (bit_test(job_gres_ptr->nic_bit_alloc[node_offset],
				     i)) {
				bit_set(node_gres_ptr->nic_bit_alloc, i);
				node_gres_ptr->nic_cnt_alloc++;
			}
		}
	} else {
		job_gres_ptr->nic_bit_alloc[node_offset] = 
				bit_alloc(node_gres_ptr->nic_cnt_avail);
		if (job_gres_ptr->nic_bit_alloc[node_offset] == NULL)
			fatal("bit_copy: malloc failure");
		for (i=0; i<node_gres_ptr->nic_cnt_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->nic_bit_alloc, i))
				continue;
			bit_set(node_gres_ptr->nic_bit_alloc, i);
			bit_set(job_gres_ptr->nic_bit_alloc[node_offset], i);
			node_gres_ptr->nic_cnt_alloc++;
			gres_cnt--;
		}
	}

	return SLURM_SUCCESS;
}

extern int job_dealloc(void *job_gres_data, void *node_gres_data,
		       int node_offset)
{
	int i, len;
	nic_job_state_t  *job_gres_ptr  = (nic_job_state_t *)  job_gres_data;
	nic_node_state_t *node_gres_ptr = (nic_node_state_t *) node_gres_data;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->nic_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_offset >= 0);
	xassert(job_gres_ptr);
	xassert(node_gres_ptr);
	xassert(node_gres_ptr->nic_bit_alloc);
	if (job_gres_ptr->node_cnt <= node_offset) {
		error("%s: bad node_offset %d count is %u",
		      plugin_name, node_offset, job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}
	if (job_gres_ptr->nic_bit_alloc == NULL) {
		error("%s: job's bitmap is NULL", plugin_name);
		return SLURM_ERROR;
	}
	if (job_gres_ptr->nic_bit_alloc[node_offset] == NULL) {
		error("%s: job's bitmap is empty", plugin_name);
		return SLURM_ERROR;
	}

	len = bit_size(job_gres_ptr->nic_bit_alloc[node_offset]);
	i   = bit_size(node_gres_ptr->nic_bit_alloc);
	if (i != len) {
		error("%s: job and node bitmap sizes differ (%d != %d)",
		      plugin_name, len, i);
		len = MIN(len, i);
		/* proceed with request, make best effort */
	}
	for (i=0; i<len; i++) {
		if (!bit_test(job_gres_ptr->nic_bit_alloc[node_offset], i))
			continue;
		bit_clear(node_gres_ptr->nic_bit_alloc, i);
		/* NOTE: Do not clear bit from
		 * job_gres_ptr->nic_bit_alloc[node_offset]
		 * since this may only be an emulated deallocate */
		node_gres_ptr->nic_cnt_alloc--;
	}

	return SLURM_SUCCESS;
}

extern void job_state_log(void *gres_data, uint32_t job_id)
{
	nic_job_state_t *gres_ptr;
	char *mult, tmp_str[128];
	int i;

	xassert(gres_data);
	gres_ptr = (nic_job_state_t *) gres_data;
	info("%s state for job %u", plugin_name, job_id);
	if (gres_ptr->nic_cnt_mult)
		mult = "cpu";
	else
		mult = "node";
	info("  nic_cnt:%u per %s node_cnt:%u", gres_ptr->nic_cnt_alloc, mult,
	     gres_ptr->node_cnt);

	if (gres_ptr->node_cnt && gres_ptr->nic_bit_alloc) {
		for (i=0; i<gres_ptr->node_cnt; i++) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ptr->nic_bit_alloc[i]);
			info("  nic_bit_alloc[%d]:%s", i, tmp_str);
		}
	} else {
		info("  nic_bit_alloc:NULL");
	}

	if (gres_ptr->node_cnt && gres_ptr->nic_bit_step_alloc) {
		for (i=0; i<gres_ptr->node_cnt; i++) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ptr->nic_bit_step_alloc[i]);
			info("  nic_bit_step_alloc[%d]:%s", i, tmp_str);
		}
	} else {
		info("  nic_bit_step_alloc:NULL");
	}
}

extern void step_state_delete(void *gres_data)
{
	int i;
	nic_step_state_t *gres_ptr = (nic_step_state_t *) gres_data;

	if (gres_ptr == NULL)
		return;

	if (gres_ptr->nic_bit_alloc) {
		for (i=0; i<gres_ptr->node_cnt; i++)
			FREE_NULL_BITMAP(gres_ptr->nic_bit_alloc[i]);
		xfree(gres_ptr->nic_bit_alloc);
	}
	xfree(gres_ptr);
}

extern int step_state_validate(char *config, void **gres_data)
{
	char *last = NULL;
	nic_job_state_t *gres_ptr;
	uint32_t cnt;
	uint8_t mult = 0;

	if (!strcmp(config, "nic")) {
		cnt = 1;
	} else if (!strncmp(config, "nic:", 4)) {
		cnt = strtol(config+4, &last, 10);
		if (last[0] == '\0')
			;
		else if ((last[0] == 'k') || (last[0] == 'K'))
			cnt *= 1024;
		else if (!strcasecmp(last, "*cpu"))
			mult = 1;
		else
			return SLURM_ERROR;
		if (cnt == 0)
			return SLURM_ERROR;
	} else
		return SLURM_ERROR;

	gres_ptr = xmalloc(sizeof(nic_step_state_t));
	gres_ptr->nic_cnt_alloc = cnt;
	gres_ptr->nic_cnt_mult  = mult;
	*gres_data = gres_ptr;
	return SLURM_SUCCESS;
}

extern void *step_state_dup(void *gres_data)
{

	int i;
	nic_step_state_t *gres_ptr = (nic_step_state_t *) gres_data;
	nic_step_state_t *new_gres_ptr;

	if (gres_ptr == NULL)
		return NULL;

	new_gres_ptr = xmalloc(sizeof(nic_step_state_t));
	new_gres_ptr->nic_cnt_alloc	= gres_ptr->nic_cnt_alloc;
	new_gres_ptr->nic_cnt_mult	= gres_ptr->nic_cnt_mult;
	new_gres_ptr->node_cnt		= gres_ptr->node_cnt;
	new_gres_ptr->nic_bit_alloc	= xmalloc(sizeof(bitstr_t *) *
						  gres_ptr->node_cnt);
	for (i=0; i<gres_ptr->node_cnt; i++) {
		if (gres_ptr->nic_bit_alloc[i] == NULL)
			continue;
		new_gres_ptr->nic_bit_alloc[i] = bit_copy(gres_ptr->
							  nic_bit_alloc[i]);
	}
	return new_gres_ptr;
}

extern int step_state_pack(void *gres_data, Buf buffer)
{
	int i;
	nic_step_state_t *gres_ptr = (nic_step_state_t *) gres_data;

	pack32(gres_ptr->nic_cnt_alloc, buffer);
	pack8 (gres_ptr->nic_cnt_mult,  buffer);

	pack32(gres_ptr->node_cnt,      buffer);
	for (i=0; i<gres_ptr->node_cnt; i++)
		pack_bit_str(gres_ptr->nic_bit_alloc[i], buffer);

	return SLURM_SUCCESS;
}

extern int step_state_unpack(void **gres_data, Buf buffer)
{
	int i;
	nic_step_state_t *gres_ptr;

	gres_ptr = xmalloc(sizeof(nic_step_state_t));

	if (buffer) {
		safe_unpack32(&gres_ptr->nic_cnt_alloc,  buffer);
		safe_unpack8 (&gres_ptr->nic_cnt_mult,   buffer);

		safe_unpack32(&gres_ptr->node_cnt,       buffer);
		gres_ptr->nic_bit_alloc = xmalloc(sizeof(bitstr_t *) *
						  gres_ptr->node_cnt);
		for (i=0; i<gres_ptr->node_cnt; i++)
			unpack_bit_str(&gres_ptr->nic_bit_alloc[i], buffer);
	}

	*gres_data = gres_ptr;
	return SLURM_SUCCESS;

unpack_error:
	error("Unpacking %s step state info", plugin_name);
	if (gres_ptr->nic_bit_alloc) {
		for (i=0; i<gres_ptr->node_cnt; i++)
			FREE_NULL_BITMAP(gres_ptr->nic_bit_alloc[i]);
		xfree(gres_ptr->nic_bit_alloc);
	}
	xfree(gres_ptr);
	*gres_data = NULL;
	return SLURM_ERROR;
}

extern void step_state_log(void *gres_data, uint32_t job_id, uint32_t step_id)
{
	nic_step_state_t *gres_ptr = (nic_step_state_t *) gres_data;
	char *mult, tmp_str[128];
	int i;

	xassert(gres_ptr);
	info("%s state for step %u.%u", plugin_name, job_id, step_id);
	if (gres_ptr->nic_cnt_mult)
		mult = "cpu";
	else
		mult = "node";
	info("  nic_cnt:%u per %s node_cnt:%u", gres_ptr->nic_cnt_alloc, mult,
	     gres_ptr->node_cnt);

	if (gres_ptr->node_cnt && gres_ptr->nic_bit_alloc) {
		for (i=0; i<gres_ptr->node_cnt; i++) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ptr->nic_bit_alloc[i]);
			info("  nic_bit_alloc[%d]:%s", i, tmp_str);
		}
	} else {
		info("  nic_bit_alloc:NULL");
	}
}

extern uint32_t step_test(void *step_gres_data, void *job_gres_data,
			  int node_offset, bool ignore_alloc)
{
	nic_job_state_t  *job_gres_ptr  = (nic_job_state_t *)  job_gres_data;
	nic_step_state_t *step_gres_ptr = (nic_step_state_t *) step_gres_data;
	uint32_t gres_cnt;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if (node_offset == NO_VAL) {
		if (step_gres_ptr->nic_cnt_alloc > job_gres_ptr->nic_cnt_alloc)
			return 0;
		return NO_VAL;
	}

	if (node_offset >= job_gres_ptr->node_cnt) {
		error("%s step_test node offset invalid (%d >= %u)",
		      plugin_name, node_offset, job_gres_ptr->node_cnt);
		return 0;
	}
	if ((job_gres_ptr->nic_bit_alloc == NULL) ||
	    (job_gres_ptr->nic_bit_alloc[node_offset] == NULL)) {
		error("%s step_test nic_bit_alloc is NULL", plugin_name);
		return 0;
	}

	gres_cnt = bit_set_count(job_gres_ptr->nic_bit_alloc[node_offset]);
	if (!ignore_alloc &&
	    job_gres_ptr->nic_bit_step_alloc &&
	    job_gres_ptr->nic_bit_step_alloc[node_offset]) {
		gres_cnt -= bit_set_count(job_gres_ptr->
					  nic_bit_step_alloc[node_offset]);
	}
	if (step_gres_ptr->nic_cnt_mult)	/* Gres count per CPU */
		gres_cnt /= step_gres_ptr->nic_cnt_alloc;
	else if (step_gres_ptr->nic_cnt_alloc > gres_cnt)
		gres_cnt = 0;
	else
		gres_cnt = NO_VAL;

	return gres_cnt;
}

extern int step_alloc(void *step_gres_data, void *job_gres_data,
		      int node_offset, int cpu_cnt)
{
	nic_job_state_t  *job_gres_ptr  = (nic_job_state_t *)  job_gres_data;
	nic_step_state_t *step_gres_ptr = (nic_step_state_t *) step_gres_data;
	uint32_t gres_avail, gres_needed;
	bitstr_t *nic_bit_alloc;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);
	if (node_offset >= job_gres_ptr->node_cnt) {
		error("%s step_alloc node offset invalid (%d >= %u)",
		      plugin_name, node_offset, job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}
	if ((job_gres_ptr->nic_bit_alloc == NULL) ||
	    (job_gres_ptr->nic_bit_alloc[node_offset] == NULL)) {
		error("%s step_alloc nic_bit_alloc is NULL", plugin_name);
		return SLURM_ERROR;
	}

	nic_bit_alloc = bit_copy(job_gres_ptr->nic_bit_alloc[node_offset]);
	if (nic_bit_alloc == NULL)
		fatal("bit_copy malloc failure");
	if (job_gres_ptr->nic_bit_step_alloc &&
	    job_gres_ptr->nic_bit_step_alloc[node_offset]) {
		bit_not(job_gres_ptr->nic_bit_step_alloc[node_offset]);
		bit_and(nic_bit_alloc,
			job_gres_ptr->nic_bit_step_alloc[node_offset]);
		bit_not(job_gres_ptr->nic_bit_step_alloc[node_offset]);
	}
	gres_avail  = bit_set_count(nic_bit_alloc);
	gres_needed = step_gres_ptr->nic_cnt_alloc;
	if (step_gres_ptr->nic_cnt_mult)
		gres_needed *= cpu_cnt;
	if (gres_needed > gres_avail) {
		error("%s step oversubscribing resources on node %d",
		      plugin_name, node_offset);
	} else {
		int gres_rem = gres_needed;
		int i, len = bit_size(nic_bit_alloc);
		for (i=0; i<len; i++) {
			if (gres_rem > 0) {
				if (bit_test(nic_bit_alloc, i))
					gres_rem--;
			} else {
				bit_clear(nic_bit_alloc, i);
			}
		}
	}

	if (job_gres_ptr->nic_bit_step_alloc == NULL) {
		job_gres_ptr->nic_bit_step_alloc =
			xmalloc(sizeof(bitstr_t *) * job_gres_ptr->node_cnt);
	}
	if (job_gres_ptr->nic_bit_step_alloc[node_offset]) {
		bit_or(job_gres_ptr->nic_bit_step_alloc[node_offset],
		       nic_bit_alloc);
	} else {
		job_gres_ptr->nic_bit_step_alloc[node_offset] =
			bit_copy(nic_bit_alloc);
	}
	if (step_gres_ptr->nic_bit_alloc == NULL) {
		step_gres_ptr->nic_bit_alloc = xmalloc(sizeof(bitstr_t *) *
						       job_gres_ptr->node_cnt);
		step_gres_ptr->node_cnt = job_gres_ptr->node_cnt;
	}
	if (step_gres_ptr->nic_bit_alloc[node_offset]) {
		error("%s step bit_alloc already exists", plugin_name);
		bit_or(step_gres_ptr->nic_bit_alloc[node_offset],nic_bit_alloc);
		FREE_NULL_BITMAP(nic_bit_alloc);
	} else {
		step_gres_ptr->nic_bit_alloc[node_offset] = nic_bit_alloc;
	}

	return SLURM_SUCCESS;
}

extern int step_dealloc(void *step_gres_data, void *job_gres_data)
{

	nic_job_state_t  *job_gres_ptr  = (nic_job_state_t *)  job_gres_data;
	nic_step_state_t *step_gres_ptr = (nic_step_state_t *) step_gres_data;
	uint32_t i, j, node_cnt;
	int len_j, len_s;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);
	node_cnt = MIN(job_gres_ptr->node_cnt, step_gres_ptr->node_cnt);
	if (step_gres_ptr->nic_bit_alloc == NULL) {
		error("%s step dealloc bit_alloc is NULL", plugin_name);
		return SLURM_ERROR;
	}
	if (job_gres_ptr->nic_bit_alloc == NULL) {
		error("%s step dealloc, job's bit_alloc is NULL", plugin_name);
		return SLURM_ERROR;
	}
	for (i=0; i<node_cnt; i++) {
		if (step_gres_ptr->nic_bit_alloc[i] == NULL)
			continue;
		if (job_gres_ptr->nic_bit_alloc[i] == NULL) {
			error("%s step dealloc, job's bit_alloc[%d] is NULL",
			      plugin_name, i);
			continue;
		}
		len_j = bit_size(job_gres_ptr->nic_bit_alloc[i]);
		len_s = bit_size(step_gres_ptr->nic_bit_alloc[i]);
		if (len_j != len_s) {
			error("%s step dealloc, bit_alloc[%d] size mis-match"
			      "(%d != %d)", len_j, len_s);
			len_j = MIN(len_j, len_s);
		}
		for (j=0; j<len_j; j++) {
			if (!bit_test(step_gres_ptr->nic_bit_alloc[i], j))
				continue;
			if (job_gres_ptr->nic_bit_step_alloc &&
			    job_gres_ptr->nic_bit_step_alloc[i]) {
				bit_clear(job_gres_ptr->nic_bit_step_alloc[i],
					  j);
			}
		}
		FREE_NULL_BITMAP(step_gres_ptr->nic_bit_alloc[i]);
	}

	return SLURM_SUCCESS;
}
