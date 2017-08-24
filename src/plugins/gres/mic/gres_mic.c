/*****************************************************************************\
 *  gres_mic.c - Support MICs as a generic resources.
 *****************************************************************************
 *  Copyright (C) 2012 CSC-IT Center for Science Ltd.
 *  Written by Olli-Pekka Lehto
 *  Based upon gres_gpu.c with the copyright notice shown below:
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/bitstring.h"
#include "src/common/env.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/xstring.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
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
 *  * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char	plugin_name[]		= "Gres MIC plugin";
const char	plugin_type[]		= "gres/mic";
const uint32_t	plugin_version		= SLURM_VERSION_NUMBER;

static char	gres_name[]		= "mic";

static int *mic_devices = NULL;
static int nb_available_files;

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 * In the general case, no code would need to be changed.
 */
extern int node_config_load(List gres_conf_list)
{
	int i, rc = SLURM_SUCCESS;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	int nb_mic = 0;	/* Number of MICs in the list */
	int available_files_index = 0;

	xassert(gres_conf_list);
	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(iter))) {
		if (xstrcmp(gres_slurmd_conf->name, gres_name))
			continue;
		if (gres_slurmd_conf->file)
			nb_mic++;
	}
	list_iterator_destroy(iter);
	mic_devices = NULL;
	nb_available_files = -1;

	/* (Re-)Allocate memory if number of files changed */
	if (nb_mic != nb_available_files) {
		xfree(mic_devices);	/* No-op if NULL */
		mic_devices = (int *) xmalloc(sizeof(int) * nb_mic);
		nb_available_files = nb_mic;
		for (i = 0; i < nb_available_files; i++)
			mic_devices[i] = -1;
	}

	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(iter))) {
		if ((xstrcmp(gres_slurmd_conf->name, gres_name) == 0) &&
		    gres_slurmd_conf->file) {
			/* Populate mic_devices array with number
			 * at end of the file name */
			for (i = 0; gres_slurmd_conf->file[i]; i++) {
				if (!isdigit(gres_slurmd_conf->file[i]))
					continue;
				mic_devices[available_files_index] =
					atoi(gres_slurmd_conf->file + i);
				break;
			}
			available_files_index++;
		}
	}
	list_iterator_destroy(iter);

	if (rc != SLURM_SUCCESS)
		fatal("%s failed to load configuration", plugin_name);

	for (i = 0; i < nb_available_files; i++)
		info("mic %d is device number %d", i, mic_devices[i]);

	return rc;
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job's GRES state.
 */
extern void job_set_env(char ***job_env_ptr, void *gres_ptr, int node_inx)
{
	int i, len;
	char *dev_list = NULL;
	gres_job_state_t *gres_job_ptr = (gres_job_state_t *) gres_ptr;

	if ((gres_job_ptr != NULL) &&
	    (node_inx >= 0) && (node_inx < gres_job_ptr->node_cnt) &&
	    (gres_job_ptr->gres_bit_alloc != NULL) &&
	    (gres_job_ptr->gres_bit_alloc[node_inx] != NULL)) {
		len = bit_size(gres_job_ptr->gres_bit_alloc[node_inx]);
		for (i=0; i<len; i++) {
			if (!bit_test(gres_job_ptr->gres_bit_alloc[node_inx],i))
				continue;
			if (!dev_list)
				dev_list = xmalloc(128);
			else
				xstrcat(dev_list, ",");
			if (mic_devices && (mic_devices[i] >= 0))
				xstrfmtcat(dev_list, "%d", mic_devices[i]);
			else
				xstrfmtcat(dev_list, "%d", i);
		}
	}
	if (dev_list) {
		env_array_overwrite(job_env_ptr,"OFFLOAD_DEVICES",
				    dev_list);
		xfree(dev_list);
	} else {
		/* The gres.conf file must identify specific device files
		 * in order to set the OFFLOAD_DEVICES env var */
		error("gres/mic unable to set OFFLOAD_DEVICES, "
		      "no device files configured");
	}
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job step's GRES state.
 */
extern void step_set_env(char ***job_env_ptr, void *gres_ptr)
{
	int i, len;
	char *dev_list = NULL;
	gres_step_state_t *gres_step_ptr = (gres_step_state_t *) gres_ptr;

	if ((gres_step_ptr != NULL) &&
	    (gres_step_ptr->node_cnt == 1) &&
	    (gres_step_ptr->gres_bit_alloc != NULL) &&
	    (gres_step_ptr->gres_bit_alloc[0] != NULL)) {
		len = bit_size(gres_step_ptr->gres_bit_alloc[0]);
		for (i=0; i<len; i++) {
			if (!bit_test(gres_step_ptr->gres_bit_alloc[0], i))
				continue;
			if (!dev_list)
				dev_list = xmalloc(128);
			else
				xstrcat(dev_list, ",");
			if (mic_devices && (mic_devices[i] >= 0))
				xstrfmtcat(dev_list, "%d", mic_devices[i]);
			else
				xstrfmtcat(dev_list, "%d", i);
		}
	}
	if (dev_list) {
		env_array_overwrite(job_env_ptr,"OFFLOAD_DEVICES",
				    dev_list);
		xfree(dev_list);
	} else {
		/* The gres.conf file must identify specific device files
		 * in order to set the OFFLOAD_DEVICES env var */
		error("gres/mic unable to set OFFLOAD_DEVICES, "
		      "no device files configured");
	}
}

/*
 * Reset environment variables as appropriate for a job (i.e. this one tasks)
 * based upon the job step's GRES state and assigned CPUs.
 */
extern void step_reset_env(char ***job_env_ptr, void *gres_ptr,
			   bitstr_t *usable_gres)
{
	int i, len, first_match = -1;
	char *dev_list = NULL;
	gres_step_state_t *gres_step_ptr = (gres_step_state_t *) gres_ptr;

	if ((gres_step_ptr != NULL) &&
	    (gres_step_ptr->node_cnt == 1) &&
	    (gres_step_ptr->gres_bit_alloc != NULL) &&
	    (gres_step_ptr->gres_bit_alloc[0] != NULL) &&
	    (usable_gres != NULL)) {
		len = MIN(bit_size(gres_step_ptr->gres_bit_alloc[0]),
			  bit_size(usable_gres));
		for (i = 0; i < len; i++) {
			if (!bit_test(gres_step_ptr->gres_bit_alloc[0], i))
				continue;
			if (first_match == -1)
				first_match = i;
			if (!bit_test(usable_gres, i))
				continue;
			if (!dev_list)
				dev_list = xmalloc(128);
			else
				xstrcat(dev_list, ",");
			if (mic_devices && (mic_devices[i] >= 0))
				xstrfmtcat(dev_list, "%d", mic_devices[i]);
			else
				xstrfmtcat(dev_list, "%d", i);
		}
		if (!dev_list && (first_match != -1)) {
			i = first_match;
			dev_list = xmalloc(128);
			if (mic_devices && (mic_devices[i] >= 0))
				xstrfmtcat(dev_list, "%d", mic_devices[i]);
			else
				xstrfmtcat(dev_list, "%d", i);
		}
	}
	if (dev_list) {
		env_array_overwrite(job_env_ptr,"OFFLOAD_DEVICES",
				    dev_list);
		xfree(dev_list);
	}
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void send_stepd(int fd)
{
	int i;

	safe_write(fd, &nb_available_files, sizeof(int));
	for (i = 0; i < nb_available_files; i++)
		safe_write(fd, &mic_devices[i], sizeof(int));
	return;

rwfail:	error("gres_plugin_send_stepd failed");
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void recv_stepd(int fd)
{
	int i;

	safe_read(fd, &nb_available_files, sizeof(int));
	if (nb_available_files > 0)
		mic_devices = xmalloc(sizeof(int) * nb_available_files);
	for (i = 0; i < nb_available_files; i++)
		safe_read(fd, &mic_devices[i], sizeof(int));
	return;

rwfail:	error("gres_plugin_recv_stepd failed");
}

extern int job_info(gres_job_state_t *job_gres_data, uint32_t node_inx,
		    enum gres_job_data_type data_type, void *data)
{
	return EINVAL;
}

extern int step_info(gres_step_state_t *step_gres_data, uint32_t node_inx,
		     enum gres_step_data_type data_type, void *data)
{
	return EINVAL;
}
