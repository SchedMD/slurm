/*****************************************************************************\
 *  sprfmrgh5.h - slurm profile accounting plugin for io and energy using hdf5.
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
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
\*****************************************************************************/

#ifndef _GATHER_PROFILE_SPRFMRGH5_H_
#define _GATHER_PROFILE_SPRFMRGH5_H_

/* ============================================================================
 * ============================================================================
 * Functions for merging samples from node step files into a job file
 * ============================================================================
 * ========================================================================+ */

/*
 * get all samples for a series for a node-step
 *
 * Parameters
 *	gidSamples - handle to samples group.
 *	namSeries  - name of data series
 *	type       - data type in sample (PROFILE_*_DATA_
 *	nSamples   - number of samples in series
 *
 * Returns -- data, caller must free.
 */
void* get_all_samples(hid_t gidSamples, char* namSeries,
		char* type, int nSamples);

/*
 * Add data from the time-series section from the node-step HDF5 file
 *     (if it exists) to corresponding node and step in job HDF5 file
 *
 * Parameters
 *	jgidTasks - Tasks nodes in job file
 *	jgNode    - group for node of job
 *	nsgNode   - group for node in node-step file
 */
void merge_series_data(hid_t jgidTasks, hid_t jgNode, hid_t nsgNode);


/* ============================================================================
 * Functions for merging tasks data into a job file
   ==========================================================================*/

/*
 * Add data from the tasks section from node-step HDF5 file (if it exists)
 *     to corresponding node and step in job HDF5 file
 *
 * Parameters
 *	jgTasks    - group for tasks of step
 *	nsgNode    - group for node in node-step file
 *	nodeName   - name of node
 */
void merge_task_totals(hid_t jgTasks, hid_t nsgNode, char* nodeName);

/* ============================================================================
 * Functions for merging node totals into a job file
   ==========================================================================*/

/*
 * Add data from the nodes section from node-step HDF5 file (if it exists)
 *     to corresponding node and step in job HDF5 file
 *
 * Parameters
 *	jgNode     - group for node of step
 *	nsdNode    - group for node in node-step file
 */
void merge_node_totals(hid_t jgNode, hid_t nsgNode);

/* ============================================================================
 * Functions for merging step data into a job file
   ==========================================================================*/

/*
 * add node-step data to job file
 *
 * Parameters
 *	fid_job	  - hdf5 file descriptor for job
 *	filename  - name of node-step file
 *	nodeIndex - index of node within step
 *	nodeName  - hostname of node
 *	jgidNodes - Nodes group in job file
 *	jgidTasks - Tasks group in job file
 */
void merge_node_step_data(hid_t fid_job, char* fileName, int nodeIndex,
		char* nodeName, hid_t jgidNodes, hid_t jgidTasks);

/*
 * Merge of the (node) step files into one file for the job.
 */
void merge_step_files();


/* ============================================================================
 * ============================================================================
 * Functions for data extraction
 * ============================================================================
 * ========================================================================= */

/*
 * Get the parent group of a specified series
 *
 * Parameters
 * 	group 	- id of node containing series
 *
 * Returns
 * 	id of parent of series level (caller must close)
 *
 */
hid_t get_series_parent(hid_t group);

/*
 * Get names of all series on the node
 *
 * Parameters
 * 	group	- id of node
 *
 * Returns
 *	Creates static seriesNames with pointers to string names;
 *      Caller must delete with 'delete_string_list'
 */
void get_series_names(hid_t group);

/*
 * extract a data set from a node(s)
 *
 * Parameters
 *	fOt	  - File def for output file
 *	stepx     - stepid
 *	jgidNodes - nodes group in job (and step)
 *	nnodes	  - number of nodes
 *	header    - put heading in ouput
 *	dataset   - name of dataset
 */
void extract_node_level(FILE* fOt, int stepx, hid_t jgidNodes, int nnodes,
		bool header, char* dataSet);

/*
 * extract data from job file.
 *
 * Parameters
 *	command line options are static data
 *
 */
void extract_data();

#endif
