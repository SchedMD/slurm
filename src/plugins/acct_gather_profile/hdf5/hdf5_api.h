/****************************************************************************\
 *  hdf5_api.h
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  Portions Copyright (C) 2013 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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
#ifndef __ACCT_GATHER_HDF5_API_H__
#define __ACCT_GATHER_HDF5_API_H__

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <stdlib.h>

#include <hdf5.h>
#include "src/common/slurm_acct_gather_profile.h"

#define MAX_PROFILE_PATH 1024
#define MAX_ATTR_NAME 64
#define MAX_GROUP_NAME 64
#define MAX_DATASET_NAME 64

#define ATTR_NODENAME "Node Name"
#define ATTR_STARTTIME "Start Time"
#define ATTR_NSTEPS "Number of Steps"
#define ATTR_NNODES "Number of Nodes"
#define ATTR_NTASKS "Number of Tasks"
#define ATTR_TASKID "Task Id"
#define ATTR_CPUPERTASK "CPUs per Task"
#define ATTR_DATATYPE "Data Type"
#define ATTR_SUBDATATYPE "Subdata Type"
#define ATTR_STARTTIME "Start Time"
#define ATTR_STARTSEC "Start Second"
#define SUBDATA_DATA "Data"
#define SUBDATA_NODE "Node"
#define SUBDATA_SAMPLE "Sample"
#define SUBDATA_SERIES "Series"
#define SUBDATA_TOTAL "Total"
#define SUBDATA_SUMMARY "Summary"

#define GRP_STEP "Step"
#define GRP_NODES "Nodes"
#define GRP_NODE "Node"
#define GRP_SAMPLES "Time Series"
#define GRP_SAMPLE "Sample"
#define GRP_TASKS "Tasks"
#define GRP_TASK "Task"
#define GRP_TOTALS "Totals"

// Data types supported by all HDF5 plugins of this type

#define TOD_LEN 24
#define TOD_FMT "%F %T"

/*
 * prof_uint_sum is a low level structure intended to hold the
 * minimum, average, maximum, and total values of a data item.
 * It is usually used in a summary data structure for an item
 * that occurs in a time series.
 */
typedef struct prof_uint_sum {
	uint64_t min;	// Minumum value
	uint64_t ave;	// Average value
	uint64_t max;	// Maximum value
	uint64_t total;	// Accumlated value
} prof_uint_sum_t;

// Save as prof_uint_sum, but for double precision items
typedef struct prof_dbl_sum {
	double	min;	// Minumum value
	double	ave;	// Average value
	double	max;	// Maximum value
	double	total;	// Accumlated value
} prof_dbl_sum_t;

#define PROFILE_ENERGY_DATA "Energy"
// energy data structures
//	node_step file
typedef struct profile_energy {
	char		tod[TOD_LEN];	// Not used in node-step
	time_t		time;
	uint64_t	power;
	uint64_t	cpu_freq;
} profile_energy_t;
//	summary data in job-node-totals
typedef struct profile_energy_s {
	char		start_time[TOD_LEN];
	uint64_t	elapsed_time;
	prof_uint_sum_t	power;
	prof_uint_sum_t cpu_freq;
} profile_energy_s_t; // series summary

#define PROFILE_IO_DATA "I/O"
// io data structure
//	node_step file
typedef struct profile_io {
	char		tod[TOD_LEN];	// Not used in node-step
	time_t		time;
	uint64_t	reads;
	double		read_size;	// currently in megabytes
	uint64_t	writes;
	double		write_size;	// currently in megabytes
} profile_io_t;
//	summary data in job-node-totals
typedef struct profile_io_s {
	char		start_time[TOD_LEN];
	uint64_t	elapsed_time;
	prof_uint_sum_t	reads;
	prof_dbl_sum_t	read_size;	// currently in megabytes
	prof_uint_sum_t	writes;
	prof_dbl_sum_t	write_size;	// currently in megabytes
} profile_io_s_t;

#define PROFILE_NETWORK_DATA "Network"
// Network data structure
//	node_step file
typedef struct profile_network {
	char		tod[TOD_LEN];	// Not used in node-step
	time_t		time;
	uint64_t	packets_in;
	double		size_in;	// currently in megabytes
	uint64_t	packets_out;
	double		size_out;	// currently in megabytes
} profile_network_t;
//	summary data in job-node-totals
typedef struct profile_network_s {
	char		start_time[TOD_LEN];
	uint64_t	elapsed_time;
	prof_uint_sum_t packets_in;
	prof_dbl_sum_t  size_in;		// currently in megabytes
	prof_uint_sum_t packets_out;
	prof_dbl_sum_t  size_out;	// currently in megabytes
} profile_network_s_t;

#define PROFILE_TASK_DATA "Task"
// task data structure
//	node_step file
typedef struct profile_task {
	char		tod[TOD_LEN];	// Not used in node-step
	time_t		time;
	uint64_t	cpu_freq;
	uint64_t	cpu_time;
	double		cpu_utilization;
	uint64_t	rss;
	uint64_t	vm_size;
	uint64_t	pages;
	double	 	read_size;	// currently in megabytes
	double	 	write_size;	// currently in megabytes
} profile_task_t;
//	summary data in job-node-totals
typedef struct profile_task_s {
	char		start_time[TOD_LEN];
	uint64_t	elapsed_time;
	prof_uint_sum_t	cpu_freq;
	prof_uint_sum_t cpu_time;
	prof_dbl_sum_t 	cpu_utilization;
	prof_uint_sum_t rss;
	prof_uint_sum_t vm_size;
	prof_uint_sum_t pages;
	prof_dbl_sum_t 	read_size;	// currently in megabytes
	prof_dbl_sum_t 	write_size;	// currently in megabytes
} profile_task_s_t;

/*
 * Structure of function pointers of common operations on a profile data type.
 *	dataset_size -- size of one dataset (structure size)
 *	create_memory_datatype -- creates hdf5 memory datatype corresponding
 *		to the datatype structure.
 *	create_file_datatype -- creates hdf5 file datatype corresponding
 *		to the datatype structure.
 *	create_s_memory_datatype -- creates hdf5 memory datatype corresponding
 *		to the summary datatype structure.
 *	create_s_file_datatype -- creates hdf5 file datatype corresponding
 *		to the summary datatype structure.
 *	init_job_series -- allocates a buffer for a complete time series
 *		(in job merge) and initializes each member
 *	merge_step_series -- merges all the individual time samples into a
 *		single data set with one item per sample.
 *		Data items can be scaled (e.g. subtracting beginning time)
 *		differenced (to show counts in interval) or other things
 *		appropriate for the series.
 *	series_total -- accumulate or average members in the entire series to
 *		be added to the file as totals for the node or task.
 *	extract_series -- format members of a structure for putting to
 *		to a file data extracted from a time series to be imported into
 *		another analysis tool. (e.g. format as comma separated value.)
 *	extract_totals -- format members of a structure for putting to
 *		to a file data extracted from a time series total to be
 *		imported into another analysis tool.
 *		(format as comma,separated value, for example.)
 */
typedef struct hdf5_api_ops {
	int   (*dataset_size) ();
	hid_t (*create_memory_datatype) ();
	hid_t (*create_file_datatype) ();
	hid_t (*create_s_memory_datatype) ();
	hid_t (*create_s_file_datatype) ();
	void* (*init_job_series) (int);
	void  (*merge_step_series) (hid_t, void*, void*, void*);
	void* (*series_total) (int, void*);
	void  (*extract_series) (FILE*, bool, int, int, char*, char*, void*,
			int);
	void  (*extract_total) (FILE*, bool, int, int, char*, char*, void*,
			int);
} hdf5_api_ops_t;

/* ============================================================================
 * Common support functions
   ==========================================================================*/

/*
 * Create a opts group from type
 */
hdf5_api_ops_t* profile_factory(char* type);

/*
 * Initialize profile (initialize static memory)
 */
void profile_init();

/*
 * Finialize profile (initialize static memory)
 */
void profile_fini();

/*
 * Make a dataset name
 *
 * Parameters
 *	type	- series name
 *
 * Returns
 *	common data set name based on type in static memory
 */
char* get_data_set_name(char* type);

/*
 * print info on an object for debugging
 *
 * Parameters
 *	group	 - handle to group.
 *	namGroup - name of the group
 */
void hdf5_obj_info(hid_t group, char* namGroup);

/*
 * get attribute handle by name.
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the attribute
 *
 * Returns - handle for attribute (or -1 when not found), caller must close
 */
hid_t get_attribute_handle(hid_t parent, char* name);

/*
 * get group by name.
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the group
 *
 * Returns - handle for group (or -1 when not found), caller must close
 */
hid_t get_group(hid_t parent, char* name);

/*
 * make group by name.
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the group
 *
 * Returns - handle for group (or -1 on error), caller must close
 */
hid_t make_group(hid_t parent, char* name);

/*
 * Put string attribute
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the attribute
 *	value	- value of the attribute
 */
void put_string_attribute(hid_t parent, char* name, char* value);

/*
 * get string attribute
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the attribute
 *
 * Return: pointer to value. Caller responsibility to free!!!
 */
char* get_string_attribute(hid_t parent, char* name);

/*
 * Put integer attribute
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the attribute
 *	value	- value of the attribute
 */
void put_int_attribute(hid_t parent, char* name, int value);

/*
 * get int attribute
 *
 * Parameters
 *	parent	- handle to parent group.
 *	name	- name of the attribute
 *
 * Return: value
 */
int get_int_attribute(hid_t parent, char* name);

/*
 * Get data from a group of a HDF5 file
 *
 * Parameters
 *	parent   - handle to parent.
 *	type     - type of data (PROFILE_*_DATA in slurm_acct_gather_profile.h)
 *	namGroup - name of group
 *	sizeData - pointer to variable into which to put size of dataset
 *
 * Returns -- data set of type (or null), caller must free.
 */
void* get_hdf5_data(hid_t parent, char* type, char* namGroup, int* sizeData);

/*
 * Put one data sample into a new group in an HDF5 file
 *
 * Parameters
 *	parent  - handle to parent group.
 *	type    - type of data (PROFILE_*_DATA from slurm_acct_gather_profile.h)
 *	subtype - generally source (node, series, ...) or summary
 *	group   - name of new group
 *	data    - data for the sample
 *      nItems  - number of items of type in the data
 */
void put_hdf5_data(hid_t parent, char* type, char* subtype, char* group,
		void* data, int nItems);

// ============================================================================
// Routines supporting Energy Data type
// ============================================================================

/*
 * returns size of one dataset
 */
int energy_dataset_size();

/*
 * Create the energy memory datatype.
 *
 * Returns - memory datatype for energy (caller must close)
 *
 */
hid_t energy_create_memory_datatype();

/*
 * Create the summary energy memory datatype.
 *
 * Returns - memory datatype for energy summary (caller must close)
 *
 */
hid_t energy_s_create_memory_datatype();

/*
 * Create the energy file datatype.
 *
 * Returns - file datatype for energy (caller must close)
 *
 */
hid_t energy_create_file_datatype();

/*
 * Create the summary energy file datatype.
 *
 * Returns - file datatype for energy summary (caller must close)
 *
 */
hid_t energy_s_create_file_datatype();

/*
 * Initialize data array for all energy samples
 *
 * Parameters
 * 	nSamples - number of samples in data series
 *
 * Returns
 * 	initialized data set; Caller must free
 */
void* energy_init_job_series(int nSamples);

/*
 * Add current sample to collection of all samples.
 *     Opportunity to do difference calculations
 *
 * Parameters
 *	group  - group containing dataset
 *	prior  - previous sample
 *	cur    - current sample
 *	buf    - loc in collection for current sample
 *
 */
void energy_merge_step_series(hid_t group, void* prior, void* cur, void* buf);

/*
 * create total for series
 *
 * Parameters
 * 	nSamples - number of samples in the series
 * 	data	 - data for series
 *
 * Returns
 * 	series total. caller must free
 *
 */
void* energy_series_total(int nSamples, void* data);

/*
 * Write energy time series data to a csv file
 *
 * Parameters
 *	fOt	  - file descriptor for output file
 *	putHeader - first data item, put the header line
 *	job	  - jobid
 *	step	  - stepid
 *	node	  - node name
 *	series    - series name
 *	data	  - data of type
 *	sizeData  - size of data set
 */
void energy_extract_series(FILE* fOt, bool putHeader, int job, int step,
		char* node, char* series, void* data, int sizeData);

/*
 * Write energy totals data to a csv file
 *
 * Parameters
 *	fOt	  - file descriptor for output file
 *	putHeader - first data item, put the header line
 *	job	  - jobid
 *	step	  - stepid
 *	node	  - node name
 *	series    - series name
 *	data	  - data of type
 *	sizeData  - size of data set
 */
void energy_extract_totals(FILE* fOt, bool putHeader, int job, int step,
		char* node, char* series, void* data, int sizeData);

/*
 * Create array of function pointers for common HDF5 operations
 */
hdf5_api_ops_t* energy_profile_factory();

// ============================================================================
// Routines supporting I/O Data type
// ============================================================================

/*
 * returns size of one dataset
 */
int io_dataset_size();

/*
 * Create the IO memory datatype.
 *
 * Returns - memory datatype for IO (caller must close)
 *
 */
hid_t io_create_memory_datatype(void);

/*
 * Create the IO Summary memory datatype.
 *
 * Returns - memory datatype for IO summary (caller must close)
 *
 */
hid_t io_s_create_memory_datatype(void);

/*
 * Create the IO file datatype.
 *
 * Returns - file datatype for IO (caller must close)
 *
 */
hid_t io_create_file_datatype(void);

/*
 * Create the IO Summary file datatype.
 *
 * Returns - file datatype for IO Summary (caller must close)
 *
 */
hid_t io_s_create_file_datatype(void);

/*
 * Initialize data array for all io samples
 *
 * Parameters
 *	nSamples - number of samples in data series
 *
 * Returns
 *	initialized data set; Caller must free
 */
void* io_init_job_series(int nSamples);

/*
 * Add current sample to collection of all samples.
 *     Opportunity to do difference calculations
 *
 * Parameters
 *	group  - group containing dataset
 *	prior  - previous sample
 *	cur    - current sample
 *	buf    - loc in collection for current sample
 */
void io_merge_step_series(hid_t group, void* prior, void* cur, void* buf);

/*
 * create total for series
 *
 * Parameters
 *	nSamples - number of samples in the series
 *	data	 - data for series
 *
 * Returns
 *	series total. caller must free
 *
 */
void* io_series_total(int nSamples, void* data);

/*
 * Write IO series data to a csv file
 *
 * Parameters
 *	fOt	  - file descriptor for output file
 *	putHeader - first data item, put the header line
 *	job	  - jobid
 *	step	  - stepid
 *	node	  - node name
 *	series    - series name
 *	data	  - data of type
 *	sizeData  - size of data set
 */
void io_extract_series(FILE* fOt, bool putHeader, int job, int step,
		char* node, char* series, void* data, int sizeData);

/*
 * Write IO Totals data to a csv file
 *
 * Parameters
 *	fOt	  - file descriptor for output file
 *	putHeader - first data item, put the header line
 *	job	  - jobid
 *	step	  - stepid
 *	node	  - node name
 *	series    - series name
 *	data	  - data of type
 *	sizeData  - size of data set
 */
void io_extract_total(FILE* fOt, bool putHeader, int job, int step,
		char* node, char* series, void* data, int sizeData);

/*
 * Create array of function pointers for common HDF5 operations
 */
hdf5_api_ops_t* io_profile_factory();

// ============================================================================
// Routines supporting Network Data type
// ============================================================================

/*
 * returns size of one dataset
 */
int network_dataset_size();

/*
 * Create the Network memory datatype.
 *
 * Returns - memory datatype for Network (caller must close)
 *
 */
hid_t network_create_memory_datatype(void);

/*
 * Create the Network Summary memory datatype.
 *
 * Returns - memory datatype for Network summary (caller must close)
 *
 */
hid_t network_s_create_memory_datatype(void);

/*
 * Create the Network file datatype.
 *
 * Returns - file datatype for Network (caller must close)
 *
 */
hid_t network_create_file_datatype(void);

/*
 * Create the Network summary file datatype.
 *
 * Returns - file datatype for Network summary (caller must close)
 *
 */
hid_t network_s_create_file_datatype(void);

/*
 * Initialize data array for all Network samples
 *
 * Parameters
 * 	nSamples - number of samples in data series
 *
 * Returns
 *	initialized data set; Caller must free
 */
void* network_init_job_series(int nSamples);

/*
 * Add current sample to collection of all samples.
 *     Opportunity to do difference calculations
 *
 * Parameters
 *	group  - group containing dataset
 *	prior  - previous sample
 *	cur    - current sample
 *	buf    - loc in collection for current sample
 *
 */
void network_merge_step_series(hid_t group, void* prior, void* cur, void* buf);

/*
 * create total for series
 *
 * Parameters
 *	nSamples - number of samples in the series
 *	data	 - data for series
 *
 * Returns
 *	series total. caller must free
 *
 */
void* network_series_totals(int nSamples, void* data);

/*
 * Write Network series data to a csv file
 *
 * Parameters
 *	fOt	  - file descriptor for output file
 *	putHeader - first data item, put the header line
 *	job	  - jobid
 *	step	  - stepid
 *	node	  - node name
 *	series    - series name
 *	data	  - data of type
 *	sizeData  - size of data set
 */
void network_extract_series(FILE* fOt, bool putHeader, int job, int step,
		char* node, char* series, void* data, int sizeData);

/*
 * Write Network totals data to a csv file
 *
 * Parameters
 *	fOt	  - file descriptor for output file
 *	putHeader - first data item, put the header line
 *	job	  - jobid
 *	step	  - stepid
 *	node	  - node name
 *	series    - series name
 *	data	  - data of type
 *	sizeData  - size of data set
 */
void network_extract_total(FILE* fOt, bool putHeader, int job, int step,
		char* node, char* series, void* data, int sizeData);

/*
 * Create array of function pointers for common HDF5 operations
 */
hdf5_api_ops_t* network_profile_factory();


// ============================================================================
// Routines supporting Task Data type
// ============================================================================

/*
 * returns size of one dataset
 */
int task_dataset_size();

/*
 * Create the task memory datatype.
 *
 * Returns - memory datatype for task (caller must close)
 *
 */
hid_t task_create_memory_datatype();

/*
 * Create the task summary memory datatype.
 *
 * Returns - memory datatype for task summary (caller must close)
 *
 */
hid_t task_s_create_memory_datatype();

/*
 * Create the task file datatype.
 *
 * Returns - file datatype for task (caller must close)
 *
 */
hid_t task_create_file_datatype();

/*
 * Create the task summary file datatype.
 *
 * Returns - file datatype for task summary (caller must close)
 *
 */
hid_t task_s_create_file_datatype();

/*
 * Initialize data array for all task samples
 *
 * Parameters
 *	nSamples - number of samples in data series
 *
 * Returns
 *	initialized data set; Caller must free
 */
void* task_init_job_series(int nSamples);

/*
 * Add current sample to collection of all samples.
 *     Opportunity to do difference calculations
 *
 * Parameters
 *	group  - group containing dataset
 *	prior  - previous sample
 *	cur    - current sample
 *	buf    - loc in collection for current sample
 */
void task_merge_step_series(hid_t group, void* prior, void* cur, void* buf);

/*
 * create total for series
 *
 * Parameters
 *	nSamples - number of samples in the series
 *	data	 - data for series
 *
 * Returns
 *	series total. caller must free
 */
void* task_series_total(int nSamples, void* data);

/*
 * Write task series data to a csv file
 *
 * Parameters
 *	fOt	  - file descriptor for output file
 *	putHeader - first data item, put the header line
 *	job	  - jobid
 *	step	  - stepid
 *	node	  - node name
 *	series    - series name
 * 	data	  - data of type
 *	sizeData  - size of data set
 */
void task_extract_series(FILE* fOt, bool putHeader, int job, int step,
		char* node, char* series, void* data, int sizeData);

/*
 * Write task totals data to a csv file
 *
 * Parameters
 *	fOt	  - file descriptor for output file
 *	putHeader - first data item, put the header line
 *	job	  - jobid
 *	step	  - stepid
 *	node	  - node name
 *	series    - series name
 * 	data	  - data of type
 *	sizeData  - size of data set
 */
void task_extract_total(FILE* fOt, bool putHeader, int job, int step,
		char* node, char* series, void* data, int sizeData);

/*
 * Create array of function pointers for common HDF5 operations
 */
hdf5_api_ops_t* task_profile_factory();

#endif /*__ACCT_GATHER_HDF5_API_H__*/
