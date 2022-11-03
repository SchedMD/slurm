/****************************************************************************\
 *  hdf5_api.c
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  Provide support for acct_gather_profile plugins based on HDF5 files.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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
\****************************************************************************/

#include <string.h>

#include "src/common/macros.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/interfaces/acct_gather_profile.h"

#include "hdf5_api.h"

extern void profile_fini(void)
{
	H5close(); /* make sure all H5 Objects are closed */

	return;
}

extern hid_t get_attribute_handle(hid_t parent, char *name)
{
	char buf[MAX_ATTR_NAME+1];
	int nattr, i, len;
	hid_t aid;
	H5O_info_t object_info;

	if (parent < 0) {
		debug3("PROFILE: parent is not HDF5 object");
		return -1;
	}

	H5Oget_info(parent, &object_info);
	nattr = object_info.num_attrs;
	for (i = 0; (nattr>0) && (i<nattr); i++) {
		aid = H5Aopen_by_idx(parent, ".", H5_INDEX_NAME, H5_ITER_INC,
				     i, H5P_DEFAULT, H5P_DEFAULT);
		// Get the name of the attribute.
		len = H5Aget_name(aid, MAX_ATTR_NAME, buf);
		if (len < MAX_ATTR_NAME) {
			if (xstrcmp(buf, name) == 0) {
				return aid;
			}
		}
		H5Aclose(aid);
	}
	debug3("PROFILE: failed to find HDF5 attribute=%s\n", name);

	return -1;
}

extern hid_t get_group(hid_t parent, const char *name)
{
	char buf[MAX_GROUP_NAME];
	hsize_t nobj;
	hid_t gid;
	int i, len;
	H5G_info_t group_info;

	if (parent < 0) {
		debug3("PROFILE: parent is not HDF5 object");
		return -1;
	}
	H5Gget_info(parent, &group_info);
	nobj = group_info.nlinks;
	for (i = 0; (nobj>0) && (i<nobj); i++) {
		// Get the name of the group.
		len = H5Lget_name_by_idx(parent, ".", H5_INDEX_NAME,
					 H5_ITER_INC, i, buf, MAX_GROUP_NAME,
					 H5P_DEFAULT);
		if ((len > 0) && (len < MAX_GROUP_NAME)) {
			if (xstrcmp(buf, name) == 0) {
				gid = H5Gopen(parent, name, H5P_DEFAULT);
				if (gid < 0)
					error("PROFILE: Failed to open %s",
					      name);
				return gid;
			}
		}
	}

	return -1;
}

extern hid_t make_group(hid_t parent, const char *name)
{
	hid_t gid = -1;

	if (parent < 0) {
		debug3("PROFILE: parent is not HDF5 object");
		return -1;
	}
	gid = get_group(parent, name);
	if (gid > 0)
		return gid;
	gid = H5Gcreate(parent, name, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (gid < 0) {
		debug3("PROFILE: failed to create HDF5 group=%s", name);
		return -1;
	}

	return gid;
}

extern void put_string_attribute(hid_t parent, char *name, char *value)
{
	hid_t   attr, space_attr, typ_attr;
	hsize_t dim_attr[1] = {1}; // Single dimension array of values

	if (!value)
		value = "";
	typ_attr = H5Tcopy(H5T_C_S1);
	if (typ_attr < 0) {
		debug3("PROFILE: failed to copy type for attribute %s", name);
		return;
	}
	H5Tset_size(typ_attr, strlen(value));
	H5Tset_strpad(typ_attr, H5T_STR_NULLTERM);
	space_attr = H5Screate_simple(1, dim_attr, NULL);
	if (space_attr < 0) {
		H5Tclose(typ_attr);
		debug3("PROFILE: failed to create space for attribute %s",
		       name);
		return;
	}
	attr = H5Acreate(parent, name, typ_attr, space_attr,
			 H5P_DEFAULT, H5P_DEFAULT);
	if (attr < 0) {
		H5Tclose(typ_attr);
		H5Sclose(space_attr);
		debug3("PROFILE: failed to create attribute %s", name);
		return;
	}
	if (H5Awrite(attr, typ_attr, value) < 0) {
		debug3("PROFILE: failed to write attribute %s", name);
		// Fall through to release resources
	}
	H5Sclose(space_attr);
	H5Tclose(typ_attr);
	H5Aclose(attr);

	return;
}

extern void put_int_attribute(hid_t parent, char *name, int value)
{
	hid_t   attr, space_attr;
	hsize_t dim_attr[1] = {1}; // Single dimension array of values
	space_attr  = H5Screate_simple(1, dim_attr, NULL);
	if (space_attr < 0) {
		debug3("PROFILE: failed to create space for attribute %s",
		       name);
		return;
	}
	attr = H5Acreate(parent, name, H5T_NATIVE_INT, space_attr,
			 H5P_DEFAULT, H5P_DEFAULT);
	if (attr < 0) {
		H5Sclose(space_attr);
		debug3("PROFILE: failed to create attribute %s", name);
		return;
	}
	if (H5Awrite(attr, H5T_NATIVE_INT, &value) < 0) {
		debug3("PROFILE: failed to write attribute %s", name);
		// Fall through to release resources
	}
	H5Sclose(space_attr);
	H5Aclose(attr);

	return;
}
