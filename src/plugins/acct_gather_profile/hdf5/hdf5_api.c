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
		tot->var.min = MIN(smp[i].var, tot->var.min);	\
		tot->var.max = MAX(smp[i].var, tot->var.max);	\
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
		tot->var.min = MIN(smp[i].var, tot->var.min);	\
		tot->var.max = MAX(smp[i].var, tot->var.max);	\
	}							\
	tot->var.ave = tot->var.total / count;

/* Macro to put an int min,ave,max,total for a variable to extract file
 *
 * Parameters
 * 	fp	file descriptor
 * 	var	variable name
 * 	prf	prefix for series (usually ','
 */
#define PUT_UINT_SUM(fp, var, prfx)			\
	fprintf(fp, "%s%ld,%ld,%ld,%ld", prfx,		\
		var.min, var.ave, var.max, var.total);
/* Macro to put an int min,ave,max,total for a variable to extract file
 *
 * Parameters
 * 	fp	file descriptor
 * 	var	variable name
 * 	prf	prefix for series (usually ','
 */
#define PUT_DBL_SUM(fp, var, prfx)			\
	fprintf(fp, "%s%.3f,%.3f,%.3f,%.3f", prfx,	\
		var.min, var.ave, var.max, var.total);


// ============================================================================
// Routines supporting Energy Data type
// ============================================================================

static int _energy_dataset_size(void)
{
	return sizeof(profile_energy_t);
}

static hid_t _energy_create_memory_datatype(void)
{
	hid_t   mtyp_energy = H5Tcreate(H5T_COMPOUND, sizeof(profile_energy_t));
	if (mtyp_energy < 0) {
		debug3("PROFILE: failed to create Energy memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtyp_energy, "Date_Time", profile_energy_t, tod);
	MEM_ADD_UINT64(mtyp_energy, "Time", profile_energy_t, time);
	MEM_ADD_UINT64(mtyp_energy, "Power", profile_energy_t, power);
	MEM_ADD_UINT64(mtyp_energy, "CPU_Frequency",
		       profile_energy_t, cpu_freq);

	return mtyp_energy;
}

static hid_t _energy_create_file_datatype(void)
{
	hid_t   ftyp_energy = H5Tcreate(H5T_COMPOUND, (TOD_LEN+3*8));
	if (ftyp_energy < 0) {
		debug3("PROFILE: failed to create Energy file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftyp_energy, "Date_Time", 0);
	FILE_ADD_UINT64(ftyp_energy, "Time");
	FILE_ADD_UINT64(ftyp_energy, "Power");
	FILE_ADD_UINT64(ftyp_energy, "CPU_Frequency");

	return ftyp_energy;
}

static hid_t _energy_s_create_memory_datatype(void)
{
	hid_t   mtyp_energy = H5Tcreate(H5T_COMPOUND,
					sizeof(profile_energy_s_t));
	if (mtyp_energy < 0) {
		debug3("PROFILE: failed to create Energy_s memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtyp_energy, "Start Time",
			  profile_energy_s_t, start_time);
	MEM_ADD_UINT64(mtyp_energy, "Elapsed Time",
		       profile_energy_s_t, elapsed_time);
	MEM_ADD_UINT64(mtyp_energy, "Min Power", profile_energy_s_t, power.min);
	MEM_ADD_UINT64(mtyp_energy, "Ave Power", profile_energy_s_t, power.ave);
	MEM_ADD_UINT64(mtyp_energy, "Max Power", profile_energy_s_t, power.max);
	MEM_ADD_UINT64(mtyp_energy, "Total Power",
		       profile_energy_s_t, power.total);
	MEM_ADD_UINT64(mtyp_energy, "Min CPU Frequency", profile_energy_s_t,
		       cpu_freq.min);
	MEM_ADD_UINT64(mtyp_energy, "Ave CPU Frequency", profile_energy_s_t,
		       cpu_freq.ave);
	MEM_ADD_UINT64(mtyp_energy, "Max CPU Frequency", profile_energy_s_t,
		       cpu_freq.max);
	MEM_ADD_UINT64(mtyp_energy, "Total CPU Frequency", profile_energy_s_t,
		       cpu_freq.total);

	return mtyp_energy;
}

static hid_t _energy_s_create_file_datatype(void)
{
	hid_t   ftyp_energy = H5Tcreate(H5T_COMPOUND, (TOD_LEN+9*8));
	if (ftyp_energy < 0) {
		debug3("PROFILE: failed to create Energy_s file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftyp_energy, "Start Time", 0);
	FILE_ADD_UINT64(ftyp_energy, "Elapsed Time");
	FILE_ADD_UINT64(ftyp_energy, "Min Power");
	FILE_ADD_UINT64(ftyp_energy, "Ave Power");
	FILE_ADD_UINT64(ftyp_energy, "Max Power");
	FILE_ADD_UINT64(ftyp_energy, "Total Power");
	FILE_ADD_UINT64(ftyp_energy, "Min CPU Frequency");
	FILE_ADD_UINT64(ftyp_energy, "Ave CPU Frequency");
	FILE_ADD_UINT64(ftyp_energy, "Max CPU Frequency");
	FILE_ADD_UINT64(ftyp_energy, "Total CPU Frequency");

	return ftyp_energy;
}

static void *_energy_init_job_series(int n_samples)
{
	profile_energy_t*  energy_data;

	energy_data = xmalloc(n_samples * sizeof(profile_energy_t));
	if (energy_data == NULL) {
		debug3("PROFILE: failed to get memory for energy data");
		return NULL;
	}
	return (void*) energy_data;
}

static char** _energy_get_series_tod(void* data, int nsmp)
{
	int ix;
	char      **tod_values = NULL;
	profile_energy_t* energy_series = (profile_energy_t*) data;
	tod_values = (char**) xmalloc(nsmp*sizeof(char*));
	if (tod_values == NULL) {
		info("Failed to get memory for energy tod");
		return NULL;
	}
	for (ix=0; ix < nsmp; ix++) {
		tod_values[ix] = xstrdup(energy_series[ix].tod);
	}
	return tod_values;
}

static double* _energy_get_series_values(char* data_name, void* data, int nsmp)
{
	int ix;
	profile_energy_t* energy_series = (profile_energy_t*) data;
	double  *energy_values = NULL;
	energy_values = xmalloc(nsmp*sizeof(double));
	if (energy_values == NULL) {
		info("PROFILE: Failed to get memory for energy data");
		return NULL;
	}
	if (strcasecmp(data_name,"Time") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			energy_values[ix] = (double) energy_series[ix].time;

		}
		return energy_values;
	} else if (strcasecmp(data_name,"Power") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			energy_values[ix] = (double) energy_series[ix].power;

		}
		return energy_values;
	} else if (strcasecmp(data_name,"CPU_Frequency") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			energy_values[ix] = (double) energy_series[ix].cpu_freq;

		}
		return energy_values;
	}
	xfree(energy_values);
	info("PROFILE: %s is invalid data item for energy data", data_name);
	return NULL;
}

static void _energy_merge_step_series(
	hid_t group, void *prior, void *cur, void *buf)
{
//	This is a difference series
	profile_energy_t* prf_cur = (profile_energy_t*) cur;
	profile_energy_t* prf_buf = (profile_energy_t*) buf;
	struct tm *ts = localtime(&prf_cur->time);
	strftime(prf_buf->tod, TOD_LEN, TOD_FMT, ts);
	if (prior == NULL) {
		// First sample.
		seriesStart = prf_cur->time;
		prf_buf->time = 0;

	} else {
		prf_buf->time = prf_cur->time - seriesStart;
	}
	prf_buf->power = prf_cur->power;
	prf_buf->cpu_freq = prf_cur->cpu_freq;
	return;
}

static void *_energy_series_total(int n_samples, void *data)
{
	profile_energy_t* energy_data;
	profile_energy_s_t* total;
	if (n_samples < 1)
		return NULL;
	energy_data = (profile_energy_t*) data;
	total = xmalloc(sizeof(profile_energy_s_t));
	if (total == NULL) {
		error("PROFILE: Out of memory getting energy total");
		return NULL;
	}
	// Assuming energy series are a difference series
	strcpy(total->start_time, energy_data[0].tod);
	total->elapsed_time = energy_data[n_samples-1].time;
	INCR_DIF_SAMPLE(total, energy_data, power, n_samples);
	INCR_DIF_SAMPLE(total, energy_data, cpu_freq, n_samples);
	return total;
}

static void _energy_extract_series(
	FILE* fp, bool put_header, int job, int step,
	char *node, char *series, void *data, int size_data)
{

	int n_items, ix;
	profile_energy_t* energy_data = (profile_energy_t*) data;
	if (put_header) {
		fprintf(fp, "Job,Step,Node,Series,Date_Time,Elapsed_Time,"
			"Power, CPU_Frequency\n");
	}
	n_items = size_data / sizeof(profile_energy_t);
	for (ix=0; ix < n_items; ix++) {
		fprintf(fp, "%d,%d,%s,%s,%s,%ld,%ld,%ld\n", job, step, node,
			series, energy_data[ix].tod, energy_data[ix].time,
			energy_data[ix].power, energy_data[ix].cpu_freq);
	}
	return;
}

static void _energy_extract_total(
	FILE* fp, bool put_header, int job, int step,
	char *node, char *series, void *data, int size_data)
{
	profile_energy_s_t* energy_data = (profile_energy_s_t*) data;
	if (put_header) {
		fprintf(fp, "Job,Step,Node,Series,Start_Time,Elapsed_Time,"
			"Min_Power,Ave_Power,Max_Power,Total_Power,"
			"Min_CPU Frequency,Ave_CPU Frequency,"
			"Max_CPU Frequency,Total_CPU Frequency\n");
	}
	fprintf(fp, "%d,%d,%s,%s,%s,%ld", job, step, node, series,
		energy_data->start_time, energy_data->elapsed_time);
	PUT_UINT_SUM(fp, energy_data->power, ",");
	PUT_UINT_SUM(fp, energy_data->cpu_freq, ",");
	fprintf(fp, "\n");
	return;
}

static hdf5_api_ops_t* _energy_profile_factory(void)
{
	hdf5_api_ops_t* ops = xmalloc(sizeof(hdf5_api_ops_t));
	ops->dataset_size = &_energy_dataset_size;
	ops->create_memory_datatype = &_energy_create_memory_datatype;
	ops->create_file_datatype = &_energy_create_file_datatype;
	ops->create_s_memory_datatype = &_energy_s_create_memory_datatype;
	ops->create_s_file_datatype = &_energy_s_create_file_datatype;
	ops->init_job_series = &_energy_init_job_series;
	ops->get_series_tod = &_energy_get_series_tod;
	ops->get_series_values = &_energy_get_series_values;
	ops->merge_step_series = &_energy_merge_step_series;
	ops->series_total = &_energy_series_total;
	ops->extract_series = &_energy_extract_series;
	ops->extract_total = &_energy_extract_total;
	return ops;
}


// ============================================================================
// Routines supporting I/O Data type
// ============================================================================

static int _io_dataset_size(void)
{
	return sizeof(profile_io_t);
}

static hid_t _io_create_memory_datatype(void)
{
	hid_t   mtyp_io = -1;

	mtyp_io = H5Tcreate(H5T_COMPOUND, sizeof(profile_io_t));
	if (mtyp_io < 0) {
		debug3("PROFILE: failed to create IO memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtyp_io, "Date_Time", profile_io_t, tod);
	MEM_ADD_UINT64(mtyp_io, "Time", profile_io_t, time);
	MEM_ADD_UINT64(mtyp_io, "Reads", profile_io_t, reads);
	MEM_ADD_DBL(mtyp_io, "Megabytes_Read", profile_io_t, read_size);
	MEM_ADD_UINT64(mtyp_io, "Writes", profile_io_t, writes);
	MEM_ADD_DBL(mtyp_io, "Megabytes_Write", profile_io_t, write_size);
	return mtyp_io;
}

static hid_t _io_create_file_datatype(void)
{
	hid_t   ftyp_io = -1;

	ftyp_io = H5Tcreate(H5T_COMPOUND, TOD_LEN+5*8);
	if (ftyp_io < 0) {
		debug3("PROFILE: failed to create IO file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftyp_io, "Date_Time", 0);
	FILE_ADD_UINT64(ftyp_io, "Time");
	FILE_ADD_UINT64(ftyp_io, "Reads");
	FILE_ADD_DBL(ftyp_io, "Megabytes_Read");
	FILE_ADD_UINT64(ftyp_io, "Writes");
	FILE_ADD_DBL(ftyp_io, "Megabytes_Write");

	return ftyp_io;
}

static hid_t _io_s_create_memory_datatype(void)
{
	hid_t   mtyp_io = -1;

	mtyp_io = H5Tcreate(H5T_COMPOUND, sizeof(profile_io_s_t));
	if (mtyp_io < 0) {
		debug3("PROFILE: failed to create IO memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtyp_io, "Start Time", profile_io_s_t, start_time);
	MEM_ADD_UINT64(mtyp_io, "Elapsed Time", profile_io_s_t, elapsed_time);
	MEM_ADD_UINT64(mtyp_io, "Min Reads", profile_io_s_t, reads.min);
	MEM_ADD_UINT64(mtyp_io, "Ave Reads", profile_io_s_t, reads.ave);
	MEM_ADD_UINT64(mtyp_io, "Max Reads", profile_io_s_t, reads.max);
	MEM_ADD_UINT64(mtyp_io, "Total Reads", profile_io_s_t, reads.total);
	MEM_ADD_DBL(mtyp_io, "Min Read Megabytes",
		    profile_io_s_t, read_size.min);
	MEM_ADD_DBL(mtyp_io, "Ave Read Megabytes",
		    profile_io_s_t, read_size.ave);
	MEM_ADD_DBL(mtyp_io, "Max Read Megabytes",
		    profile_io_s_t, read_size.max);
	MEM_ADD_DBL(mtyp_io, "Total Read Megabytes", profile_io_s_t,
		    read_size.total);
	MEM_ADD_UINT64(mtyp_io, "Min Writes", profile_io_s_t, writes.min);
	MEM_ADD_UINT64(mtyp_io, "Ave Writes", profile_io_s_t, writes.ave);
	MEM_ADD_UINT64(mtyp_io, "Max Writes", profile_io_s_t, writes.max);
	MEM_ADD_UINT64(mtyp_io, "Total Writes", profile_io_s_t, writes.total);
	MEM_ADD_DBL(mtyp_io, "Min Write Megabytes", profile_io_s_t,
		    write_size.min);
	MEM_ADD_DBL(mtyp_io, "Ave Write Megabytes", profile_io_s_t,
		    write_size.ave);
	MEM_ADD_DBL(mtyp_io, "Max Write Megabytes", profile_io_s_t,
		    write_size.max);
	MEM_ADD_DBL(mtyp_io, "Total Write Megabytes", profile_io_s_t,
		    write_size.total);

	return mtyp_io;
}

static hid_t _io_s_create_file_datatype(void)
{
	hid_t   ftyp_io = -1;

	ftyp_io = H5Tcreate(H5T_COMPOUND, TOD_LEN+17*8);
	if (ftyp_io < 0) {
		debug3("PROFILE: failed to create IO file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftyp_io, "Start Time", 0);
	FILE_ADD_UINT64(ftyp_io, "Elapsed Time");
	FILE_ADD_UINT64(ftyp_io, "Min Reads");
	FILE_ADD_UINT64(ftyp_io, "Ave Reads");
	FILE_ADD_UINT64(ftyp_io, "Max Reads");
	FILE_ADD_UINT64(ftyp_io, "Total Reads");
	FILE_ADD_DBL(ftyp_io, "Min Read Megabytes");
	FILE_ADD_DBL(ftyp_io, "Ave Read Megabytes");
	FILE_ADD_DBL(ftyp_io, "Max Read Megabytes");
	FILE_ADD_DBL(ftyp_io, "Total Read Megabytes");
	FILE_ADD_UINT64(ftyp_io, "Min Writes");
	FILE_ADD_UINT64(ftyp_io, "Ave Writes");
	FILE_ADD_UINT64(ftyp_io, "Max Writes");
	FILE_ADD_UINT64(ftyp_io, "Total Writes");
	FILE_ADD_DBL(ftyp_io, "Min Write Megabytes");
	FILE_ADD_DBL(ftyp_io, "Ave Write Megabytes");
	FILE_ADD_DBL(ftyp_io, "Max Write Megabytes");
	FILE_ADD_DBL(ftyp_io, "Total Write Megabytes");

	return ftyp_io;
}

static void *_io_init_job_series(int n_samples)
{
	profile_io_t*  io_data;
	io_data = xmalloc(n_samples * sizeof(profile_io_t));
	if (io_data == NULL) {
		debug3("PROFILE: failed to get memory for combined io data");
		return NULL;
	}
	return (void*) io_data;
}

static char** _io_get_series_tod(void* data, int nsmp)
{
	int ix;
	char      **tod_values = NULL;
	profile_io_t* io_series = (profile_io_t*) data;
	tod_values = (char**) xmalloc(nsmp*sizeof(char*));
	if (tod_values == NULL) {
		info("Failed to get memory for io tod");
		return NULL;
	}
	for (ix=0; ix < nsmp; ix++) {
		tod_values[ix] = xstrdup(io_series[ix].tod);
	}
	return tod_values;
}

static double* _io_get_series_values(char* data_name, void* data, int nsmp)
{
	int ix;
	profile_io_t* io_series = (profile_io_t*) data;
	double  *io_values = NULL;
	io_values = xmalloc(nsmp*sizeof(double));
	if (io_values == NULL) {
		info("PROFILE: Failed to get memory for io data");
		return NULL;
	}
	if (strcasecmp(data_name,"Time") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			io_values[ix] = (double) io_series[ix].time;

		}
		return io_values;
	} else if (strcasecmp(data_name,"Reads") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			io_values[ix] = (double) io_series[ix].reads;

		}
		return io_values;
	} else if (strcasecmp(data_name,"Megabytes_Read") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			io_values[ix] = io_series[ix].read_size;

		}
		return io_values;
	} else if (strcasecmp(data_name,"Writes") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			io_values[ix] = (double) io_series[ix].writes;

		}
		return io_values;
	} else if (strcasecmp(data_name,"Megabytes_Write") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			io_values[ix] = io_series[ix].write_size;

		}
		return io_values;
	}
	xfree(io_values);
	info("PROFILE: %s is invalid data item for io data", data_name);
	return NULL;
}

static void _io_merge_step_series(
	hid_t group, void *prior, void *cur, void *buf)
{
	// This is a difference series
	static uint64_t start_reads = 0;
	static uint64_t start_writes = 0;
	static double start_read_size = 0;
	static double start_write_size = 0;
	profile_io_t* prfCur = (profile_io_t*) cur;
	profile_io_t* prfBuf = (profile_io_t*) buf;
	struct tm *ts = localtime(&prfCur->time);
	strftime(prfBuf->tod, TOD_LEN, TOD_FMT, ts);
	if (prior == NULL) {
		// First sample.
		seriesStart = prfCur->time;
		prfBuf->time = 0;
		start_reads = prfCur->reads;
		prfBuf->reads = 0;
		start_writes = prfCur->writes;
		prfBuf->writes = 0;
		start_read_size = prfCur->read_size;
		prfBuf->read_size = 0;
		start_write_size = prfCur->write_size;
		prfBuf->write_size = 0;
	} else {
		prfBuf->time = prfCur->time - seriesStart;
		prfBuf->reads = prfCur->reads - start_reads;
		prfBuf->writes = prfCur->writes - start_writes;
		prfBuf->read_size = prfCur->read_size - start_read_size;
		prfBuf->write_size = prfCur->write_size - start_write_size;
	}
	return;
}

static void *_io_series_total(int n_samples, void *data)
{
	profile_io_t* io_data;
	profile_io_s_t* total;
	if (n_samples < 1)
		return NULL;
	io_data = (profile_io_t*) data;
	total = xmalloc(sizeof(profile_io_s_t));
	if (total == NULL) {
		error("PROFILE: Out of memory getting I/O total");
		return NULL;
	}
	// Assuming io series are a running total, and the first
	// sample just sets the initial conditions
	strcpy(total->start_time, io_data[0].tod);
	total->elapsed_time = io_data[n_samples-1].time;
	INCR_DIF_SAMPLE(total, io_data, reads, n_samples);
	INCR_DIF_SAMPLE(total, io_data, read_size, n_samples);
	INCR_DIF_SAMPLE(total, io_data, writes, n_samples);
	INCR_DIF_SAMPLE(total, io_data, write_size, n_samples);
	return total;
}

static void _io_extract_series(
	FILE* fp, bool put_header, int job, int step,
	char *node, char *series, void *data, int size_data)
{
	int n_items, ix;
	profile_io_t* io_data = (profile_io_t*) data;
	if (put_header) {
		fprintf(fp,"Job,Step,Node,Series,Date_Time,Elapsed_time,"
			"Reads,Read Megabytes,Writes,Write Megabytes\n");
	}
	n_items = size_data / sizeof(profile_io_t);
	for (ix=0; ix < n_items; ix++) {
		fprintf(fp,"%d,%d,%s,%s,%s,%ld,%ld,%.3f,%ld,%.3f\n",
			job, step, node, series,
			io_data[ix].tod, io_data[ix].time,
			io_data[ix].reads, io_data[ix].read_size,
			io_data[ix].writes, io_data[ix].write_size);
	}
	return;
}

static void _io_extract_total(
	FILE* fp, bool put_header, int job, int step,
	char *node, char *series, void *data, int size_data)
{
	profile_io_s_t* io_data = (profile_io_s_t*) data;
	if (put_header) {
		fprintf(fp,"Job,Step,Node,Series,Start_Time,Elapsed_time,"
			"Min_Reads,Ave_Reads,Max_Reads,Total_Reads,"
			"Min_Read_Megabytes,Ave_Read_Megabytes,"
			"Max_Read_Megabytes,Total_Read_Megabytes,"
			"Min_Writes,Ave_Writes,Max_Writes,Total_Writes,"
			"Min_Write_Megabytes,Ave_Write_Megabytes,"
			"Max_Write_Megabytes,Total_Write_Megabytes\n");
	}
	fprintf(fp, "%d,%d,%s,%s,%s,%ld", job, step, node, series,
		io_data->start_time, io_data->elapsed_time);
	PUT_UINT_SUM(fp, io_data->reads, ",");
	PUT_DBL_SUM(fp, io_data->read_size, ",");
	PUT_UINT_SUM(fp, io_data->writes, ",");
	PUT_DBL_SUM(fp, io_data->write_size, ",");
	fprintf(fp, "\n");
	return;
}

static hdf5_api_ops_t* _io_profile_factory(void)
{
	hdf5_api_ops_t* ops = xmalloc(sizeof(hdf5_api_ops_t));
	ops->dataset_size = &_io_dataset_size;
	ops->create_memory_datatype = &_io_create_memory_datatype;
	ops->create_file_datatype = &_io_create_file_datatype;
	ops->create_s_memory_datatype = &_io_s_create_memory_datatype;
	ops->create_s_file_datatype = &_io_s_create_file_datatype;
	ops->init_job_series = &_io_init_job_series;
	ops->get_series_tod = &_io_get_series_tod;
	ops->get_series_values = &_io_get_series_values;
	ops->merge_step_series = &_io_merge_step_series;
	ops->series_total = &_io_series_total;
	ops->extract_series = &_io_extract_series;
	ops->extract_total = &_io_extract_total;
	return ops;
}


// ============================================================================
// Routines supporting Network Data type
// ============================================================================

static int _network_dataset_size(void)
{
	return sizeof(profile_network_t);
}

static hid_t _network_create_memory_datatype(void)
{
	hid_t   mtyp_network = H5Tcreate(H5T_COMPOUND,
					 sizeof(profile_network_t));
	if (mtyp_network < 0) {
		debug3("PROFILE: failed to create Network memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtyp_network, "Date_Time", profile_network_t, tod);
	MEM_ADD_UINT64(mtyp_network, "Time", profile_network_t, time);
	MEM_ADD_UINT64(mtyp_network, "Packets_In",
		       profile_network_t, packets_in);
	MEM_ADD_DBL(mtyp_network, "Megabytes_In", profile_network_t, size_in);
	MEM_ADD_UINT64(mtyp_network, "Packets_Out",
		       profile_network_t, packets_out);
	MEM_ADD_DBL(mtyp_network, "Megabytes_Out", profile_network_t, size_out);

	return mtyp_network;
}

static hid_t _network_create_file_datatype(void)
{
	hid_t   ftyp_network = H5Tcreate(H5T_COMPOUND, TOD_LEN+5*8);
	if (ftyp_network < 0) {
		debug3("PROFILE: failed to create Network file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftyp_network, "Date_Time", 0);
	FILE_ADD_UINT64(ftyp_network, "Time");
	FILE_ADD_UINT64(ftyp_network, "Packets_In");
	FILE_ADD_DBL(ftyp_network, "Megabytes_In");
	FILE_ADD_UINT64(ftyp_network, "Packets_Out");
	FILE_ADD_DBL(ftyp_network, "Megabytes_Out");

	return ftyp_network;
}

static hid_t _network_s_create_memory_datatype(void)
{
	hid_t   mtyp_network = -1;

	mtyp_network = H5Tcreate(H5T_COMPOUND, sizeof(profile_network_s_t));
	if (mtyp_network < 0) {
		debug3("PROFILE: failed to create Network memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtyp_network, "Start Time", profile_network_s_t,
			  start_time);
	MEM_ADD_UINT64(mtyp_network, "Elapsed Time", profile_network_s_t,
		       elapsed_time);
	MEM_ADD_UINT64(mtyp_network, "Min Packets In", profile_network_s_t,
		       packets_in.min);
	MEM_ADD_UINT64(mtyp_network, "Ave Packets In", profile_network_s_t,
		       packets_in.ave);
	MEM_ADD_UINT64(mtyp_network, "Max Packets In", profile_network_s_t,
		       packets_in.max);
	MEM_ADD_UINT64(mtyp_network, "Total Packets In", profile_network_s_t,
		       packets_in.total);
	MEM_ADD_DBL(mtyp_network, "Min Megabytes In", profile_network_s_t,
		    size_in.min);
	MEM_ADD_DBL(mtyp_network, "Ave Megabytes In", profile_network_s_t,
		    size_in.ave);
	MEM_ADD_DBL(mtyp_network, "Max Megabytes In", profile_network_s_t,
		    size_in.max);
	MEM_ADD_DBL(mtyp_network, "Total Megabytes In", profile_network_s_t,
		    size_in.total);
	MEM_ADD_UINT64(mtyp_network, "Min Packets Out", profile_network_s_t,
		       packets_out.min);
	MEM_ADD_UINT64(mtyp_network, "Ave Packets Out", profile_network_s_t,
		       packets_out.ave);
	MEM_ADD_UINT64(mtyp_network, "Max Packets Out", profile_network_s_t,
		       packets_out.max);
	MEM_ADD_UINT64(mtyp_network, "Total Packets Out", profile_network_s_t,
		       packets_out.total);
	MEM_ADD_DBL(mtyp_network, "Min Megabytes Out", profile_network_s_t,
		    size_out.min);
	MEM_ADD_DBL(mtyp_network, "Ave Megabytes Out", profile_network_s_t,
		    size_out.ave);
	MEM_ADD_DBL(mtyp_network, "Max Megabytes Out", profile_network_s_t,
		    size_out.max);
	MEM_ADD_DBL(mtyp_network, "Total Megabytes Out", profile_network_s_t,
		    size_out.total);

	return mtyp_network;
}

static hid_t _network_s_create_file_datatype(void)
{
	hid_t   ftyp_network = H5Tcreate(H5T_COMPOUND, TOD_LEN+17*8);
	if (ftyp_network < 0) {
		debug3("PROFILE: failed to create Network file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftyp_network, "Start Time", 0);
	FILE_ADD_UINT64(ftyp_network, "Elapsed Time");
	FILE_ADD_UINT64(ftyp_network, "Min Packets In");
	FILE_ADD_UINT64(ftyp_network, "Ave Packets In");
	FILE_ADD_UINT64(ftyp_network, "Max Packets In");
	FILE_ADD_UINT64(ftyp_network, "Total Packets In");
	FILE_ADD_DBL(ftyp_network, "Min Megabytes In");
	FILE_ADD_DBL(ftyp_network, "Ave Megabytes In");
	FILE_ADD_DBL(ftyp_network, "Max Megabytes In");
	FILE_ADD_DBL(ftyp_network, "Total Megabytes In");
	FILE_ADD_UINT64(ftyp_network, "Min Packets Out");
	FILE_ADD_UINT64(ftyp_network, "Ave Packets Out");
	FILE_ADD_UINT64(ftyp_network, "Max Packets Out");
	FILE_ADD_UINT64(ftyp_network, "Total Packets Out");
	FILE_ADD_DBL(ftyp_network, "Min Megabytes Out");
	FILE_ADD_DBL(ftyp_network, "Ave Megabytes Out");
	FILE_ADD_DBL(ftyp_network, "Max Megabytes Out");
	FILE_ADD_DBL(ftyp_network, "Total Megabytes Out");

	return ftyp_network;
}

static void *_network_init_job_series(int n_samples)
{
	profile_network_t*  network_data;

	network_data = xmalloc(n_samples * sizeof(profile_network_t));
	if (network_data == NULL) {
		debug3("PROFILE: failed to get memory for network data");
		return NULL;
	}
	return (void*) network_data;
}

static char** _network_get_series_tod(void* data, int nsmp)
{
	int ix;
	char      **tod_values = NULL;
	profile_network_t* network_series = (profile_network_t*) data;
	tod_values = (char**) xmalloc(nsmp*sizeof(char*));
	if (tod_values == NULL) {
		info("Failed to get memory for network tod");
		return NULL;
	}
	for (ix=0; ix < nsmp; ix++) {
		tod_values[ix] = xstrdup(network_series[ix].tod);
	}
	return tod_values;
}

static double* _network_get_series_values(char* data_name, void* data, int nsmp)
{
	int ix;
	profile_network_t* network_series = (profile_network_t*) data;
	double  *network_values = NULL;
	network_values = xmalloc(nsmp*sizeof(double));
	if (network_values == NULL) {
		info("PROFILE: Failed to get memory for network data");
		return NULL;
	}
	if (strcasecmp(data_name,"Time") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			network_values[ix] = (double) network_series[ix].time;

		}
		return network_values;
	} else if (strcasecmp(data_name,"Packets_In") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			network_values[ix] =
					(double) network_series[ix].packets_in;

		}
		return network_values;
	} else if (strcasecmp(data_name,"Megabytes_In") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			network_values[ix] = network_series[ix].size_in;

		}
		return network_values;
	} else if (strcasecmp(data_name,"Packets_Out") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			network_values[ix] =
					(double) network_series[ix].packets_out;

		}
		return network_values;
	} else if (strcasecmp(data_name,"Megabytes_Out") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			network_values[ix] = network_series[ix].size_out;

		}
		return network_values;
	}
	xfree(network_values);
	info("PROFILE: %s is invalid data item for network data", data_name);
	return NULL;
}

static void _network_merge_step_series(
	hid_t group, void *prior, void *cur, void *buf)
{
// This is a difference series
	profile_network_t* prf_cur = (profile_network_t*) cur;
	profile_network_t* prf_buf = (profile_network_t*) buf;
	struct tm *ts = localtime(&prf_cur->time);
	strftime(prf_buf->tod, TOD_LEN, TOD_FMT, ts);
	if (prior == NULL) {
		// First sample.
		seriesStart = prf_cur->time;
		prf_buf->time = 0;
	} else {
		prf_buf->time = prf_cur->time - seriesStart;
	}
	prf_buf->packets_in = prf_cur->packets_in;
	prf_buf->packets_out = prf_cur->packets_out;
	prf_buf->size_in = prf_cur->size_in;
	prf_buf->size_out = prf_cur->size_out;
	return;
}

static void *_network_series_total(int n_samples, void *data)
{
	profile_network_t* network_data;
	profile_network_s_t* total;
	if (n_samples < 1)
		return NULL;
	network_data = (profile_network_t*) data;
	total = xmalloc(sizeof(profile_network_s_t));
	if (total == NULL) {
		error("PROFILE: Out of memory getting network total");
		return NULL;
	}
	// Assuming network series are a running total, and the first
	// sample just sets the initial conditions
	strcpy(total->start_time, network_data[0].tod);
	total->elapsed_time = network_data[n_samples-1].time;
	INCR_DIF_SAMPLE(total, network_data, packets_in, n_samples);
	INCR_DIF_SAMPLE(total, network_data, size_in, n_samples);
	INCR_DIF_SAMPLE(total, network_data, packets_out, n_samples);
	INCR_DIF_SAMPLE(total, network_data, size_out, n_samples);
	return total;
}

static void _network_extract_series(
	FILE* fp, bool put_header, int job, int step,
	char *node, char *series, void *data, int size_data)
{
	int n_items, ix;
	profile_network_t* network_data = (profile_network_t*) data;

	if (put_header) {
		fprintf(fp,"Job,Step,Node,Series,Date_Time,Elapsed_time,"
			"Packets_In,MegaBytes_In,Packets_Out,MegaBytes_Out\n");
	}
	n_items = size_data / sizeof(profile_network_t);
	for (ix=0; ix < n_items; ix++) {
		fprintf(fp,"%d,%d,%s,%s,%s,%ld,%ld,%.3f,%ld,%.3f\n",
			job, step, node,series,
			network_data[ix].tod, network_data[ix].time,
			network_data[ix].packets_in, network_data[ix].size_in,
			network_data[ix].packets_out,
			network_data[ix].size_out);
	}
	return;
}

static void _network_extract_total(
	FILE* fp, bool put_header, int job, int step,
	char *node, char *series, void *data, int size_data)
{
	profile_network_s_t* network_data = (profile_network_s_t*) data;
	if (put_header) {
		fprintf(fp,"Job,Step,Node,Series,Start_Time,Elapsed_time,"
			"Min_Packets_In,Ave_Packets_In,"
			"Max_Packets_In,Total_Packets_In,"
			"Min_Megabytes_In,Ave_Megabytes_In,"
			"Max_Megabytes_In,Total_Megabytes_In,"
			"Min_Packets_Out,Ave_Packets_Out,"
			"Max_Packets_Out,Total_Packets_Out,"
			"Min_Megabytes_Out,Ave_Megabytes_Out,"
			"Max_Megabytes_Out,Total_Megabytes_Out\n");
	}
	fprintf(fp, "%d,%d,%s,%s,%s,%ld", job, step, node, series,
		network_data->start_time, network_data->elapsed_time);
	PUT_UINT_SUM(fp, network_data->packets_in, ",");
	PUT_DBL_SUM(fp, network_data->size_in, ",");
	PUT_UINT_SUM(fp, network_data->packets_out, ",");
	PUT_DBL_SUM(fp, network_data->size_out, ",");
	fprintf(fp, "\n");
	return;
}

static hdf5_api_ops_t *_network_profile_factory(void)
{
	hdf5_api_ops_t* ops = xmalloc(sizeof(hdf5_api_ops_t));
	ops->dataset_size = &_network_dataset_size;
	ops->create_memory_datatype = &_network_create_memory_datatype;
	ops->create_file_datatype = &_network_create_file_datatype;
	ops->create_s_memory_datatype = &_network_s_create_memory_datatype;
	ops->create_s_file_datatype = &_network_s_create_file_datatype;
	ops->init_job_series = &_network_init_job_series;
	ops->get_series_tod = &_network_get_series_tod;
	ops->get_series_values = &_network_get_series_values;
	ops->merge_step_series = &_network_merge_step_series;
	ops->series_total = &_network_series_total;
	ops->extract_series = &_network_extract_series;
	ops->extract_total = &_network_extract_total;
	return ops;
}

// ============================================================================
// Routines supporting Task Data type
// ============================================================================

static int _task_dataset_size(void)
{
	return sizeof(profile_task_t);
}

static hid_t _task_create_memory_datatype(void)
{
	hid_t   mtyp_task = H5Tcreate(H5T_COMPOUND, sizeof(profile_task_t));
	if (mtyp_task < 0) {
		debug3("PROFILE: failed to create Task memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtyp_task, "Date_Time", profile_task_t, tod);
	MEM_ADD_UINT64(mtyp_task, "Time", profile_task_t, time);
	MEM_ADD_UINT64(mtyp_task, "CPU_Frequency", profile_task_t, cpu_freq);
	MEM_ADD_UINT64(mtyp_task, "CPU_Time", profile_task_t, cpu_time);
	MEM_ADD_DBL(mtyp_task, "CPU_Utilization",
		    profile_task_t, cpu_utilization);
	MEM_ADD_UINT64(mtyp_task, "RSS", profile_task_t, rss);
	MEM_ADD_UINT64(mtyp_task, "VM_Size", profile_task_t, vm_size);
	MEM_ADD_UINT64(mtyp_task, "Pages", profile_task_t, pages);
	MEM_ADD_DBL(mtyp_task, "Read_Megabytes", profile_task_t, read_size);
	MEM_ADD_DBL(mtyp_task, "Write_Megabytes", profile_task_t, write_size);

	return mtyp_task;
}

static hid_t _task_create_file_datatype(void)
{
	hid_t   ftyp_task = H5Tcreate(H5T_COMPOUND, TOD_LEN+9*8);
	if (ftyp_task < 0) {
		debug3("PROFILE: failed to create Task file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftyp_task, "Date_Time", 0);
	FILE_ADD_UINT64(ftyp_task, "Time");
	FILE_ADD_UINT64(ftyp_task, "CPU_Frequency");
	FILE_ADD_UINT64(ftyp_task, "CPU_Time");
	FILE_ADD_DBL(ftyp_task, "CPU_Utilization");
	FILE_ADD_UINT64(ftyp_task, "RSS");
	FILE_ADD_UINT64(ftyp_task, "VM_Size");
	FILE_ADD_UINT64(ftyp_task, "Pages");
	FILE_ADD_DBL(ftyp_task, "Read_Megabytes");
	FILE_ADD_DBL(ftyp_task, "Write_Megabytes");

	return ftyp_task;
}

static hid_t _task_s_create_memory_datatype(void)
{
	hid_t   mtyp_task = H5Tcreate(H5T_COMPOUND, sizeof(profile_task_s_t));
	if (mtyp_task < 0) {
		debug3("PROFILE: failed to create Task memory datatype");
		return -1;
	}
	MEM_ADD_DATE_TIME(mtyp_task, "Start Time", profile_task_s_t,
			  start_time);
	MEM_ADD_UINT64(mtyp_task, "Elapsed Time", profile_task_s_t,
		       elapsed_time);
	MEM_ADD_UINT64(mtyp_task, "Min CPU Frequency", profile_task_s_t,
		       cpu_freq.min);
	MEM_ADD_UINT64(mtyp_task, "Ave CPU Frequency", profile_task_s_t,
		       cpu_freq.ave);
	MEM_ADD_UINT64(mtyp_task, "Max CPU Frequency", profile_task_s_t,
		       cpu_freq.max);
	MEM_ADD_UINT64(mtyp_task, "Total CPU Frequency", profile_task_s_t,
		       cpu_freq.total);
	MEM_ADD_UINT64(mtyp_task, "Min CPU Time", profile_task_s_t,
		       cpu_time.min);
	MEM_ADD_UINT64(mtyp_task, "Ave CPU Time", profile_task_s_t,
		       cpu_time.ave);
	MEM_ADD_UINT64(mtyp_task, "Max CPU Time", profile_task_s_t,
		       cpu_time.max);
	MEM_ADD_UINT64(mtyp_task, "Total CPU Time", profile_task_s_t,
		       cpu_time.total);
	MEM_ADD_DBL(mtyp_task, "Min CPU Utilization", profile_task_s_t,
		    cpu_utilization.min);
	MEM_ADD_DBL(mtyp_task, "Ave CPU Utilization", profile_task_s_t,
		    cpu_utilization.ave);
	MEM_ADD_DBL(mtyp_task, "Max CPU Utilization", profile_task_s_t,
		    cpu_utilization.max);
	MEM_ADD_DBL(mtyp_task, "Total CPU Utilization", profile_task_s_t,
		    cpu_utilization.total);
	MEM_ADD_UINT64(mtyp_task, "Min RSS", profile_task_s_t, rss.min);
	MEM_ADD_UINT64(mtyp_task, "Ave RSS", profile_task_s_t, rss.ave);
	MEM_ADD_UINT64(mtyp_task, "Max RSS", profile_task_s_t, rss.max);
	MEM_ADD_UINT64(mtyp_task, "Total RSS", profile_task_s_t, rss.total);
	MEM_ADD_UINT64(mtyp_task, "Min VM Size", profile_task_s_t, vm_size.min);
	MEM_ADD_UINT64(mtyp_task, "Ave VM Size", profile_task_s_t, vm_size.ave);
	MEM_ADD_UINT64(mtyp_task, "Max VM Size", profile_task_s_t, vm_size.max);
	MEM_ADD_UINT64(mtyp_task, "Total VM Size",
		       profile_task_s_t, vm_size.total);
	MEM_ADD_UINT64(mtyp_task, "Min Pages", profile_task_s_t, pages.min);
	MEM_ADD_UINT64(mtyp_task, "Ave Pages", profile_task_s_t, pages.ave);
	MEM_ADD_UINT64(mtyp_task, "Max Pages", profile_task_s_t, pages.max);
	MEM_ADD_UINT64(mtyp_task, "Total Pages", profile_task_s_t, pages.total);
	MEM_ADD_DBL(mtyp_task, "Min Read Megabytes", profile_task_s_t,
		    read_size.min);
	MEM_ADD_DBL(mtyp_task, "Ave Read Megabytes", profile_task_s_t,
		    read_size.ave);
	MEM_ADD_DBL(mtyp_task, "Max Read Megabytes", profile_task_s_t,
		    read_size.max);
	MEM_ADD_DBL(mtyp_task, "Total Read Megabytes", profile_task_s_t,
		    read_size.total);
	MEM_ADD_DBL(mtyp_task, "Min Write Megabytes", profile_task_s_t,
		    write_size.min);
	MEM_ADD_DBL(mtyp_task, "Ave Write Megabytes", profile_task_s_t,
		    write_size.ave);
	MEM_ADD_DBL(mtyp_task, "Max Write Megabytes", profile_task_s_t,
		    write_size.max);
	MEM_ADD_DBL(mtyp_task, "Total Write Megabytes", profile_task_s_t,
		    write_size.total);

	return mtyp_task;
}

static hid_t _task_s_create_file_datatype(void)
{
	hid_t   ftyp_task = H5Tcreate(H5T_COMPOUND, TOD_LEN+33*8);
	if (ftyp_task < 0) {
		debug3("PROFILE: failed to create Task file datatype");
		return -1;
	}
	moffset = TOD_LEN;
	FILE_ADD_DATE_TIME(ftyp_task, "Start Time", 0);
	FILE_ADD_UINT64(ftyp_task, "Elapsed Time");
	FILE_ADD_UINT64(ftyp_task, "Min CPU Frequency");
	FILE_ADD_UINT64(ftyp_task, "Ave CPU Frequency");
	FILE_ADD_UINT64(ftyp_task, "Max CPU Frequency");
	FILE_ADD_UINT64(ftyp_task, "Total CPU Frequency");
	FILE_ADD_UINT64(ftyp_task, "Min CPU Time");
	FILE_ADD_UINT64(ftyp_task, "Ave CPU Time");
	FILE_ADD_UINT64(ftyp_task, "Max CPU Time");
	FILE_ADD_UINT64(ftyp_task, "Total CPU Time");
	FILE_ADD_DBL(ftyp_task, "Min CPU Utilization");
	FILE_ADD_DBL(ftyp_task, "Ave CPU Utilization");
	FILE_ADD_DBL(ftyp_task, "Max CPU Utilization");
	FILE_ADD_DBL(ftyp_task, "Total CPU Utilization");
	FILE_ADD_UINT64(ftyp_task, "Min RSS");
	FILE_ADD_UINT64(ftyp_task, "Ave RSS");
	FILE_ADD_UINT64(ftyp_task, "Max RSS");
	FILE_ADD_UINT64(ftyp_task, "Total RSS");
	FILE_ADD_UINT64(ftyp_task, "Min VM Size");
	FILE_ADD_UINT64(ftyp_task, "Ave VM Size");
	FILE_ADD_UINT64(ftyp_task, "Max VM Size");
	FILE_ADD_UINT64(ftyp_task, "Total VM Size");
	FILE_ADD_UINT64(ftyp_task, "Min Pages");
	FILE_ADD_UINT64(ftyp_task, "Ave Pages");
	FILE_ADD_UINT64(ftyp_task, "Max Pages");
	FILE_ADD_UINT64(ftyp_task, "Total Pages");
	FILE_ADD_DBL(ftyp_task, "Min Read Megabytes");
	FILE_ADD_DBL(ftyp_task, "Ave Read Megabytes");
	FILE_ADD_DBL(ftyp_task, "Max Read Megabytes");
	FILE_ADD_DBL(ftyp_task, "Total Read Megabytes");
	FILE_ADD_DBL(ftyp_task, "Min Write Megabytes");
	FILE_ADD_DBL(ftyp_task, "Ave Write Megabytes");
	FILE_ADD_DBL(ftyp_task, "Max Write Megabytes");
	FILE_ADD_DBL(ftyp_task, "Total Write Megabytes");

	return ftyp_task;
}

static void *_task_init_job_series(int n_samples)
{
	profile_task_t*  task_data;
	task_data = xmalloc(n_samples * sizeof(profile_task_t));
	if (task_data == NULL) {
		debug3("PROFILE: failed to get memory for combined task data");
		return NULL;
	}
	return (void*) task_data;
}

static char** _task_get_series_tod(void* data, int nsmp)
{
	int ix;
	char      **tod_values = NULL;
	profile_task_t* task_series = (profile_task_t*) data;
	tod_values = (char**) xmalloc(nsmp*sizeof(char*));
	if (tod_values == NULL) {
		info("Failed to get memory for task tod");
		return NULL;
	}
	for (ix=0; ix < nsmp; ix++) {
		tod_values[ix] = xstrdup(task_series[ix].tod);
	}
	return tod_values;
}

static double* _task_get_series_values(char* data_name, void* data, int nsmp)
{
	int ix;
	profile_task_t* task_series = (profile_task_t*) data;
	double  *task_values = NULL;
	task_values = xmalloc(nsmp*sizeof(double));
	if (task_values == NULL) {
		info("PROFILE: Failed to get memory for task data");
		return NULL;
	}
	if (strcasecmp(data_name,"Time") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			task_values[ix] = (double) task_series[ix].time;

		}
		return task_values;
	} else if (strcasecmp(data_name,"CPU_Frequency") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			task_values[ix] = (double) task_series[ix].cpu_freq;

		}
		return task_values;
	} else if (strcasecmp(data_name,"CPU_Time") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			task_values[ix] = (double) task_series[ix].cpu_time;

		}
		return task_values;
	} else if (strcasecmp(data_name,"CPU_Utilization") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			task_values[ix] = task_series[ix].cpu_utilization;

		}
		return task_values;
	} else if (strcasecmp(data_name,"RSS") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			task_values[ix] = (double) task_series[ix].rss;

		}
		return task_values;
	} else if (strcasecmp(data_name,"VM_Size") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			task_values[ix] = (double) task_series[ix].vm_size;

		}
		return task_values;
	} else if (strcasecmp(data_name,"Pages") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			task_values[ix] = (double) task_series[ix].pages;

		}
		return task_values;
	} else if (strcasecmp(data_name,"Read_Megabytes") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			task_values[ix] = task_series[ix].read_size;

		}
		return task_values;
	} else if (strcasecmp(data_name,"Write_Megabytes") == 0) {
		for (ix=0; ix < nsmp; ix++) {
			task_values[ix] = task_series[ix].write_size;

		}
		return task_values;
	}
	xfree(task_values);
	info("PROFILE: %s is invalid data item for task data", data_name);
	return NULL;
}

static void _task_merge_step_series(
	hid_t group, void *prior, void *cur, void *buf)
{
// This is a running total series
	profile_task_t* prf_prior = (profile_task_t*) prior;
	profile_task_t* prf_cur = (profile_task_t*) cur;
	profile_task_t* buf_prv = NULL;
	profile_task_t* buf_cur = (profile_task_t*) buf;

	struct tm *ts;
	ts = localtime(&prf_cur->time);
	strftime(buf_cur->tod, TOD_LEN, TOD_FMT, ts);
	if (prf_prior == NULL) {
		// First sample.
		seriesStart = prf_cur->time;
		buf_cur->time = 0;
		buf_cur->cpu_time = 0;
		buf_cur->cpu_utilization = 0;
		buf_cur->read_size = 0.0;
		buf_cur->write_size = 0.0;
	} else {
		buf_prv = buf_cur - 1;
		buf_cur->time = prf_cur->time - seriesStart;
		buf_cur->cpu_time = prf_cur->cpu_time - prf_prior->cpu_time;
		buf_cur->cpu_utilization = 100.0*((double) buf_cur->cpu_time /
				(double) (buf_cur->time - buf_prv->time));
		buf_cur->read_size =
			prf_cur->read_size - prf_prior->read_size;
		buf_cur->write_size =
			prf_cur->write_size - prf_prior->write_size;
	}
	buf_cur->cpu_freq = prf_cur->cpu_freq;
	buf_cur->rss = prf_cur->rss;
	buf_cur->vm_size = prf_cur->vm_size;
	buf_cur->pages = prf_cur->pages;
	return;
}

static void *_task_series_total(int n_samples, void *data)
{
	profile_task_t* task_data;
	profile_task_s_t* total;
	task_data = (profile_task_t*) data;
	total = xmalloc(sizeof(profile_task_s_t));
	if (total == NULL) {
		error("PROFILE: Out of memory getting task total");
		return NULL;
	}
	strcpy(total->start_time, task_data[0].tod);
	total->elapsed_time = task_data[n_samples-1].time;
	INCR_DIF_SAMPLE(total, task_data, cpu_freq, n_samples);
	INCR_RT_SAMPLE(total, task_data, cpu_time, n_samples);
	INCR_DIF_SAMPLE(total, task_data, cpu_utilization, n_samples);
	INCR_DIF_SAMPLE(total, task_data, rss, n_samples);
	INCR_DIF_SAMPLE(total, task_data, vm_size , n_samples);
	INCR_DIF_SAMPLE(total, task_data, pages, n_samples);
	INCR_RT_SAMPLE(total, task_data, read_size, n_samples);
	INCR_RT_SAMPLE(total, task_data, write_size, n_samples);
	return total;
}

static void _task_extract_series(
	FILE* fp, bool put_header, int job, int step,
	char *node, char *series, void *data, int size_data)
{
	int n_items, ix;
	profile_task_t* task_data = (profile_task_t*) data;
	if (put_header) {
		fprintf(fp,"Job,Step,Node,Series,Date Time,ElapsedTime,"
			"CPU Frequency,CPU Time,"
			"CPU Utilization,rss,VM Size,Pages,"
			"Read_bytes,Write_bytes\n");
	}
	n_items = size_data / sizeof(profile_task_t);
	for (ix=0; ix < n_items; ix++) {
		fprintf(fp,"%d,%d,%s,%s,%s,%ld,%ld,%ld,%.3f",
			job, step, node, series,
			task_data[ix].tod, task_data[ix].time,
			task_data[ix].cpu_freq,
			task_data[ix].cpu_time, task_data[ix].cpu_utilization);
		fprintf(fp,",%ld,%ld,%ld,%.3f,%.3f\n",	task_data[ix].rss,
			task_data[ix].vm_size, task_data[ix].pages,
			task_data[ix].read_size, task_data[ix].write_size);
	}
	return;
}

static void _task_extract_total(
	FILE* fp, bool put_header, int job, int step,
	char *node, char *series, void *data, int size_data)
{

	profile_task_s_t* task_data = (profile_task_s_t*) data;
	if (put_header) {
		fprintf(fp,"Job,Step,Node,Series,Start_Time,Elapsed_time,"
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
	fprintf(fp, "%d,%d,%s,%s,%s,%ld", job, step, node, series,
		task_data->start_time, task_data->elapsed_time);
	PUT_UINT_SUM(fp, task_data->cpu_freq, ",");
	PUT_UINT_SUM(fp, task_data->cpu_time, ",");
	PUT_DBL_SUM(fp, task_data->cpu_utilization, ",");
	PUT_UINT_SUM(fp, task_data->rss, ",");
	PUT_UINT_SUM(fp, task_data->vm_size, ",");
	PUT_UINT_SUM(fp, task_data->pages, ",");
	PUT_DBL_SUM(fp, task_data->read_size, ",");
	PUT_DBL_SUM(fp, task_data->write_size, ",");
	fprintf(fp, "\n");
	return;
}

static hdf5_api_ops_t *_task_profile_factory(void)
{
	hdf5_api_ops_t* ops = xmalloc(sizeof(hdf5_api_ops_t));
	ops->dataset_size = &_task_dataset_size;
	ops->create_memory_datatype = &_task_create_memory_datatype;
	ops->create_file_datatype = &_task_create_file_datatype;
	ops->create_s_memory_datatype = &_task_s_create_memory_datatype;
	ops->create_s_file_datatype = &_task_s_create_file_datatype;
	ops->init_job_series = &_task_init_job_series;
	ops->get_series_tod = &_task_get_series_tod;
	ops->get_series_values = &_task_get_series_values;
	ops->merge_step_series = &_task_merge_step_series;
	ops->series_total = &_task_series_total;
	ops->extract_series = &_task_extract_series;
	ops->extract_total = &_task_extract_total;
	return ops;
}

/* ============================================================================
 * Common support functions
 ===========================================================================*/

extern hdf5_api_ops_t* profile_factory(uint32_t type)
{
	switch (type) {
	case ACCT_GATHER_PROFILE_ENERGY:
		return _energy_profile_factory();
		break;
	case ACCT_GATHER_PROFILE_TASK:
		return _task_profile_factory();
		break;
	case ACCT_GATHER_PROFILE_LUSTRE:
		return _io_profile_factory();
		break;
	case ACCT_GATHER_PROFILE_NETWORK:
		return _network_profile_factory();
		break;
	default:
		error("profile_factory: Unknown type %d sent", type);
		return NULL;
	}
}


extern void profile_init(void)
{
	typTOD = H5Tcopy (H5T_C_S1);
	H5Tset_size (typTOD, TOD_LEN); /* create string of length TOD_LEN */

	return;
}

extern void profile_fini(void)
{
	H5Tclose(typTOD);
	H5close(); /* make sure all H5 Objects are closed */

	return;
}

extern char *get_data_set_name(char *type)
{
	static char  dset_name[MAX_DATASET_NAME+1];
	dset_name[0] = '\0';
	sprintf(dset_name, "%s Data", type);

	return dset_name;
}


static char* _H5O_type_t2str(H5O_type_t type)
{
	switch (type)
	{
	case H5O_TYPE_UNKNOWN:
		return "H5O_TYPE_UNKNOWN";
	case H5O_TYPE_GROUP:
		return "H5O_TYPE_GROUP";
	case H5O_TYPE_DATASET:
		return "H5O_TYPE_DATASET";
	case H5O_TYPE_NAMED_DATATYPE:
		return "H5O_TYPE_NAMED_DATATYPE";
	case H5O_TYPE_NTYPES:
		return "H5O_TYPE_NTYPES";
	default:
		return "Invalid H5O_TYPE";
	}
}


extern void hdf5_obj_info(hid_t group, char *nam_group)
{
	char buf[MAX_GROUP_NAME+1];
	hsize_t nobj, nattr;
	hid_t aid;
	int i, len;
	H5G_info_t group_info;
	H5O_info_t object_info;

	if (group < 0) {
		info("PROFILE: Group is not HDF5 object");
		return;
	}
	H5Gget_info(group, &group_info);
	nobj = group_info.nlinks;
	H5Oget_info(group, &object_info);
	nattr = object_info.num_attrs;
	info("PROFILE group: %s NumObject=%d NumAttributes=%d",
	     nam_group, (int) nobj, (int) nattr);
	for (i = 0; (nobj>0) && (i<nobj); i++) {
		H5Oget_info_by_idx(group, ".", H5_INDEX_NAME, H5_ITER_INC, i,
				   &object_info, H5P_DEFAULT);
		len = H5Lget_name_by_idx(group, ".", H5_INDEX_NAME,
					 H5_ITER_INC, i, buf, MAX_GROUP_NAME,
					 H5P_DEFAULT);
		if ((len > 0) && (len < MAX_GROUP_NAME)) {
			info("PROFILE: Obj=%d Type=%s Name=%s",
			     i, _H5O_type_t2str(object_info.type), buf);
		} else {
			info("PROFILE: Obj=%d Type=%s Name=%s (is truncated)",
			     i, _H5O_type_t2str(object_info.type), buf);
		}
	}
	for (i = 0; (nattr>0) && (i<nattr); i++) {
		aid = H5Aopen_by_idx(group, ".", H5_INDEX_NAME, H5_ITER_INC,
				     i, H5P_DEFAULT, H5P_DEFAULT);
		// Get the name of the attribute.
		len = H5Aget_name(aid, MAX_ATTR_NAME, buf);
		if (len < MAX_ATTR_NAME) {
			info("PROFILE: Attr=%d Name=%s", i, buf);
		} else {
			info("PROFILE: Attr=%d Name=%s (is truncated)", i, buf);
		}
		H5Aclose(aid);
	}

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
			if (strcmp(buf, name) == 0) {
				return aid;
			}
		}
		H5Aclose(aid);
	}
	debug3("PROFILE: failed to find HDF5 attribute=%s\n", name);

	return -1;
}

extern hid_t get_group(hid_t parent, char *name)
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
			if (strcmp(buf, name) == 0) {
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

extern hid_t make_group(hid_t parent, char *name)
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

extern char *get_string_attribute(hid_t parent, char *name)
{
	char *value = NULL;

	hid_t   attr, type;
	size_t  size;

	attr = get_attribute_handle(parent, name);
	if (attr < 0) {
		debug3("PROFILE: Attribute=%s does not exist", name);
		return NULL;
	}
	type  = H5Aget_type(attr);
	if (H5Tget_class(type) != H5T_STRING) {
		H5Aclose(attr);
		debug3("PROFILE: Attribute=%s is not a string", name);
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
		debug3("PROFILE: failed to read attribute=%s", name);
		return NULL;
	}
	H5Tclose(type);
	H5Aclose(attr);

	return value;
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

extern int get_int_attribute(hid_t parent, char *name)
{
	int value = 0;

	hid_t   attr;
	attr = get_attribute_handle(parent, name);
	if (attr < 0) {
		debug3("PROFILE: Attribute=%s does not exist, returning", name);
		return value;
	}
	if (H5Aread(attr, H5T_NATIVE_INT, &value) < 0) {
		debug3("PROFILE: failed to read attribute=%s, returning", name);
	}
	H5Aclose(attr);

	return value;
}


extern void put_uint32_attribute(hid_t parent, char *name, uint32_t value)
{
	hid_t   attr, space_attr;
	hsize_t dim_attr[1] = {1}; // Single dimension array of values

	space_attr  = H5Screate_simple(1, dim_attr, NULL);
	if (space_attr < 0) {
		debug3("PROFILE: failed to create space for attribute %s",
		       name);
		return;
	}
	attr = H5Acreate(parent, name, H5T_NATIVE_UINT32, space_attr,
			 H5P_DEFAULT, H5P_DEFAULT);
	if (attr < 0) {
		H5Sclose(space_attr);
		debug3("PROFILE: failed to create attribute %s", name);
		return;
	}
	if (H5Awrite(attr, H5T_NATIVE_UINT32, &value) < 0) {
		debug3("PROFILE: failed to write attribute %s", name);
		// Fall through to release resources
	}
	H5Sclose(space_attr);
	H5Aclose(attr);

	return;
}

extern uint32_t get_uint32_attribute(hid_t parent, char *name)
{
	int value = 0;
	hid_t   attr;

	attr = get_attribute_handle(parent, name);
	if (attr < 0) {
		debug3("PROFILE: Attribute=%s does not exist, returning", name);
		return value;
	}
	if (H5Aread(attr, H5T_NATIVE_UINT32, &value) < 0) {
		debug3("PROFILE: failed to read attribute=%s, returning", name);
	}
	H5Aclose(attr);

	return value;
}

extern void *get_hdf5_data(hid_t parent, uint32_t type,
			   char *nam_group, int *size_data)
{
	void *  data = NULL;

	hid_t   id_data_set, dtyp_memory;
	hsize_t szDset;
	herr_t  ec;
	char *subtype = NULL;
	hdf5_api_ops_t* ops = profile_factory(type);
	char *type_name = acct_gather_profile_type_to_string(type);

	if (ops == NULL) {
		debug3("PROFILE: failed to create %s operations",
		       type_name);
		return NULL;
	}
	subtype = get_string_attribute(parent, ATTR_SUBDATATYPE);
	if (subtype < 0) {
		xfree(ops);
		debug3("PROFILE: failed to get %s attribute",
		       ATTR_SUBDATATYPE);
		return NULL;
	}
	id_data_set = H5Dopen(parent, get_data_set_name(nam_group),
			      H5P_DEFAULT);
	if (id_data_set < 0) {
		xfree(subtype);
		xfree(ops);
		debug3("PROFILE: failed to open %s Data Set",
		       type_name);
		return NULL;
	}
	if (strcmp(subtype, SUBDATA_SUMMARY))
		dtyp_memory = (*(ops->create_memory_datatype))();
	else
		dtyp_memory = (*(ops->create_s_memory_datatype))();
	xfree(subtype);
	if (dtyp_memory < 0) {
		H5Dclose(id_data_set);
		xfree(ops);
		debug3("PROFILE: failed to create %s memory datatype",
		       type_name);
		return NULL;
	}
	szDset = H5Dget_storage_size(id_data_set);
	*size_data = (int) szDset;
	if (szDset == 0) {
		H5Tclose(dtyp_memory);
		H5Dclose(id_data_set);
		xfree(ops);
		debug3("PROFILE: %s data set is empty",
		       type_name);
		return NULL;
	}
	data = xmalloc(szDset);
	if (data == NULL) {
		H5Tclose(dtyp_memory);
		H5Dclose(id_data_set);
		xfree(ops);
		debug3("PROFILE: failed to get memory for %s data set",
		       type_name);
		return NULL;
	}
	ec = H5Dread(id_data_set, dtyp_memory, H5S_ALL, H5S_ALL, H5P_DEFAULT,
		     data);
	if (ec < 0) {
		H5Tclose(dtyp_memory);
		H5Dclose(id_data_set);
		xfree(data);
		xfree(ops);
		debug3("PROFILE: failed to read %s data",
		       type_name);
		return NULL;
	}
	H5Tclose(dtyp_memory);
	H5Dclose(id_data_set);
	xfree(ops);

	return data;
}

extern void put_hdf5_data(hid_t parent, uint32_t type, char *subtype,
			  char *group, void *data, int n_item)
{
	hid_t   id_group, dtyp_memory, dtyp_file, id_data_space, id_data_set;
	hsize_t dims[1];
	herr_t  ec;
	hdf5_api_ops_t* ops = profile_factory(type);
	char *type_name = acct_gather_profile_type_to_string(type);

	if (ops == NULL) {
		debug3("PROFILE: failed to create %s operations",
		       type_name);
		return;
	}
	// Create the datatypes.
	if (strcmp(subtype, SUBDATA_SUMMARY)) {
		dtyp_memory = (*(ops->create_memory_datatype))();
		dtyp_file = (*(ops->create_file_datatype))();
	} else {
		dtyp_memory = (*(ops->create_s_memory_datatype))();
		dtyp_file = (*(ops->create_s_file_datatype))();
	}

	if (dtyp_memory < 0) {
		xfree(ops);
		debug3("PROFILE: failed to create %s memory datatype",
		       type_name);
		return;
	}

	if (dtyp_file < 0) {
		H5Tclose(dtyp_memory);
		xfree(ops);
		debug3("PROFILE: failed to create %s file datatype",
		       type_name);
		return;
	}

	dims[0] = n_item;
	id_data_space = H5Screate_simple(1, dims, NULL);
	if (id_data_space < 0) {
		H5Tclose(dtyp_file);
		H5Tclose(dtyp_memory);
		xfree(ops);
		debug3("PROFILE: failed to create %s space descriptor",
		       type_name);
		return;
	}

	id_group = H5Gcreate(parent, group, H5P_DEFAULT,
			     H5P_DEFAULT, H5P_DEFAULT);
	if (id_group < 0) {
		H5Sclose(id_data_space);
		H5Tclose(dtyp_file);
		H5Tclose(dtyp_memory);
		xfree(ops);
		debug3("PROFILE: failed to create %s group", group);
		return;
	}

	put_string_attribute(id_group, ATTR_DATATYPE, type_name);
	put_string_attribute(id_group, ATTR_SUBDATATYPE, subtype);

	id_data_set = H5Dcreate(id_group, get_data_set_name(group), dtyp_file,
				id_data_space, H5P_DEFAULT, H5P_DEFAULT,
				H5P_DEFAULT);
	if (id_data_set < 0) {
		H5Gclose(id_group);
		H5Sclose(id_data_space);
		H5Tclose(dtyp_file);
		H5Tclose(dtyp_memory);
		xfree(ops);
		debug3("PROFILE: failed to create %s dataset", group);
		return;
	}

	ec = H5Dwrite(id_data_set, dtyp_memory, H5S_ALL, H5S_ALL, H5P_DEFAULT,
		      data);
	if (ec < 0) {
		debug3("PROFILE: failed to create write task data");
		// Fall through to release resources
	}
	H5Dclose(id_data_set);
	H5Gclose(id_group);
	H5Sclose(id_data_space);
	H5Tclose(dtyp_file);
	H5Tclose(dtyp_memory);
	xfree(ops);


	return;
}

