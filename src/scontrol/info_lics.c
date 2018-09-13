/*****************************************************************************\
 *  info_lics.c - licenses information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2013 SchedMD
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by David Bigagli david@schemd.com
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

#include "scontrol.h"

static void _print_license_info(const char *, license_info_msg_t *);
static slurm_license_info_t ** _license_sort(license_info_msg_t
					     *license_list);
static int _lic_cmp(const void *lic1, const void *lic2);

static int _lic_cmp(const void *lic1, const void *lic2)
{
	char *name1 = (*((slurm_license_info_t **)lic1))->name;
	char *name2 = (*((slurm_license_info_t **)lic2))->name;
	return xstrcmp(name1, name2);
}

/* license_sort()
 *
 * Sort the list of licenses by their name
 *
 */
static slurm_license_info_t ** _license_sort(license_info_msg_t
					     *license_list)
{
	slurm_license_info_t **lic_list_ptr = xmalloc(
		sizeof(slurm_license_info_t*) * license_list->num_lic);
	slurm_license_info_t *lic_ptr;
	int list_cnt;

	// Set tmp array of pointers to each license
	for (list_cnt = 0, lic_ptr = license_list->lic_array;
	     list_cnt < license_list->num_lic; list_cnt++, lic_ptr++) {
		lic_list_ptr[list_cnt] = lic_ptr;
	}

	qsort(lic_list_ptr, license_list->num_lic,
	      sizeof(slurm_license_info_t *), _lic_cmp);

	return lic_list_ptr;
}

/* scontrol_print_licenses()
 *
 * Retrieve and display the license information
 * from the controller
 *
 */
void
scontrol_print_licenses(const char *name)
{
	int cc;
	license_info_msg_t *msg;
	uint16_t show_flags;
	static time_t last_update;

	show_flags = 0;
	/* call the controller to get the meat
	 */
	cc = slurm_load_licenses(last_update, &msg, show_flags);
	if (cc != SLURM_SUCCESS) {
		/* Hosed, crap out.
		 */
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_license error");
		return;
	}

	last_update = time(NULL);
	/* print the info
	 */
	_print_license_info(name, msg);

	/* free at last
	 */
	slurm_free_license_info_msg(msg);

	return;
}

/* _print_license_info()
 *
 * Print the license information.
 */
static void _print_license_info(const char *name, license_info_msg_t *msg)
{
	int cc;
	slurm_license_info_t **sorted_lic = NULL;

	if (!msg->num_lic) {
		printf("No licenses configured in Slurm.\n");
		return;
	}

	sorted_lic = _license_sort(msg);

	for (cc = 0; cc < msg->num_lic; cc++) {
		if (name && xstrcmp((sorted_lic[cc])->name, name))
			continue;
		printf("LicenseName=%s%sTotal=%d Used=%u Free=%u Remote=%s\n",
		       (sorted_lic[cc])->name,
		       one_liner ? " " : "\n    ",
		       (sorted_lic[cc])->total,
		       (sorted_lic[cc])->in_use,
		       (sorted_lic[cc])->available,
		       (sorted_lic[cc])->remote ? "yes" : "no");
		if (name)
			break;
	}

	xfree(sorted_lic);
}
