/*****************************************************************************\
 *  info_layout.c - layout information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2015
 *  Written by Bull - Thomas Cadeau
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
#include "src/common/pack.h"

/*
 * scontrol_print_layout - print information about the supplied layout
 * IN layout_type - print information about the supplied layout 
 */
extern void
scontrol_print_layout (int argc, char **argv)
{
	int i = 0, tag_len = 0;
	char *tag = NULL, *val = NULL;
	char *layout_type = NULL, *entities = NULL, *type = NULL;
	uint32_t flags = 0;
	layout_info_msg_t *layout_info_ptr = NULL;

	while (i < argc) {
		tag = argv[i];
		tag_len = strlen(tag);
		val = strchr(argv[i], '=');

		if (val) {
			tag_len = val - argv[i];
			val++;
		} else if (argc > i+1) {
			val = argv[i+1];
			i++;
		} else {
			val = NULL;
		}
		if (xstrncasecmp(tag, "layouts", MAX(tag_len, 3)) == 0) {
			layout_type = val;
		} else if (xstrncasecmp(tag, "entity", MAX(tag_len, 3)) == 0) {
			entities = val;
		} else if (xstrncasecmp(tag, "type", MAX(tag_len, 3)) == 0) {
			type = val;
		} else if (xstrncasecmp(tag, "nolayout", MAX(tag_len, 4)) ==0) {
			flags |= LAYOUTS_DUMP_NOLAYOUT;
		} else {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf (stderr,
					 "invalid option for layouts: %s\n",
					 tag);
		}
		i++;
	}
	if (slurm_load_layout (layout_type, entities, type, flags,
			       &layout_info_ptr) == SLURM_SUCCESS) {
		slurm_print_layout_info ( stdout, layout_info_ptr, one_liner );
		slurm_free_layout_info_msg (layout_info_ptr);
	}

	return;
}
