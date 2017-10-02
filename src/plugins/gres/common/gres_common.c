/*****************************************************************************\
 *  gres_common.c - common functions for gres plugins
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
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

#include "gres_common.h"

#include "src/common/xstring.h"

extern int common_node_config_load(List gres_conf_list,
				   char *gres_name,
				   int **avail_devices,
				   int *num_avail_devices)
{
	int i, rc = SLURM_SUCCESS;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	int avail_device_inx = 0, loc_num_avail_devices = 0;
	int *loc_avail_devices = 0;

	xassert(gres_conf_list);
	xassert(avail_devices);

	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(iter))) {
		if ((gres_slurmd_conf->has_file != 1) ||
		    !gres_slurmd_conf->file ||
		    xstrcmp(gres_slurmd_conf->name, gres_name))
			continue;

		loc_num_avail_devices++;
	}
	list_iterator_destroy(iter);
	*num_avail_devices = -1;
	xfree(*avail_devices);	/* No-op if NULL */
	loc_avail_devices = xmalloc(sizeof(int) * loc_num_avail_devices);
	for (i = 0; i < loc_num_avail_devices; i++)
		loc_avail_devices[i] = -1;

	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(iter))) {
		char *bracket, *fname, *tmp_name;
		hostlist_t hl;
		if ((gres_slurmd_conf->has_file != 1) ||
		    !gres_slurmd_conf->file ||
		    xstrcmp(gres_slurmd_conf->name, gres_name))
			continue;
		/* Populate loc_avail_devices array with number
		 * at end of the file name */
		bracket = strrchr(gres_slurmd_conf->file, '[');
		if (bracket)
			tmp_name = xstrdup(bracket);
		else
			tmp_name = xstrdup(gres_slurmd_conf->file);
		hl = hostlist_create(tmp_name);
		xfree(tmp_name);
		if (!hl) {
			rc = EINVAL;
			break;
		}
		while ((fname = hostlist_shift(hl))) {
			if (avail_device_inx ==
			    loc_num_avail_devices) {
				loc_num_avail_devices++;
				xrealloc(loc_avail_devices,
					 sizeof(int) * loc_num_avail_devices);
				loc_avail_devices[avail_device_inx] = -1;
			}
			for (i = 0; fname[i]; i++) {
				if (!isdigit(fname[i]))
					continue;
				loc_avail_devices[avail_device_inx] =
					atoi(fname + i);
				break;
			}
			avail_device_inx++;
			free(fname);
		}
		hostlist_destroy(hl);
	}
	list_iterator_destroy(iter);

	if (rc == SLURM_SUCCESS) {
		for (i = 0; i < loc_num_avail_devices; i++)
			info("%s %d is device number %d",
			     gres_name, i, loc_avail_devices[i]);

		*num_avail_devices = loc_num_avail_devices;
		*avail_devices = loc_avail_devices;
	}

	return rc;
}
