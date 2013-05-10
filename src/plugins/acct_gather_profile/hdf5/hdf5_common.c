/*****************************************************************************\
 *  hdf5_common.c
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  Copyright (C) 2013 SchedMD LLC
 *
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
 *****************************************************************************
 *
 * This program is expected to be launched by the SLURM epilog script for a
 * job on the controller node to merge node-step files into a job file.
 *
\*****************************************************************************/
#include "hdf5_common.h"

hid_t typTOD;

extern void profile_init()
{
	typTOD = H5Tcopy (H5T_C_S1);
	H5Tset_size (typTOD, TOD_LEN);
}

extern void profile_fini()
{
	H5Tclose(typTOD);
	H5close();
}

extern void put_string_attribute(hid_t parent, char* name, char* value)
{
	hid_t   attr, spaceAttr, typAttr;
	hsize_t dimAttr[1] = {1}; // Single dimension array of values

	typAttr = H5Tcopy(H5T_C_S1);
	if (typAttr < 0) {
		debug3("PROFILE: failed to copy type for attribute %s", name);
		return;
	}
	H5Tset_size(typAttr,strlen(value));
	H5Tset_strpad(typAttr,H5T_STR_NULLTERM);
	spaceAttr = H5Screate_simple(1, dimAttr, NULL);
	if (spaceAttr < 0) {
		H5Tclose(typAttr);
		debug3("PROFILE: failed to create space for attribute %s",
		       name);
		return;
	}
	attr = H5Acreate(parent, name, typAttr, spaceAttr,
			 H5P_DEFAULT, H5P_DEFAULT);
	if (attr < 0) {
		H5Tclose(typAttr);
		H5Sclose(spaceAttr);
		debug3("PROFILE: failed to create attribute %s", name);
		return;
	}
	if (H5Awrite(attr, typAttr, value) < 0) {
		debug3("PROFILE: failed to write attribute %s", name);
		// Fall through to release resources
	}
	H5Sclose(spaceAttr);
	H5Tclose(typAttr);
	H5Aclose(attr);

	return;
}

extern void put_int_attribute(hid_t parent, char* name, int value)
{
	hid_t   attr, spaceAttr;
	hsize_t dimAttr[1] = {1}; // Single dimension array of values
	spaceAttr  = H5Screate_simple(1, dimAttr, NULL);
	if (spaceAttr < 0) {
		debug3("PROFILE: failed to create space for attribute %s",
		       name);
		return;
	}
	attr = H5Acreate(parent, name, H5T_NATIVE_INT, spaceAttr,
			 H5P_DEFAULT, H5P_DEFAULT);
	if (attr < 0) {
		H5Sclose(spaceAttr);
		debug3("PROFILE: failed to create attribute %s", name);
		return;
	}
	if (H5Awrite(attr, H5T_NATIVE_INT, &value) < 0) {
		debug3("PROFILE: failed to write attribute %s", name);
		// Fall through to release resources
	}
	H5Sclose(spaceAttr);
	H5Aclose(attr);

	return;
}

extern void put_hdf5_data(hid_t parent, char* type, char* subtype,
			  char* group, void* data, int nItem)
{
	hid_t   idGroup, dtypMemory, dtypFile, idDataSpace, idDataSet;
	hsize_t dims[1];
	herr_t  ec;
	hdf5_api_ops_t* ops;
	ops = profile_factory(type);
	if (ops == NULL) {
		debug3("PROFILE: failed to create %s operations", type);
		return;
	}
	// Create the datatypes.
	if (strcmp(subtype,SUBDATA_SUMMARY) != 0)
		dtypMemory = (*(ops->create_memory_datatype))();
	else
		dtypMemory = (*(ops->create_s_memory_datatype))();
	if (dtypMemory < 0) {
		xfree(ops);
		debug3("PROFILE: failed to create %s memory datatype", type);
		return;
	}
	if (strcmp(subtype,SUBDATA_SUMMARY) != 0)
		dtypFile = (*(ops->create_file_datatype))();
	else
		dtypFile = (*(ops->create_s_file_datatype))();
	if (dtypFile < 0) {
		H5Tclose(dtypMemory);
		xfree(ops);
		debug3("PROFILE: failed to create %s file datatype", type);
		return;
	}

	dims[0] = nItem;
	idDataSpace = H5Screate_simple(1, dims, NULL);
	if (idDataSpace < 0) {
		H5Tclose(dtypFile);
		H5Tclose(dtypMemory);
		xfree(ops);
		debug3("PROFILE: failed to create %s space descriptor", type);
		return;
	}

	idGroup = H5Gcreate(parent, group, H5P_DEFAULT,
			    H5P_DEFAULT, H5P_DEFAULT);
	if (idGroup < 0) {
		H5Sclose(idDataSpace);
		H5Tclose(dtypFile);
		H5Tclose(dtypMemory);
		xfree(ops);
		debug3("PROFILE: failed to create %s group", group);
		return;
	}

	put_string_attribute(idGroup, ATTR_DATATYPE, type);
	put_string_attribute(idGroup, ATTR_SUBDATATYPE, subtype);

	idDataSet = H5Dcreate(idGroup, get_data_set_name(group), dtypFile,
			      idDataSpace, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	if (idDataSet < 0) {
		H5Gclose(idGroup);
		H5Sclose(idDataSpace);
		H5Tclose(dtypFile);
		H5Tclose(dtypMemory);
		xfree(ops);
		debug3("PROFILE: failed to create %s dataset", group);
		return;
	}

	ec = H5Dwrite(idDataSet, dtypMemory, H5S_ALL, H5S_ALL, H5P_DEFAULT,
		      data);
	if (ec < 0) {
		debug3("PROFILE: failed to create write task data");
		// Fall through to release resources
	}
	H5Dclose(idDataSet);
	H5Gclose(idGroup);
	H5Sclose(idDataSpace);
	H5Tclose(dtypFile);
	H5Tclose(dtypMemory);
	xfree(ops);


	return;
}

extern hid_t make_group(hid_t parent, char* name)
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

extern hid_t get_group(hid_t parent, char* name)
{
	char buf[MAX_GROUP_NAME];
	hsize_t nobj;
	hid_t gid;
	int i, len;

	if (parent < 0) {
		debug3("PROFILE: parent is not HDF5 object");
		return -1;
	}
	H5Gget_num_objs(parent, &nobj);
	for (i = 0; (nobj>0) && (i<nobj); i++) {
		// Get the name of the group.
		len = H5Gget_objname_by_idx(parent, i, buf, MAX_GROUP_NAME);
		if ((len > 0) && (len < MAX_GROUP_NAME)) {
			if (strcmp(buf,name) == 0) {
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
