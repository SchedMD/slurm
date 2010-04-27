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
const char	plugin_name[]		= "Gres GPU plugin";
const char	plugin_type[]		= "gres/gpu";
const uint32_t	plugin_id		= 101;
const uint32_t	plugin_version		= 100;
const uint32_t	min_plug_version	= 100;

/* Gres configuration loaded/used by slurmd. Modify or expand as
 * additional information becomes available (e.g. topology). */
typedef struct gpu_config {
	bool     loaded;
	uint32_t gpu_cnt;
} gpu_config_t;
static gpu_config_t gres_config;

/* Gres node state as used by slurmctld. Includes data from gres_config loaded
 * from slurmd, resources configured (may be more or less than actually found)
 * plus resource allocation information. */
typedef struct gpu_node_state {
	/* Actual hardware found */
	uint32_t gpu_cnt_found;

	/* Configured resources via Gres parameter */
	uint32_t gpu_cnt_config;

	/* Total resources available for allocation to jobs */
	uint32_t gpu_cnt_avail;

	/* Resources currently allocated to jobs */
	uint32_t  gpu_cnt_alloc;
	bitstr_t *gpu_bit_alloc;
} gpu_node_state_t;

/* Gres job state as used by slurmctld. */
typedef struct gpu_job_state {
	/* Count of resources needed */
	uint32_t gpu_cnt_alloc;

	/* If 0 then gpu_cnt_alloc is per node,
	 * if 1 then gpu_cnt_alloc is per CPU */
	uint8_t  gpu_cnt_mult;
} gpu_job_state_t;

/*
 * This will be the output for "--gres=help" option.
 * Called only by salloc, sbatch and srun.
 */
extern int help_msg(char *msg, int msg_size)
{
	char *response = "gpu[:count[*cpu]]";
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
	gres_config.gpu_cnt = 8;
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
	pack32(gres_config.gpu_cnt, buffer);

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
		gres_config.gpu_cnt = NO_VAL;
		return SLURM_SUCCESS;
	}

	safe_unpack32(&version, buffer);

	if (version == plugin_version) {
		safe_unpack32(&gres_config.gpu_cnt, buffer);
		/* info("gpu_cnt=%u", gres_config.gpu_cnt); */
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
extern void node_config_delete(void *gres_data)
{
	gpu_node_state_t *gres_ptr;

	xassert(gres_data);
	gres_ptr = (gpu_node_state_t *) gres_data;
	if (gres_ptr->gpu_bit_alloc)
		bit_free(gres_ptr->gpu_bit_alloc);
	xfree(gres_ptr);
}

/*
 * Validate a node's configuration and put a gres record onto a list
 * Called immediately after unpack_node_config().
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
	gpu_node_state_t *gres_ptr;
	char *node_gres_config, *tok, *last = NULL;
	int32_t gres_config_cnt = -1;
	bool updated_config = false;

	xassert(gres_data);
	gres_ptr = (gpu_node_state_t *) *gres_data;
	if (gres_ptr == NULL) {
		gres_ptr = xmalloc(sizeof(gpu_node_state_t));
		*gres_data = gres_ptr;
		gres_ptr->gpu_cnt_found = gres_config.gpu_cnt;
		updated_config = true;
	} else if (gres_ptr->gpu_cnt_found != gres_config.gpu_cnt) {
		if (gres_ptr->gpu_cnt_found != NO_VAL) {
			info("%s count changed for node %s from %u to %u",
			     plugin_name, node_name, gres_config.gpu_cnt,
			     gres_ptr->gpu_cnt_found);
		}
		gres_ptr->gpu_cnt_found = gres_config.gpu_cnt;
		updated_config = true;
	}

	/* If the resource isn't configured for use with this node,
	 * just return leaving gpu_cnt_config=0, gpu_cnt_avail=0, etc. */
	if ((orig_config == NULL) || (orig_config[0] == '\0') ||
	    (updated_config == false))
		return SLURM_SUCCESS;

	node_gres_config = xstrdup(orig_config);
	tok = strtok_r(node_gres_config, ",", &last);
	while (tok) {
		if (!strcmp(tok, "gpu")) {
			gres_config_cnt = 1;
			break;
		}
		if (!strncmp(tok, "gpu:", 4)) {
			gres_config_cnt = strtol(tok+4, &last, 10);
			if (last[0] == '\0')
				;
			else if ((last[0] == 'k') || (last[0] == 'K'))
				gres_config_cnt *= 1024;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	gres_ptr->gpu_cnt_config = gres_config_cnt;
	xfree(node_gres_config);

	if (gres_config_cnt < 0)
		;	/* not configured for use */
	else if (fast_schedule == 0)
		gres_ptr->gpu_cnt_avail = gres_ptr->gpu_cnt_found;
	else
		gres_ptr->gpu_cnt_avail = gres_ptr->gpu_cnt_config;

	if (gres_ptr->gpu_bit_alloc == NULL) {
		gres_ptr->gpu_bit_alloc = bit_alloc(gres_ptr->gpu_cnt_avail);
	} else if (gres_ptr->gpu_cnt_avail > 
		   bit_size(gres_ptr->gpu_bit_alloc)) {
		gres_ptr->gpu_bit_alloc = bit_realloc(gres_ptr->gpu_bit_alloc,
						      gres_ptr->gpu_cnt_avail);
	}
	if (gres_ptr->gpu_bit_alloc == NULL)
		fatal("bit_alloc: malloc failure");

	if ((fast_schedule < 2) && 
	    (gres_ptr->gpu_cnt_found < gres_ptr->gpu_cnt_config)) {
		if (reason_down && (*reason_down == NULL))
			*reason_down = xstrdup("gres/gpu count too low");
		rc = EINVAL;
	} else if ((fast_schedule == 0) && 
		   (gres_ptr->gpu_cnt_found > gres_ptr->gpu_cnt_config)) {
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
			if (strcmp(tok, "gpu") && strncmp(tok, "gpu:", 4)) {
				xstrcat(new_configured_res, tok);
			} else {
				xstrfmtcat(new_configured_res, "gpu:%u",
					   gres_ptr->gpu_cnt_found);
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
	gpu_node_state_t *gres_ptr;
	char *node_gres_config, *tok, *last = NULL;
	int32_t gres_config_cnt = 0;

	xassert(gres_data);
	gres_ptr = (gpu_node_state_t *) *gres_data;
	if (gres_ptr == NULL) {
		/* Assume that node has not yet registerd */
		info("%s record is NULL for node %s", plugin_name, node_name);
		return rc;
	}

	node_gres_config = xstrdup(orig_config);
	tok = strtok_r(node_gres_config, ",", &last);
	while (tok) {
		if (!strcmp(tok, "gpu")) {
			gres_config_cnt = 1;
			break;
		}
		if (!strncmp(tok, "gpu:", 4)) {
			gres_config_cnt = strtol(tok+4, &last, 10);
			if (last[0] == '\0')
				;
			else if ((last[0] == 'k') || (last[0] == 'K'))
				gres_config_cnt *= 1024;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	gres_ptr->gpu_cnt_config = gres_config_cnt;
	xfree(node_gres_config);

	if (fast_schedule == 0)
		gres_ptr->gpu_cnt_avail = gres_ptr->gpu_cnt_found;
	else if (gres_ptr->gpu_cnt_config != NO_VAL)
		gres_ptr->gpu_cnt_avail = gres_ptr->gpu_cnt_config;

	if (gres_ptr->gpu_bit_alloc == NULL) {
		gres_ptr->gpu_bit_alloc = bit_alloc(gres_ptr->gpu_cnt_avail);
	} else if (gres_ptr->gpu_cnt_avail > 
		   bit_size(gres_ptr->gpu_bit_alloc)) {
		gres_ptr->gpu_bit_alloc = bit_realloc(gres_ptr->gpu_bit_alloc,
						      gres_ptr->gpu_cnt_avail);
	}
	if (gres_ptr->gpu_bit_alloc == NULL)
		fatal("bit_alloc: malloc failure");

	if ((fast_schedule < 2) && 
	    (gres_ptr->gpu_cnt_found < gres_ptr->gpu_cnt_config)) {
		/* Do not set node DOWN, but give the node 
		 * a chance to register with more resources */
		gres_ptr->gpu_cnt_found = NO_VAL;
	} else if ((fast_schedule == 0) && 
		   (gres_ptr->gpu_cnt_found > gres_ptr->gpu_cnt_config)) {
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
			if (strcmp(tok, "gpu") && strncmp(tok, "gpu:", 4)) {
				xstrcat(new_configured_res, tok);
			} else {
				xstrfmtcat(new_configured_res, "gpu:%u",
					   gres_ptr->gpu_cnt_found);
			}
			tok = strtok_r(NULL, ",", &last);
		}
		xfree(node_gres_config);
		xfree(*new_config);
		*new_config = new_configured_res;
	}

	return rc;
}

extern int pack_node_state(void *gres_data, Buf buffer)
{
	gpu_node_state_t *gres_ptr = (gpu_node_state_t *) gres_data;

	pack32(gres_ptr->gpu_cnt_avail,  buffer);
	pack32(gres_ptr->gpu_cnt_alloc,  buffer);
	pack_bit_str(gres_ptr->gpu_bit_alloc, buffer);

	return SLURM_SUCCESS;
}

extern int unpack_node_state(void **gres_data, Buf buffer)
{
	gpu_node_state_t *gres_ptr;

	gres_ptr = xmalloc(sizeof(gpu_node_state_t));

	gres_ptr->gpu_cnt_found = NO_VAL;
	if (buffer) {
		safe_unpack32(&gres_ptr->gpu_cnt_avail,  buffer);
		safe_unpack32(&gres_ptr->gpu_cnt_alloc,  buffer);
		unpack_bit_str(&gres_ptr->gpu_bit_alloc, buffer);
		if (gres_ptr->gpu_bit_alloc == NULL)
			goto unpack_error;
		if (gres_ptr->gpu_cnt_avail != 
		    bit_size(gres_ptr->gpu_bit_alloc)) {
			gres_ptr->gpu_bit_alloc =
					bit_realloc(gres_ptr->gpu_bit_alloc,
						    gres_ptr->gpu_cnt_avail);
			if (gres_ptr->gpu_bit_alloc == NULL)
				goto unpack_error;
		}
		if (gres_ptr->gpu_cnt_alloc != 
		    bit_set_count(gres_ptr->gpu_bit_alloc)) {
			error("%s unpack_node_state bit count inconsistent",
			      plugin_name);
			goto unpack_error;
		}
	}

	*gres_data = gres_ptr;
	return SLURM_SUCCESS;

unpack_error:
	if (gres_ptr->gpu_bit_alloc)
		bit_free(gres_ptr->gpu_bit_alloc);
	xfree(gres_ptr);
	*gres_data = NULL;
	return SLURM_ERROR;
}

extern void node_state_log(void *gres_data, char *node_name)
{
	gpu_node_state_t *gres_ptr;

	xassert(gres_data);
	gres_ptr = (gpu_node_state_t *) gres_data;
	info("%s state for %s", plugin_name, node_name);
	info("  gpu_cnt found:%u configured:%u avail:%u alloc:%u",
	     gres_ptr->gpu_cnt_found, gres_ptr->gpu_cnt_config,
	     gres_ptr->gpu_cnt_avail, gres_ptr->gpu_cnt_alloc);
	if (gres_ptr->gpu_bit_alloc) {
		char tmp_str[128];
		bit_fmt(tmp_str, sizeof(tmp_str), gres_ptr->gpu_bit_alloc);
		info("  gpu_bit_alloc:%s", tmp_str);
	} else {
		info("  gpu_bit_alloc:NULL");
	}
}

extern void job_config_delete(void *gres_data)
{
	xfree(gres_data);
}

extern int job_gres_validate(char *config, void **gres_data)
{
	char *last = NULL;
	gpu_job_state_t *gres_ptr;
	uint32_t cnt;
	uint8_t mult = 0;

	if (!strcmp(config, "gpu")) {
		cnt = 1;
	} else if (!strncmp(config, "gpu:", 4)) {
		cnt = strtol(config+4, &last, 10);
		if (last[0] == '\0')
			;
		else if ((last[0] == 'k') || (last[0] == 'K'))
			cnt *= 1024;
		else if (!strcasecmp(last, "*cpu"))
			mult = 1;
		else
			return SLURM_ERROR;
	} else
		return SLURM_ERROR;

	gres_ptr = xmalloc(sizeof(gpu_job_state_t));
	gres_ptr->gpu_cnt_alloc = cnt;
	gres_ptr->gpu_cnt_mult  = mult;
	*gres_data = gres_ptr;
	return SLURM_SUCCESS;
}

extern int pack_job_state(void *gres_data, Buf buffer)
{
	gpu_job_state_t *gres_ptr = (gpu_job_state_t *) gres_data;

	pack32(gres_ptr->gpu_cnt_alloc,  buffer);
	pack8 (gres_ptr->gpu_cnt_mult,  buffer);

	return SLURM_SUCCESS;
}

extern int unpack_job_state(void **gres_data, Buf buffer)
{
	gpu_job_state_t *gres_ptr;

	gres_ptr = xmalloc(sizeof(gpu_job_state_t));

	if (buffer) {
		safe_unpack32(&gres_ptr->gpu_cnt_alloc,  buffer);
		safe_unpack8 (&gres_ptr->gpu_cnt_mult,   buffer);
	}

	*gres_data = gres_ptr;
	return SLURM_SUCCESS;

unpack_error:
	xfree(gres_ptr);
	*gres_data = NULL;
	return SLURM_ERROR;
}

extern uint32_t cpus_usable_by_job(void *job_gres_data, void *node_gres_data,
				   bool use_total_gres)
{
	uint32_t gres_avail;
	gpu_job_state_t  *job_gres_ptr  = (gpu_job_state_t *)  job_gres_data;
	gpu_node_state_t *node_gres_ptr = (gpu_node_state_t *) node_gres_data;

	gres_avail = node_gres_ptr->gpu_cnt_avail;
	if (!use_total_gres)
		gres_avail -= node_gres_ptr->gpu_cnt_alloc;

	if (job_gres_ptr->gpu_cnt_mult == 0) {
		/* per gres node limit */
		if (job_gres_ptr->gpu_cnt_alloc > gres_avail)
			return (uint32_t) 0;
		return NO_VAL;
	} else {
		/* per gres CPU limit */
		return (uint32_t) (gres_avail / job_gres_ptr->gpu_cnt_alloc);
	}
}

extern void job_state_log(void *gres_data, uint32_t job_id)
{
	gpu_job_state_t *gres_ptr;
	char *mult;

	xassert(gres_data);
	gres_ptr = (gpu_job_state_t *) gres_data;
	info("%s state for job %u", plugin_name, job_id);
	if (gres_ptr->gpu_cnt_mult)
		mult = "cpu";
	else
		mult = "node";
	info("  gpu_cnt %u per %s", gres_ptr->gpu_cnt_alloc, mult);
}
