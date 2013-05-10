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
\****************************************************************************/

#include "src/common/macros.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "hdf5_api.h"


// Static variables ok as add function are inside a lock.
static time_t seriesStart;
static hid_t typTOD;
static int i; // General index used in some macros.
static int moffset; // General variable used by insert macros

/*
 * Macro to insert a date string type into a compound memory type
 *
 * Parameters
 * 	p	parent (group) memory type
 * 	label	description of item
 * 	type	profile struct type
 * 	item    data item in type
 */
#define MEM_ADD_DATE_TIME(p, label, type, item)				\
	if(H5Tinsert(p, label, HOFFSET(type, item), typTOD) < 0) {	\
		debug3("PROFILE: failed insert into memory datatype");	\
		H5Tclose(p);						\
		return -1;						\
	}
/*
 * Macro to insert a date string type into a compound file type
 *
 * Parameters
 * 	p	parent (group) file type
 * 	label	description of item
 * 	offset  offset into record
 */
#define FILE_ADD_DATE_TIME(p, label, offset) 				\
	if(H5Tinsert(p, label, offset, typTOD) < 0) {			\
		debug3("PROFILE: failed insert into file datatype");	\
		H5Tclose(p);						\
		return -1;						\
	}

/*
 * Macro to insert an uint64 into a compound memory type
 *
 * Parameters
 * 	p	parent (group) memory type
 * 	label	description of item
 * 	type	profile struct type
 * 	item    data item in type
 */
#define MEM_ADD_UINT64(p, label, type, item)				\
	if(H5Tinsert(p, label, HOFFSET(type, item), H5T_NATIVE_UINT64) < 0) { \
		debug3("PROFILE: failed insert64 into memory datatype"); \
		H5Tclose(p);						\
		return -1;						\
	}
/*
 * Macro to insert a uint64 into a compound file type
 *
 * Parameters
 * 	p	parent (group) file type
 * 	label	description of item
 */
#define FILE_ADD_UINT64(p, label)					\
	if(H5Tinsert(p, label, moffset, H5T_NATIVE_UINT64) < 0) {	\
		debug3("PROFILE: failed insert64 into file datatype");	\
		H5Tclose(p);						\
		return -1;						\
	}								\
	moffset += 8;

/*
 * Macro to insert a double into a compound memory type
 *
 * Parameters
 * 	p	parent (group) memory type
 * 	label	description of item
 * 	type	profile struct type
 * 	item    data item in type
 */
#define MEM_ADD_DBL(p, label, type, item)				\
	if(H5Tinsert(p, label, HOFFSET(type, item), H5T_NATIVE_DOUBLE) < 0) { \
		debug3("PROFILE: failed insertdbl into memory datatype"); \
		H5Tclose(p);						\
		return -1;						\
	}
/*
 * Macro to insert a double into a compound file type
 *
 * Parameters
 * 	p	parent (group) file type
 * 	label	description of item
 */
#define FILE_ADD_DBL(p, label)						\
	if(H5Tinsert(p, label, moffset, H5T_NATIVE_DOUBLE) < 0) {	\
		debug3("PROFILE: failed insertdbl into file datatype");	\
		H5Tclose(p);						\
		return -1;						\
	}								\
	moffset += 8;

/*
 * Macro to increment a sample in a difference series
 * -- Difference means each sample represents counts for only that interval
 *	(assumes consistent naming convention)
 *
 *
 * Parameters
 * 	tot	total pointer
 * 	smp     sample pointer
 * 	var	variable name in sample
 * 	count	number of items in series
 */
#define INCR_DIF_SAMPLE(tot, smp, var, count)			\
	for (i=0; i<count; i++) {				\
		if (i == 0) {					\
			total->var.min = smp[i].var;		\
		}						\
		tot->var.total += smp[i].var;			\
		tot->var.min = MIN(smp[i].var,tot->var.min);	\
		tot->var.max = MAX(smp[i].var,tot->var.max);	\
	}							\
	tot->var.ave = tot->var.total / count;

/*
 * Macro to increment a sample in a running total
 * -- Running total means first sample is initial conditions
 *	(assumes consistent naming convention)
 *
 *
 * Parameters
 * 	tot	total pointer
 * 	smp     sample pointer
 * 	var	variable name in sample
 * 	count	number of items in series
 */
#define INCR_RT_SAMPLE(tot, smp, var, count)			\
	for (i=1; i<count; i++) {				\
		if (i == 1) {					\
			total->var.min = smp[i].var;		\
		}						\
		tot->var.total += smp[i].var;			\
		tot->var.min = MIN(smp[i].var,tot->var.min);	\
		tot->var.max = MAX(smp[i].var,tot->var.max);	\
	}							\
	tot->var.ave = tot->var.total / count;

/* Macro to put an int min,ave,max,total for a variable to extract file
 *
 * Parameters
 * 	fOt	file descriptor
 * 	var	variable name
 * 	prf	prefix for series (usually ','
 */
#define PUT_UINT_SUM(fOt, var, prfx)			\
	fprintf(fOt,"%s%ld,%ld,%ld,%ld",prfx,		\
		var.min,var.ave,var.max,var.total);
/* Macro to put an int min,ave,max,total for a variable to extract file
 *
 * Parameters
 * 	fOt	file descriptor
 * 	var	variable name
 * 	prf	prefix for series (usually ','
 */
#define PUT_DBL_SUM(fOt, var, prfx)			\
	fprintf(fOt,"%s%.3f,%.3f,%.3f,%.3f",prfx,	\
		var.min,var.ave,var.max,var.total);


/* ============================================================================
 * Common support functions
 ===========================================================================*/

hdf5_api_ops_t* profile_factory(char* type)
{
	if (strcmp(type, PROFILE_ENERGY_DATA) == 0) {
		return energy_profile_factory();
	} else if (strcmp(type, PROFILE_IO_DATA) == 0) {
		return io_profile_factory();
	} else if (strcmp(type, PROFILE_NETWORK_DATA) == 0) {
		return network_profile_factory();
	} else if (strcmp(type, PROFILE_TASK_DATA) == 0) {
		return task_profile_factory();
	} else {
		error("PROFILE: PROFILE: %s is an invalid data type", type);
		return NULL;
	}
}

void profile_init()
{
	typTOD = H5Tcopy (H5T_C_S1);
	H5Tset_size (typTOD, TOD_LEN); /* create string of length TOD_LEN */

	return;
}

void profile_fini()
{
	H5Tclose(typTOD);
	H5close(); // make sure all H5 Objects are closed

	return;
}

char* get_data_set_name(char* type)
{
	static char  dset_name[MAX_DATASET_NAME+1];
	dset_name[0] = '\0';
	sprintf(dset_name, "%s Data", type);

	return dset_name;
}


void hdf5_obj_info(hid_t group, char* namGroup)
{

	char* hdf5TypNam[] = {"H5G_LINK   ",
			      "H5G_GROUP  ",
			      "H5G_DATASET",
			      "H5G_TYPE   "};

	char buf[MAX_GROUP_NAME+1];
	hsize_t nobj, nattr;
	hid_t aid;
	int i, len, typ;

	if (group < 0) {
		info("PROFILE: Group is not HDF5 object");
		return;
	}
	H5Gget_num_objs(group, &nobj);
	nattr = H5Aget_num_attrs(group);
	info("PROFILE group: %s NumObject=%d NumAttributes=%d",
	     namGroup, (int) nobj, (int) nattr);
	for (i = 0; (nobj>0) && (i<nobj); i++) {
		typ = H5Gget_objtype_by_idx(group, i);
		len = H5Gget_objname_by_idx(group, i, buf, MAX_GROUP_NAME);
		if ((len > 0) && (len < MAX_GROUP_NAME)) {
			info("PROFILE: Obj=%d Type=%s Name=%s",
			     i,hdf5TypNam[typ], buf);
		} else {
			info("PROFILE: Obj=%d Type=%s Name=%s (is truncated)",
			     i,hdf5TypNam[typ], buf);
		}
	}
	for (i = 0; (nattr>0) && (i<nattr); i++) {
		aid = H5Aopen_idx(group, (unsigned int)i );
		// Get the name of the attribute.
		len = H5Aget_name(aid, MAX_ATTR_NAME, buf);
		if (len < MAX_ATTR_NAME) {
			info("PROFILE: Attr=%d Name=%s", i,buf);
		} else {
			info("PROFILE: Attr=%d Name=%s (is truncated)", i,buf);
		}
		H5Aclose(aid);
	}

	return;
}

hid_t get_attribute_handle(hid_t parent, char* name)
{

	char buf[MAX_ATTR_NAME+1];
	int nattr, i, len;
	hid_t aid;

	if (parent < 0) {
		debug3("PROFILE: parent is not HDF5 object");
		return -1;
	}
	nattr = H5Aget_num_attrs(parent);
	for (i = 0; (nattr>0) && (i<nattr); i++) {
		aid = H5Aopen_idx(parent, (unsigned int)i );

		// Get the name of the attribute.
		len = H5Aget_name(aid, MAX_ATTR_NAME, buf);
		if (len < MAX_ATTR_NAME) {
			if (strcmp(buf,name) == 0) {
				return aid;
			}
		}
		H5Aclose(aid);
	}
	debug3("PROFILE: failed to find HDF5 attribute=%s\n", name);

	return -1;
}

hid_t get_group(hid_t parent, char* name)
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

hid_t make_group(hid_t parent, char* name)
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

void put_string_attribute(hid_t parent, char* name, char* value)
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

char* get_string_attribute(hid_t parent, char* name)
{
	char* value = NULL;

	hid_t   attr, type;
	size_t  size;

	attr = get_attribute_handle(parent, name);
	if (attr < 0) {
		debug3("PROFILE: Attribute=%s does not exist",name);
		return NULL;
	}
	type  = H5Aget_type(attr);
	if (H5Tget_class(type) != H5T_STRING) {
		H5Aclose(attr);
		debug3("PROFILE: Attribute=%s is not a string",name);
		return NULL;
	}
	size = H5Tget_size(type);
	value = xmalloc(size+1);
	if (value == NULL) {
		H5Tclose(type);
		H5Aclose(attr);
		debug3("PROFILE: failed to malloc %d bytes for attribute=%s",
		       (int) size,
		       name);
		return NULL;
	}
	if (H5Aread(attr, type, value) < 0) {
		xfree(value);
		H5Tclose(type);
		H5Aclose(attr);
		debug3("PROFILE: failed to read attribute=%s",name);
		return NULL;
	}
	H5Tclose(type);
	H5Aclose(attr);

	return value;
}

void put_int_attribute(hid_t parent, char* name, int value)
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

int get_int_attribute(hid_t parent, char* name)
{
	int value = 0;

	hid_t   attr;
	attr = get_attribute_handle(parent, name);
	if (attr < 0) {
		debug3("PROFILE: Attribute=%s does not exist, returning",name);
		return value;
	}
	if (H5Aread(attr, H5T_NATIVE_INT, &value) < 0) {
		debug3("PROFILE: failed to read attribute=%s, returning",name);
	}
	H5Aclose(attr);

	return value;
}

void* get_hdf5_data(hid_t parent, char* type, char* namGroup, int* sizeData)
{
	void*   data = NULL;

	hid_t   idDataSet, dtypMemory;
	hsize_t szDset;
	herr_t  ec;
	char* subtype = NULL;

	hdf5_api_ops_t* ops = profile_factory(type);

	if (ops == NULL) {
		debug3("PROFILE: failed to create %s operations", type);
		return NULL;
	}
	subtype = get_string_attribute(parent, ATTR_SUBDATATYPE);
	if (subtype < 0) {
		xfree(ops);
		debug3("PROFILE: failed to get %s attribute",
		       ATTR_SUBDATATYPE);
		return NULL;
	}
	idDataSet = H5Dopen(parent, get_data_set_name(namGroup), H5P_DEFAULT);
	if (idDataSet < 0) {
		xfree(subtype);
		xfree(ops);
		debug3("PROFILE: failed to open %s Data Set", type);
		return NULL;
	}
	if (strcmp(subtype, SUBDATA_SUMMARY))
		dtypMemory = (*(ops->create_memory_datatype))();
	else
		dtypMemory = (*(ops->create_s_memory_datatype))();
	xfree(subtype);
	if (dtypMemory < 0) {
		H5Dclose(idDataSet);
		xfree(ops);
		debug3("PROFILE: failed to create %s memory datatype", type);
		return NULL;
	}
	szDset = H5Dget_storage_size(idDataSet);
	*sizeData = (int) szDset;
	if (szDset == 0) {
		H5Tclose(dtypMemory);
		H5Dclose(idDataSet);
		xfree(ops);
		debug3("PROFILE: %s data set is empty", type);
		return NULL;
	}
	data = xmalloc(szDset);
	if (data == NULL) {
		H5Tclose(dtypMemory);
		H5Dclose(idDataSet);
		xfree(ops);
		debug3("PROFILE: failed to get memory for %s data set", type);
		return NULL;
	}
	ec = H5Dread(idDataSet, dtypMemory, H5S_ALL, H5S_ALL, H5P_DEFAULT,
		     data);
	if (ec < 0) {
		H5Tclose(dtypMemory);
		H5Dclose(idDataSet);
		xfree(data);
		xfree(ops);
		debug3("PROFILE: failed to read %s data", type);
		return NULL;
	}
	H5Tclose(dtypMemory);
	H5Dclose(idDataSet);
	xfree(ops);

	return data;
}

void put_hdf5_data(hid_t parent, char* type, char* subtype,
		   char* group, void* data, int nItem)
{
	hid_t   idGroup, dtypMemory, dtypFile, idDataSpace, idDataSet;
	hsize_t dims[1];
	herr_t  ec;
	hdf5_api_ops_t* ops = profile_factory(type);

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


// ============================================================================
// Routines supporting Energy Data type
// ============================================================================

int energy_dataset_size()
{
	return sizeof(profile_energy_t);
}

hid_t energy_create_memory_datatype()
{
	hid_t   mtypEnergy = H5Tcreate(H5T_COMPOUND, sizeof(profile_energy_t));
	if (mtypEnergy < 0) {
		debug3("PROFILE: failed to create Energy memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtypEnergy,"Date Time", profile_energy_t,tod);
	MEM_ADD_UINT64(mtypEnergy, "Time", profile_energy_t, time);
	MEM_ADD_UINT64(mtypEnergy, "Power", profile_energy_t, power);
	MEM_ADD_UINT64(mtypEnergy, "CPU Frequency", profile_energy_t, cpu_freq);

	return mtypEnergy;
}

hid_t energy_create_file_datatype()
{
	hid_t   ftypEnergy = H5Tcreate(H5T_COMPOUND,(TOD_LEN+3*8));
	if (ftypEnergy < 0) {
		debug3("PROFILE: failed to create Energy file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftypEnergy, "Date Time", 0);
	FILE_ADD_UINT64(ftypEnergy, "Time");
	FILE_ADD_UINT64(ftypEnergy, "Power");
	FILE_ADD_UINT64(ftypEnergy, "CPU Frequency");

	return ftypEnergy;
}

hid_t energy_s_create_memory_datatype()
{
	hid_t   mtypEnergy = H5Tcreate(H5T_COMPOUND,
				       sizeof(profile_energy_s_t));
	if (mtypEnergy < 0) {
		debug3("PROFILE: failed to create Energy_s memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtypEnergy,"Start Time",
			  profile_energy_s_t, start_time);
	MEM_ADD_UINT64(mtypEnergy, "Elapsed Time",
		       profile_energy_s_t, elapsed_time);
	MEM_ADD_UINT64(mtypEnergy,"Min Power", profile_energy_s_t, power.min);
	MEM_ADD_UINT64(mtypEnergy,"Ave Power", profile_energy_s_t, power.ave);
	MEM_ADD_UINT64(mtypEnergy,"Max Power", profile_energy_s_t, power.max);
	MEM_ADD_UINT64(mtypEnergy,"Total Power",
		       profile_energy_s_t, power.total);
	MEM_ADD_UINT64(mtypEnergy,"Min CPU Frequency", profile_energy_s_t,
		       cpu_freq.min);
	MEM_ADD_UINT64(mtypEnergy,"Ave CPU Frequency", profile_energy_s_t,
		       cpu_freq.ave);
	MEM_ADD_UINT64(mtypEnergy,"Max CPU Frequency", profile_energy_s_t,
		       cpu_freq.max);
	MEM_ADD_UINT64(mtypEnergy,"Total CPU Frequency", profile_energy_s_t,
		       cpu_freq.total);

	return mtypEnergy;
}

hid_t energy_s_create_file_datatype()
{
	hid_t   ftypEnergy = H5Tcreate(H5T_COMPOUND, (TOD_LEN+9*8));
	if (ftypEnergy < 0) {
		debug3("PROFILE: failed to create Energy_s file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftypEnergy, "Start Time", 0);
	FILE_ADD_UINT64(ftypEnergy, "Elapsed Time");
	FILE_ADD_UINT64(ftypEnergy, "Min Power");
	FILE_ADD_UINT64(ftypEnergy, "Ave Power");
	FILE_ADD_UINT64(ftypEnergy, "Max Power");
	FILE_ADD_UINT64(ftypEnergy, "Total Power");
	FILE_ADD_UINT64(ftypEnergy, "Min CPU Frequency");
	FILE_ADD_UINT64(ftypEnergy, "Ave CPU Frequency");
	FILE_ADD_UINT64(ftypEnergy, "Max CPU Frequency");
	FILE_ADD_UINT64(ftypEnergy, "Total CPU Frequency");

	return ftypEnergy;
}

void* energy_init_job_series(int nSamples)
{
	profile_energy_t*  energyData;

	energyData = xmalloc(nSamples * sizeof(profile_energy_t));
	if (energyData == NULL) {
		debug3("PROFILE: failed to get memory for energy data");
		return NULL;
	}
	return (void*) energyData;
}

void energy_merge_step_series(hid_t group, void* prior, void* cur, void* buf)
{
//	This is a difference series
	profile_energy_t* prfCur = (profile_energy_t*) cur;
	profile_energy_t* prfBuf = (profile_energy_t*) buf;
	struct tm *ts = localtime(&prfCur->time);
	strftime(prfBuf->tod, TOD_LEN, TOD_FMT, ts);
	if (prior == NULL) {
		// First sample.
		seriesStart = prfCur->time;
		prfBuf->time = 0;

	} else {
		prfBuf->time = prfCur->time - seriesStart;
	}
	prfBuf->power = prfCur->power;
	prfBuf->cpu_freq = prfCur->cpu_freq;
	return;
}

void* energy_series_total(int nSamples, void* data)
{
	profile_energy_t* energyData;
	profile_energy_s_t* total;
	if (nSamples < 1)
		return NULL;
	energyData = (profile_energy_t*) data;
	total = xmalloc(sizeof(profile_energy_s_t));
	if (total == NULL) {
		error("PROFILE: Out of memory getting energy total");
		return NULL;
	}
	// Assuming energy series are a difference series
	strcpy(total->start_time, energyData[0].tod);
	total->elapsed_time = energyData[nSamples-1].time;
	INCR_DIF_SAMPLE(total, energyData, power, nSamples);
	INCR_DIF_SAMPLE(total, energyData, cpu_freq, nSamples);
	return total;
}

void energy_extract_series(FILE* fOt, bool putHeader, int job, int step,
			   char* node, char* series, void* data, int sizeData)
{

	int nItems, ix;
	profile_energy_t* energyData = (profile_energy_t*) data;
	if (putHeader) {
		fprintf(fOt, "Job,Step,Node,Series,Date_Time,Elapsed_Time,"
			"Power,CPU_Frequency\n");
	}
	nItems = sizeData / sizeof(profile_energy_t);
	for (ix=0; ix < nItems; ix++) {
		fprintf(fOt, "%d,%d,%s,%s,%s,%ld,%ld,%ld\n", job, step, node,
			series, energyData[ix].tod, energyData[ix].time,
			energyData[ix].power, energyData[ix].cpu_freq);
	}
	return;
}

void energy_extract_total(FILE* fOt, bool putHeader, int job, int step,
			  char* node, char* series, void* data, int sizeData)
{
	profile_energy_s_t* energyData = (profile_energy_s_t*) data;
	if (putHeader) {
		fprintf(fOt, "Job,Step,Node,Series,Start_Time,Elapsed_Time,"
			"Min_Power,Ave_Power,Max_Power,Total_Power,"
			"Min_CPU Frequency,Ave_CPU Frequency,"
			"Max_CPU Frequency,Total_CPU Frequency\n");
	}
	fprintf(fOt, "%d,%d,%s,%s,%s,%ld", job, step, node, series,
		energyData->start_time, energyData->elapsed_time);
	PUT_UINT_SUM(fOt, energyData->power, ",");
	PUT_UINT_SUM(fOt, energyData->cpu_freq, ",");
	fprintf(fOt, "\n");
	return;
}

hdf5_api_ops_t* energy_profile_factory()
{
	hdf5_api_ops_t* ops = xmalloc(sizeof(hdf5_api_ops_t));
	ops->dataset_size = &energy_dataset_size;
	ops->create_memory_datatype = &energy_create_memory_datatype;
	ops->create_file_datatype = &energy_create_file_datatype;
	ops->create_s_memory_datatype = &energy_s_create_memory_datatype;
	ops->create_s_file_datatype = &energy_s_create_file_datatype;
	ops->init_job_series = &energy_init_job_series;
	ops->merge_step_series = &energy_merge_step_series;
	ops->series_total = &energy_series_total;
	ops->extract_series = &energy_extract_series;
	ops->extract_total = &energy_extract_total;
	return ops;
}


// ============================================================================
// Routines supporting I/O Data type
// ============================================================================

int io_dataset_size()
{
	return sizeof(profile_io_t);
}

hid_t io_create_memory_datatype(void)
{
	hid_t   mtypIO = -1;

	mtypIO = H5Tcreate(H5T_COMPOUND, sizeof(profile_io_t));
	if (mtypIO < 0) {
		debug3("PROFILE: failed to create IO memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtypIO,"Date Time", profile_io_t,tod);
	MEM_ADD_UINT64(mtypIO, "Time", profile_io_t, time);
	MEM_ADD_UINT64(mtypIO, "Reads", profile_io_t, reads);
	MEM_ADD_DBL(mtypIO, "Megabytes Read", profile_io_t, read_size);
	MEM_ADD_UINT64(mtypIO, "Writes", profile_io_t, writes);
	MEM_ADD_DBL(mtypIO, "Megabytes Write", profile_io_t,write_size);
	return mtypIO;
}

hid_t io_create_file_datatype(void)
{
	hid_t   ftypIO = -1;

	ftypIO = H5Tcreate(H5T_COMPOUND,TOD_LEN+5*8);
	if (ftypIO < 0) {
		debug3("PROFILE: failed to create IO file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftypIO, "Date Time", 0);
	FILE_ADD_UINT64(ftypIO, "Time");
	FILE_ADD_UINT64(ftypIO, "Reads");
	FILE_ADD_DBL(ftypIO, "Megabytes Read");
	FILE_ADD_UINT64(ftypIO, "Writes");
	FILE_ADD_DBL(ftypIO, "Megabytes Write");

	return ftypIO;
}

hid_t io_s_create_memory_datatype(void)
{
	hid_t   mtypIO = -1;

	mtypIO = H5Tcreate(H5T_COMPOUND, sizeof(profile_io_s_t));
	if (mtypIO < 0) {
		debug3("PROFILE: failed to create IO memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtypIO,"Start Time", profile_io_s_t, start_time);
	MEM_ADD_UINT64(mtypIO, "Elapsed Time", profile_io_s_t, elapsed_time);
	MEM_ADD_UINT64(mtypIO, "Min Reads", profile_io_s_t, reads.min);
	MEM_ADD_UINT64(mtypIO, "Ave Reads", profile_io_s_t, reads.ave);
	MEM_ADD_UINT64(mtypIO, "Max Reads", profile_io_s_t, reads.max);
	MEM_ADD_UINT64(mtypIO, "Total Reads", profile_io_s_t, reads.total);
	MEM_ADD_DBL(mtypIO, "Min Read Megabytes",
		    profile_io_s_t, read_size.min);
	MEM_ADD_DBL(mtypIO, "Ave Read Megabytes",
		    profile_io_s_t, read_size.ave);
	MEM_ADD_DBL(mtypIO, "Max Read Megabytes",
		    profile_io_s_t, read_size.max);
	MEM_ADD_DBL(mtypIO, "Total Read Megabytes", profile_io_s_t,
		    read_size.total);
	MEM_ADD_UINT64(mtypIO, "Min Writes", profile_io_s_t, writes.min);
	MEM_ADD_UINT64(mtypIO, "Ave Writes", profile_io_s_t, writes.ave);
	MEM_ADD_UINT64(mtypIO, "Max Writes", profile_io_s_t, writes.max);
	MEM_ADD_UINT64(mtypIO, "Total Writes", profile_io_s_t, writes.total);
	MEM_ADD_DBL(mtypIO, "Min Write Megabytes", profile_io_s_t,
		    write_size.min);
	MEM_ADD_DBL(mtypIO, "Ave Write Megabytes", profile_io_s_t,
		    write_size.ave);
	MEM_ADD_DBL(mtypIO, "Max Write Megabytes", profile_io_s_t,
		    write_size.max);
	MEM_ADD_DBL(mtypIO, "Total Write Megabytes", profile_io_s_t,
		    write_size.total);

	return mtypIO;
}

hid_t io_s_create_file_datatype(void)
{
	hid_t   ftypIO = -1;

	ftypIO = H5Tcreate(H5T_COMPOUND,TOD_LEN+17*8);
	if (ftypIO < 0) {
		debug3("PROFILE: failed to create IO file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftypIO,"Start Time", 0);
	FILE_ADD_UINT64(ftypIO, "Elapsed Time");
	FILE_ADD_UINT64(ftypIO, "Min Reads");
	FILE_ADD_UINT64(ftypIO, "Ave Reads");
	FILE_ADD_UINT64(ftypIO, "Max Reads");
	FILE_ADD_UINT64(ftypIO, "Total Reads");
	FILE_ADD_DBL(ftypIO, "Min Read Megabytes");
	FILE_ADD_DBL(ftypIO, "Ave Read Megabytes");
	FILE_ADD_DBL(ftypIO, "Max Read Megabytes");
	FILE_ADD_DBL(ftypIO, "Total Read Megabytes");
	FILE_ADD_UINT64(ftypIO, "Min Writes");
	FILE_ADD_UINT64(ftypIO, "Ave Writes");
	FILE_ADD_UINT64(ftypIO, "Max Writes");
	FILE_ADD_UINT64(ftypIO, "Total Writes");
	FILE_ADD_DBL(ftypIO, "Min Write Megabytes");
	FILE_ADD_DBL(ftypIO, "Ave Write Megabytes");
	FILE_ADD_DBL(ftypIO, "Max Write Megabytes");
	FILE_ADD_DBL(ftypIO, "Total Write Megabytes");

	return ftypIO;
}

void* io_init_job_series(int nSamples)
{
	profile_io_t*  ioData;
	ioData = xmalloc(nSamples * sizeof(profile_io_t));
	if (ioData == NULL) {
		debug3("PROFILE: failed to get memory for combined io data");
		return NULL;
	}
	return (void*) ioData;
}

void io_merge_step_series(hid_t group, void* prior, void* cur, void* buf)
{
	// This is a difference series
	profile_io_t* prfCur = (profile_io_t*) cur;
	profile_io_t* prfBuf = (profile_io_t*) buf;
	struct tm *ts = localtime(&prfCur->time);
	strftime(prfBuf->tod, TOD_LEN, TOD_FMT, ts);
	if (prior == NULL) {
		// First sample.
		seriesStart = prfCur->time;
		prfBuf->time = 0;
	} else {
		prfBuf->time = prfCur->time - seriesStart;
	}
	prfBuf->reads = prfCur->reads;
	prfBuf->writes = prfCur->writes;
	prfBuf->read_size = prfCur->read_size;
	prfBuf->write_size = prfCur->write_size;
	return;
}

void* io_series_total(int nSamples, void* data)
{
	profile_io_t* ioData;
	profile_io_s_t* total;
	if (nSamples < 1)
		return NULL;
	ioData = (profile_io_t*) data;
	total = xmalloc(sizeof(profile_io_s_t));
	if (total == NULL) {
		error("PROFILE: Out of memory getting I/O total");
		return NULL;
	}
	// Assuming io series are a running total, and the first
	// sample just sets the initial conditions
	strcpy(total->start_time, ioData[0].tod);
	total->elapsed_time = ioData[nSamples-1].time;
	INCR_DIF_SAMPLE(total, ioData, reads, nSamples);
	INCR_DIF_SAMPLE(total, ioData, read_size, nSamples);
	INCR_DIF_SAMPLE(total, ioData, writes, nSamples);
	INCR_DIF_SAMPLE(total, ioData, write_size, nSamples);
	return total;
}

void io_extract_series(FILE* fOt, bool putHeader, int job, int step,
		       char* node, char* series, void* data, int sizeData)
{
	int nItems, ix;
	profile_io_t* ioData = (profile_io_t*) data;
	if (putHeader) {
		fprintf(fOt,"Job,Step,Node,Series,Date_Time,Elapsed_time,"
			"Reads,Read Megabytes,Writes,Write Megabytes\n");
	}
	nItems = sizeData / sizeof(profile_io_t);
	for (ix=0; ix < nItems; ix++) {
		fprintf(fOt,"%d,%d,%s,%s,%s,%ld,%ld,%.3f,%ld,%.3f\n",
			job,step,node,series,
			ioData[ix].tod, ioData[ix].time,
			ioData[ix].reads, ioData[ix].read_size,
			ioData[ix].writes, ioData[ix].write_size);
	}
	return;
}

void io_extract_total(FILE* fOt, bool putHeader, int job, int step,
		      char* node, char* series, void* data, int sizeData)
{
	profile_io_s_t* ioData = (profile_io_s_t*) data;
	if (putHeader) {
		fprintf(fOt,"Job,Step,Node,Series,Start_Time,Elapsed_time,"
			"Min_Reads,Ave_Reads,Max_Reads,Total_Reads,"
			"Min_Read_Megabytes,Ave_Read_Megabytes,"
			"Max_Read_Megabytes,Total_Read_Megabytes,"
			"Min_Writes,Ave_Writes,Max_Writes,Total_Writes,"
			"Min_Write_Megabytes,Ave_Write_Megabytes,"
			"Max_Write_Megabytes,Total_Write_Megabytes\n");
	}
	fprintf(fOt,"%d,%d,%s,%s,%s,%ld", job,step,node,series,
		ioData->start_time, ioData->elapsed_time);
	PUT_UINT_SUM(fOt, ioData->reads,",");
	PUT_DBL_SUM(fOt, ioData->read_size,",");
	PUT_UINT_SUM(fOt, ioData->writes,",");
	PUT_DBL_SUM(fOt, ioData->write_size,",");
	fprintf(fOt,"\n");
	return;
}

hdf5_api_ops_t* io_profile_factory()
{
	hdf5_api_ops_t* ops = xmalloc(sizeof(hdf5_api_ops_t));
	ops->dataset_size = &io_dataset_size;
	ops->create_memory_datatype = &io_create_memory_datatype;
	ops->create_file_datatype = &io_create_file_datatype;
	ops->create_s_memory_datatype = &io_s_create_memory_datatype;
	ops->create_s_file_datatype = &io_s_create_file_datatype;
	ops->init_job_series = &io_init_job_series;
	ops->merge_step_series = &io_merge_step_series;
	ops->series_total = &io_series_total;
	ops->extract_series = &io_extract_series;
	ops->extract_total = &io_extract_total;
	return ops;
}


// ============================================================================
// Routines supporting Network Data type
// ============================================================================

int network_dataset_size()
{
	return sizeof(profile_network_t);
}

hid_t network_create_memory_datatype(void)
{
	hid_t   mtypNetwork = H5Tcreate(H5T_COMPOUND,
					sizeof(profile_network_t));
	if (mtypNetwork < 0) {
		debug3("PROFILE: failed to create Network memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtypNetwork,"Date Time", profile_network_t,tod);
	MEM_ADD_UINT64(mtypNetwork, "Time", profile_network_t, time);
	MEM_ADD_UINT64(mtypNetwork, "Packets In",
		       profile_network_t, packets_in);
	MEM_ADD_DBL(mtypNetwork, "Megabytes In", profile_network_t, size_in);
	MEM_ADD_UINT64(mtypNetwork, "Packets Out",
		       profile_network_t, packets_out);
	MEM_ADD_DBL(mtypNetwork, "Megabytes Out", profile_network_t,size_out);

	return mtypNetwork;
}

hid_t network_create_file_datatype(void)
{
	hid_t   ftypNetwork = H5Tcreate(H5T_COMPOUND, TOD_LEN+5*8);
	if (ftypNetwork < 0) {
		debug3("PROFILE: failed to create Network file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftypNetwork, "Date Time", 0);
	FILE_ADD_UINT64(ftypNetwork, "Time");
	FILE_ADD_UINT64(ftypNetwork, "Packets In");
	FILE_ADD_DBL(ftypNetwork, "Megabytes In");
	FILE_ADD_UINT64(ftypNetwork, "Packets Out");
	FILE_ADD_DBL(ftypNetwork, "Megabytes Out");

	return ftypNetwork;
}

hid_t network_s_create_memory_datatype(void)
{
	hid_t   mtypNetwork = -1;

	mtypNetwork = H5Tcreate(H5T_COMPOUND, sizeof(profile_network_s_t));
	if (mtypNetwork < 0) {
		debug3("PROFILE: failed to create Network memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtypNetwork,"Start Time", profile_network_s_t,
			  start_time);
	MEM_ADD_UINT64(mtypNetwork, "Elapsed Time", profile_network_s_t,
		       elapsed_time);
	MEM_ADD_UINT64(mtypNetwork, "Min Packets In", profile_network_s_t,
		       packets_in.min);
	MEM_ADD_UINT64(mtypNetwork, "Ave Packets In", profile_network_s_t,
		       packets_in.ave);
	MEM_ADD_UINT64(mtypNetwork, "Max Packets In", profile_network_s_t,
		       packets_in.max);
	MEM_ADD_UINT64(mtypNetwork, "Total Packets In", profile_network_s_t,
		       packets_in.total);
	MEM_ADD_DBL(mtypNetwork, "Min Megabytes In", profile_network_s_t,
		    size_in.min);
	MEM_ADD_DBL(mtypNetwork, "Ave Megabytes In", profile_network_s_t,
		    size_in.ave);
	MEM_ADD_DBL(mtypNetwork, "Max Megabytes In", profile_network_s_t,
		    size_in.max);
	MEM_ADD_DBL(mtypNetwork, "Total Megabytes In", profile_network_s_t,
		    size_in.total);
	MEM_ADD_UINT64(mtypNetwork, "Min Packets Out", profile_network_s_t,
		       packets_out.min);
	MEM_ADD_UINT64(mtypNetwork, "Ave Packets Out", profile_network_s_t,
		       packets_out.ave);
	MEM_ADD_UINT64(mtypNetwork, "Max Packets Out", profile_network_s_t,
		       packets_out.max);
	MEM_ADD_UINT64(mtypNetwork, "Total Packets Out", profile_network_s_t,
		       packets_out.total);
	MEM_ADD_DBL(mtypNetwork, "Min Megabytes Out", profile_network_s_t,
		    size_out.min);
	MEM_ADD_DBL(mtypNetwork, "Ave Megabytes Out", profile_network_s_t,
		    size_out.ave);
	MEM_ADD_DBL(mtypNetwork, "Max Megabytes Out", profile_network_s_t,
		    size_out.max);
	MEM_ADD_DBL(mtypNetwork, "Total Megabytes Out",profile_network_s_t,
		    size_out.total);

	return mtypNetwork;
}

hid_t network_s_create_file_datatype(void)
{
	hid_t   ftypNetwork = H5Tcreate(H5T_COMPOUND, TOD_LEN+17*8);
	if (ftypNetwork < 0) {
		debug3("PROFILE: failed to create Network file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftypNetwork, "Start Time", 0);
	FILE_ADD_UINT64(ftypNetwork, "Elapsed Time");
	FILE_ADD_UINT64(ftypNetwork, "Min Packets In");
	FILE_ADD_UINT64(ftypNetwork, "Ave Packets In");
	FILE_ADD_UINT64(ftypNetwork, "Max Packets In");
	FILE_ADD_UINT64(ftypNetwork, "Total Packets In");
	FILE_ADD_DBL(ftypNetwork, "Min Megabytes In");
	FILE_ADD_DBL(ftypNetwork, "Ave Megabytes In");
	FILE_ADD_DBL(ftypNetwork, "Max Megabytes In");
	FILE_ADD_DBL(ftypNetwork, "Total Megabytes In");
	FILE_ADD_UINT64(ftypNetwork, "Min Packets Out");
	FILE_ADD_UINT64(ftypNetwork, "Ave Packets Out");
	FILE_ADD_UINT64(ftypNetwork, "Max Packets Out");
	FILE_ADD_UINT64(ftypNetwork, "Total Packets Out");
	FILE_ADD_DBL(ftypNetwork, "Min Megabytes Out");
	FILE_ADD_DBL(ftypNetwork, "Ave Megabytes Out");
	FILE_ADD_DBL(ftypNetwork, "Max Megabytes Out");
	FILE_ADD_DBL(ftypNetwork, "Total Megabytes Out");

	return ftypNetwork;
}

void* network_init_job_series(int nSamples)
{
	profile_network_t*  networkData;

	networkData = xmalloc(nSamples * sizeof(profile_network_t));
	if (networkData == NULL) {
		debug3("PROFILE: failed to get memory for network data");
		return NULL;
	}
	return (void*) networkData;
}

void network_merge_step_series(hid_t group, void* prior,void* cur,void* buf)
{
// This is a difference series
	profile_network_t* prfCur = (profile_network_t*) cur;
	profile_network_t* prfBuf = (profile_network_t*) buf;
	struct tm *ts = localtime(&prfCur->time);
	strftime(prfBuf->tod, TOD_LEN, TOD_FMT, ts);
	if (prior == NULL) {
		// First sample.
		seriesStart = prfCur->time;
		prfBuf->time = 0;
	} else {
		prfBuf->time = prfCur->time - seriesStart;
	}
	prfBuf->packets_in = prfCur->packets_in;
	prfBuf->packets_out = prfCur->packets_out;
	prfBuf->size_in = prfCur->size_in;
	prfBuf->size_out = prfCur->size_out;
	return;
}

void* network_series_total(int nSamples, void* data)
{
	profile_network_t* networkData;
	profile_network_s_t* total;
	if (nSamples < 1)
		return NULL;
	networkData = (profile_network_t*) data;
	total = xmalloc(sizeof(profile_network_s_t));
	if (total == NULL) {
		error("PROFILE: Out of memory getting energy total");
		return NULL;
	}
	// Assuming network series are a running total, and the first
	// sample just sets the initial conditions
	strcpy(total->start_time, networkData[0].tod);
	total->elapsed_time = networkData[nSamples-1].time;
	INCR_DIF_SAMPLE(total, networkData, packets_in, nSamples);
	INCR_DIF_SAMPLE(total, networkData, size_in, nSamples);
	INCR_DIF_SAMPLE(total, networkData, packets_out, nSamples);
	INCR_DIF_SAMPLE(total, networkData, size_out, nSamples);
	return total;
}

void network_extract_series(FILE* fOt, bool putHeader, int job, int step,
			    char* node, char* series, void* data, int sizeData)
{
	int nItems, ix;
	profile_network_t* networkData = (profile_network_t*) data;

	if (putHeader) {
		fprintf(fOt,"Job,Step,Node,Series,Date_Time,Elapsed_time,"
			"Packets_In,MegaBytes_In,Packets_Out,MegaBytes_Out\n");
	}
	nItems = sizeData / sizeof(profile_network_t);
	for (ix=0; ix < nItems; ix++) {
		fprintf(fOt,"%d,%d,%s,%s,%s,%ld,%ld,%.3f,%ld,%.3f\n",
			job, step, node,series,
			networkData[ix].tod, networkData[ix].time,
			networkData[ix].packets_in, networkData[ix].size_in,
			networkData[ix].packets_out, networkData[ix].size_out);
	}
	return;
}

void network_extract_total(FILE* fOt, bool putHeader, int job, int step,
			   char* node, char* series, void* data, int sizeData)
{
	profile_network_s_t* networkData = (profile_network_s_t*) data;
	if (putHeader) {
		fprintf(fOt,"Job,Step,Node,Series,Start_Time,Elapsed_time,"
			"Min_Packets_In,Ave_Packets_In,"
			"Max_Packets_In,Total_Packets_In,"
			"Min_Megabytes_In,Ave_Megabytes_In,"
			"Max_Megabytes_In,Total_Megabytes_In,"
			"Min_Packets_Out,Ave_Packets_Out,"
			"Max_Packets_Out,Total_Packets_Out,"
			"Min_Megabytes_Out,Ave_Megabytes_Out,"
			"Max_Megabytes_Out,Total_Megabytes_Out\n");
	}
	fprintf(fOt,"%d,%d,%s,%s,%s,%ld", job,step,node,series,
		networkData->start_time, networkData->elapsed_time);
	PUT_UINT_SUM(fOt, networkData->packets_in,",");
	PUT_DBL_SUM(fOt, networkData->size_in,",");
	PUT_UINT_SUM(fOt, networkData->packets_out,",");
	PUT_DBL_SUM(fOt, networkData->size_out,",");
	fprintf(fOt,"\n");
	return;
}

hdf5_api_ops_t* network_profile_factory()
{
	hdf5_api_ops_t* ops = xmalloc(sizeof(hdf5_api_ops_t));
	ops->dataset_size = &network_dataset_size;
	ops->create_memory_datatype = &network_create_memory_datatype;
	ops->create_file_datatype = &network_create_file_datatype;
	ops->create_s_memory_datatype = &network_s_create_memory_datatype;
	ops->create_s_file_datatype = &network_s_create_file_datatype;
	ops->init_job_series = &network_init_job_series;
	ops->merge_step_series = &network_merge_step_series;
	ops->series_total = &network_series_total;
	ops->extract_series = &network_extract_series;
	ops->extract_total = &network_extract_total;
	return ops;
}

// ============================================================================
// Routines supporting Task Data type
// ============================================================================

int task_dataset_size()
{
	return sizeof(profile_task_t);
}

hid_t task_create_memory_datatype()
{
	hid_t   mtypTask = -1;

	mtypTask = H5Tcreate(H5T_COMPOUND, sizeof(profile_task_t));
	if (mtypTask < 0) {
		debug3("PROFILE: failed to create Task memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtypTask,"Date Time", profile_task_t,tod);
	MEM_ADD_UINT64(mtypTask,"Time", profile_task_t, time);
	MEM_ADD_UINT64(mtypTask,"CPU Frequency", profile_task_t, cpu_freq);
	MEM_ADD_UINT64(mtypTask,"CPU Time", profile_task_t, cpu_time);
	MEM_ADD_DBL(mtypTask,"CPU Utilization",
		    profile_task_t, cpu_utilization);
	MEM_ADD_UINT64(mtypTask,"RSS", profile_task_t, rss);
	MEM_ADD_UINT64(mtypTask,"VM Size", profile_task_t, vm_size);
	MEM_ADD_UINT64(mtypTask,"Pages", profile_task_t, pages);
	MEM_ADD_DBL(mtypTask,"Read Megabytes", profile_task_t, read_size);
	MEM_ADD_DBL(mtypTask,"Write Megabytes", profile_task_t, write_size);

	return mtypTask;
}

hid_t task_create_file_datatype()
{
	hid_t   ftypTask = H5Tcreate(H5T_COMPOUND, TOD_LEN+9*8);
	if (ftypTask < 0) {
		debug3("PROFILE: failed to create Task file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftypTask, "Date Time", 0);
	FILE_ADD_UINT64(ftypTask, "Time");
	FILE_ADD_UINT64(ftypTask, "CPU Frequency");
	FILE_ADD_UINT64(ftypTask, "CPU Time");
	FILE_ADD_DBL(ftypTask, "CPU Utilization");
	FILE_ADD_UINT64(ftypTask, "RSS");
	FILE_ADD_UINT64(ftypTask, "VM Size");
	FILE_ADD_UINT64(ftypTask, "Pages");
	FILE_ADD_DBL(ftypTask, "Read Megabytes");
	FILE_ADD_DBL(ftypTask, "Write Megabytes");

	return ftypTask;
}

hid_t task_s_create_memory_datatype()
{
	hid_t   mtypTask = H5Tcreate(H5T_COMPOUND, sizeof(profile_task_s_t));
	if (mtypTask < 0) {
		debug3("PROFILE: failed to create Task memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtypTask,"Start Time", profile_task_s_t,start_time);
	MEM_ADD_UINT64(mtypTask,"Elapsed Time", profile_task_s_t, elapsed_time);
	MEM_ADD_UINT64(mtypTask,"Min CPU Frequency", profile_task_s_t,
		       cpu_freq.min);
	MEM_ADD_UINT64(mtypTask,"Ave CPU Frequency", profile_task_s_t,
		       cpu_freq.ave);
	MEM_ADD_UINT64(mtypTask,"Max CPU Frequency", profile_task_s_t,
		       cpu_freq.max);
	MEM_ADD_UINT64(mtypTask,"Total CPU Frequency", profile_task_s_t,
		       cpu_freq.total);
	MEM_ADD_UINT64(mtypTask,"Min CPU Time", profile_task_s_t, cpu_time.min);
	MEM_ADD_UINT64(mtypTask,"Ave CPU Time", profile_task_s_t, cpu_time.ave);
	MEM_ADD_UINT64(mtypTask,"Max CPU Time", profile_task_s_t, cpu_time.max);
	MEM_ADD_UINT64(mtypTask,"Total CPU Time", profile_task_s_t,
		       cpu_time.total);
	MEM_ADD_DBL(mtypTask,"Min CPU Utilization", profile_task_s_t,
		    cpu_utilization.min);
	MEM_ADD_DBL(mtypTask,"Ave CPU Utilization", profile_task_s_t,
		    cpu_utilization.ave);
	MEM_ADD_DBL(mtypTask,"Max CPU Utilization", profile_task_s_t,
		    cpu_utilization.max);
	MEM_ADD_DBL(mtypTask,"Total CPU Utilization", profile_task_s_t,
		    cpu_utilization.total);
	MEM_ADD_UINT64(mtypTask,"Min RSS", profile_task_s_t, rss.min);
	MEM_ADD_UINT64(mtypTask,"Ave RSS", profile_task_s_t, rss.ave);
	MEM_ADD_UINT64(mtypTask,"Max RSS", profile_task_s_t, rss.max);
	MEM_ADD_UINT64(mtypTask,"Total RSS", profile_task_s_t, rss.total);
	MEM_ADD_UINT64(mtypTask,"Min VM Size", profile_task_s_t, vm_size.min);
	MEM_ADD_UINT64(mtypTask,"Ave VM Size", profile_task_s_t, vm_size.ave);
	MEM_ADD_UINT64(mtypTask,"Max VM Size", profile_task_s_t, vm_size.max);
	MEM_ADD_UINT64(mtypTask,"Total VM Size",
		       profile_task_s_t, vm_size.total);
	MEM_ADD_UINT64(mtypTask,"Min Pages", profile_task_s_t, pages.min);
	MEM_ADD_UINT64(mtypTask,"Ave Pages", profile_task_s_t, pages.ave);
	MEM_ADD_UINT64(mtypTask, "Max Pages", profile_task_s_t, pages.max);
	MEM_ADD_UINT64(mtypTask, "Total Pages", profile_task_s_t, pages.total);
	MEM_ADD_DBL(mtypTask, "Min Read Megabytes", profile_task_s_t,
		    read_size.min);
	MEM_ADD_DBL(mtypTask, "Ave Read Megabytes", profile_task_s_t,
		    read_size.ave);
	MEM_ADD_DBL(mtypTask, "Max Read Megabytes", profile_task_s_t,
		    read_size.max);
	MEM_ADD_DBL(mtypTask, "Total Read Megabytes", profile_task_s_t,
		    read_size.total);
	MEM_ADD_DBL(mtypTask, "Min Write Megabytes", profile_task_s_t,
		    write_size.min);
	MEM_ADD_DBL(mtypTask, "Ave Write Megabytes", profile_task_s_t,
		    write_size.ave);
	MEM_ADD_DBL(mtypTask, "Max Write Megabytes", profile_task_s_t,
		    write_size.max);
	MEM_ADD_DBL(mtypTask, "Total Write Megabytes", profile_task_s_t,
		    write_size.total);

	return mtypTask;
}

hid_t task_s_create_file_datatype()
{
	hid_t   ftypTask = H5Tcreate(H5T_COMPOUND, TOD_LEN+33*8);
	if (ftypTask < 0) {
		debug3("PROFILE: failed to create Task file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftypTask, "Start Time", 0);
	FILE_ADD_UINT64(ftypTask, "Elapsed Time");
	FILE_ADD_UINT64(ftypTask, "Min CPU Frequency");
	FILE_ADD_UINT64(ftypTask, "Ave CPU Frequency");
	FILE_ADD_UINT64(ftypTask, "Max CPU Frequency");
	FILE_ADD_UINT64(ftypTask, "Total CPU Frequency");
	FILE_ADD_UINT64(ftypTask, "Min CPU Time");
	FILE_ADD_UINT64(ftypTask, "Ave CPU Time");
	FILE_ADD_UINT64(ftypTask, "Max CPU Time");
	FILE_ADD_UINT64(ftypTask, "Total CPU Time");
	FILE_ADD_DBL(ftypTask, "Min CPU Utilization");
	FILE_ADD_DBL(ftypTask, "Ave CPU Utilization");
	FILE_ADD_DBL(ftypTask, "Max CPU Utilization");
	FILE_ADD_DBL(ftypTask, "Total CPU Utilization");
	FILE_ADD_UINT64(ftypTask, "Min RSS");
	FILE_ADD_UINT64(ftypTask, "Ave RSS");
	FILE_ADD_UINT64(ftypTask, "Max RSS");
	FILE_ADD_UINT64(ftypTask, "Total RSS");
	FILE_ADD_UINT64(ftypTask, "Min VM Size");
	FILE_ADD_UINT64(ftypTask, "Ave VM Size");
	FILE_ADD_UINT64(ftypTask, "Max VM Size");
	FILE_ADD_UINT64(ftypTask, "Total VM Size");
	FILE_ADD_UINT64(ftypTask, "Min Pages");
	FILE_ADD_UINT64(ftypTask, "Ave Pages");
	FILE_ADD_UINT64(ftypTask, "Max Pages");
	FILE_ADD_UINT64(ftypTask, "Total Pages");
	FILE_ADD_DBL(ftypTask, "Min Read Megabytes");
	FILE_ADD_DBL(ftypTask, "Ave Read Megabytes");
	FILE_ADD_DBL(ftypTask, "Max Read Megabytes");
	FILE_ADD_DBL(ftypTask, "Total Read Megabytes");
	FILE_ADD_DBL(ftypTask, "Min Write Megabytes");
	FILE_ADD_DBL(ftypTask, "Ave Write Megabytes");
	FILE_ADD_DBL(ftypTask, "Max Write Megabytes");
	FILE_ADD_DBL(ftypTask, "Total Write Megabytes");

	return ftypTask;
}

void* task_init_job_series(int nSamples)
{
	profile_task_t*  taskData;
	taskData = xmalloc(nSamples * sizeof(profile_task_t));
	if (taskData == NULL) {
		debug3("PROFILE: failed to get memory for combined task data");
		return NULL;
	}
	return (void*) taskData;
}

void task_merge_step_series(hid_t group, void* prior, void* cur, void* buf)
{
// This is a running total series
	profile_task_t* prfPrior = (profile_task_t*) prior;
	profile_task_t* prfCur = (profile_task_t*) cur;
	profile_task_t* prfBuf = (profile_task_t*) buf;

	struct tm *ts;
	ts = localtime(&prfCur->time);
	strftime(prfBuf->tod, TOD_LEN, TOD_FMT, ts);
	if (prfPrior == NULL) {
		// First sample.
		seriesStart = prfCur->time;
		prfBuf->time = 0;
		prfBuf->cpu_time = 0;
		prfBuf->read_size = 0.0;
		prfBuf->write_size = 0.0;
	} else {
		prfBuf->time = prfCur->time - seriesStart;
		prfBuf->cpu_time = prfCur->cpu_time - prfPrior->cpu_time;
		prfBuf->read_size =
			prfCur->read_size - prfPrior->read_size;
		prfBuf->write_size =
			prfCur->write_size - prfPrior->write_size;
	}
	prfBuf->cpu_freq = prfCur->cpu_freq;
	prfBuf->cpu_utilization = prfCur->cpu_utilization;
	prfBuf->rss = prfCur->rss;
	prfBuf->vm_size = prfCur->vm_size;
	prfBuf->pages = prfCur->pages;
	return;
}

void* task_series_total(int nSamples, void* data)
{
	profile_task_t* taskData;
	profile_task_s_t* total;
	taskData = (profile_task_t*) data;
	total = xmalloc(sizeof(profile_task_s_t));
	if (total == NULL) {
		error("PROFILE: Out of memory getting task total");
		return NULL;
	}
	strcpy(total->start_time, taskData[0].tod);
	total->elapsed_time = taskData[nSamples-1].time;
	INCR_DIF_SAMPLE(total, taskData, cpu_freq, nSamples);
	INCR_RT_SAMPLE(total, taskData, cpu_time, nSamples);
	INCR_DIF_SAMPLE(total, taskData, cpu_utilization, nSamples);
	INCR_DIF_SAMPLE(total, taskData, rss, nSamples);
	INCR_DIF_SAMPLE(total, taskData, vm_size , nSamples);
	INCR_DIF_SAMPLE(total, taskData, pages, nSamples);
	INCR_RT_SAMPLE(total, taskData, read_size, nSamples);
	INCR_RT_SAMPLE(total, taskData, write_size, nSamples);
	return total;
}

void task_extract_series(FILE* fOt, bool putHeader, int job, int step,
			 char* node, char* series, void* data, int sizeData)
{
	int nItems, ix;
	profile_task_t* taskData = (profile_task_t*) data;
	if (putHeader) {
		fprintf(fOt,"Job,Step,Node,Series,Date Time,ElapsedTime,"
			"CPU Frequency,CPU Time,"
			"CPU Utilization,rss,VM Size,Pages,"
			"Read_bytes,Write_bytes\n");
	}
	nItems = sizeData / sizeof(profile_task_t);
	for (ix=0; ix < nItems; ix++) {
		fprintf(fOt,"%d,%d,%s,%s,%s,%ld,%ld,%ld,%.3f",
			job, step, node, series,
			taskData[ix].tod, taskData[ix].time,
			taskData[ix].cpu_freq,
			taskData[ix].cpu_time, taskData[ix].cpu_utilization);
		fprintf(fOt,",%ld,%ld,%ld,%.3f,%.3f\n",	taskData[ix].rss,
			taskData[ix].vm_size, taskData[ix].pages,
			taskData[ix].read_size, taskData[ix].write_size);
	}
	return;
}

void task_extract_total(FILE* fOt, bool putHeader, int job, int step,
			char* node, char* series, void* data, int sizeData)
{

	profile_task_s_t* taskData = (profile_task_s_t*) data;
	if (putHeader) {
		fprintf(fOt,"Job,Step,Node,Series,Start_Time,Elapsed_time,"
			"Min CPU Frequency,Ave CPU Frequency,"
			"Ave CPU Frequency,Total CPU Frequency,"
			"Min_CPU_Time,Ave_CPU_Time,"
			"Max_CPU_Time,Total_CPU_Time,"
			"Min_CPU_Utilization,Ave_CPU_Utilization,"
			"Max_CPU_Utilization,Total_CPU_Utilization,"
			"Min_RSS,Ave_RSS,Max_RSS,Total_RSS,"
			"Min_VMSize,Ave_VMSize,Max_VMSize,Total_VMSize,"
			"Min_Pages,Ave_Pages,Max_Pages,Total_Pages,"
			"Min_Read_Megabytes,Ave_Read_Megabytes,"
			"Max_Read_Megabytes,Total_Read_Megabytes,"
			"Min_Write_Megabytes,Ave_Write_Megabytes,"
			"Max_Write_Megabytes,Total_Write_Megabytes\n");
	}
	fprintf(fOt,"%d,%d,%s,%s,%s,%ld",job,step,node,series,
		taskData->start_time, taskData->elapsed_time);
	PUT_UINT_SUM(fOt, taskData->cpu_freq,",");
	PUT_UINT_SUM(fOt, taskData->cpu_time,",");
	PUT_DBL_SUM(fOt, taskData->cpu_utilization,",");
	PUT_UINT_SUM(fOt, taskData->rss,",");
	PUT_UINT_SUM(fOt, taskData->vm_size,",");
	PUT_UINT_SUM(fOt, taskData->pages,",");
	PUT_DBL_SUM(fOt, taskData->read_size,",");
	PUT_DBL_SUM(fOt, taskData->write_size,",");
	fprintf(fOt,"\n");
	return;
}

hdf5_api_ops_t* task_profile_factory()
{
	hdf5_api_ops_t* ops = xmalloc(sizeof(hdf5_api_ops_t));
	ops->dataset_size = &task_dataset_size;
	ops->create_memory_datatype = &task_create_memory_datatype;
	ops->create_file_datatype = &task_create_file_datatype;
	ops->create_s_memory_datatype = &task_s_create_memory_datatype;
	ops->create_s_file_datatype = &task_s_create_file_datatype;
	ops->init_job_series = &task_init_job_series;
	ops->merge_step_series = &task_merge_step_series;
	ops->series_total = &task_series_total;
	ops->extract_series = &task_extract_series;
	ops->extract_total = &task_extract_total;
	return ops;
}
