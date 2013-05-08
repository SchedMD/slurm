/*****************************************************************************\
 *  sprfmrgh5.c - slurm profile accounting plugin for io and energy using hdf5.
 *              - Utility to merge node-step files into a job file
 *              - or extract data from an job file
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
 *****************************************************************************
 *
 * This program is expected to be launched by the SLURM epilog script for a
 * job on the controller node to merge node-step files into a job file.
 *
\*****************************************************************************/

#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "src/common/xstring.h"
#include "src/plugins/acct_gather_profile/common/profile_hdf5.h"

// ============================================================================

// Options
static int   	jobid = -1;   // SLURM jobid
static int   	xstepid = -1; // SLURM step id to be extracted
static bool  	mergeMode = true;
static bool	header = true;
static bool     keepfiles = false;
static char     levelName[MAX_GROUP_NAME+1];
static char     seriesName[MAX_GROUP_NAME+1];
static char     outputFile[MAX_PROFILE_PATH+1];
static char**   seriesNames = NULL;
static int      numSeries = 0;
static char*    xnode;
static char*    slurmDataRoot = NULL;

void usage() {
	printf("\n\n\nsprfmrgh5 --jobid=n ...\n");
	printf("  Merges node-step HDF5 files for a SLURM job.\n");
	printf("  Required command line arguments are:\n");
	printf("     --jobid=n     jobid of SLURM job\n");
	printf("  Optional command line arguments are:\n");
	printf("     --profiledir=path  path to directory holding"
		" profile files\n");
	printf("     --savefiles   save node/step files\n");
	printf("     --extract     extract data series from job file\n");
	printf("                   default mode is merge node-step files\n");
	printf("     Extract mode options (all imply --extract)\n");
	printf("     --stepid={n|*) id step to extract (*=all,default)\n");
	printf("     --node={name|*} Node name to extract (*=all,default)\n");
	printf("     --level=[Node:Totals|Node:TimeSeries\n");
	printf("             Level to which series is attached\n");
	printf("     --series=[name|Tasks|*] Name of series\n");
	printf("              name=Specific name, Tasks=all tasks, (*=all)\n");
	printf("     --output=path "
		"path to a file into which to write the extract\n");
	printf("     --help           prints this message\n");
	printf("     Note all option values are case sensitive\n\n\n");
}

void opts(int argc, char **argv) {
	int errors = 0;
	int iax;
	char *posValue,*posDash;
	// Establish some defaults
	strcpy(outputFile,"profile_data.csv");
	xnode = xstrdup("*");
	for (iax=1; iax<argc; iax++) {
		if (strncasecmp(argv[iax],"--help=",6)==0) {
			usage();
			exit(0);
		} else if (strncasecmp(argv[iax],"--jobid=",8)==0) {
			posValue = &argv[iax][8];
			jobid = (int) strtol(posValue,NULL,10);
			if (jobid < 1) {
				printf("Jobid (%d) must be positive\n",jobid);
				errors++;
			}
		} else if (strncasecmp(argv[iax],"--profiledir=",13)==0) {
			xfree(slurmDataRoot);
			posValue = &argv[iax][13];
			slurmDataRoot = xstrdup(posValue);
		} else if (strncasecmp(argv[iax],"--savefiles",11)==0) {
			keepfiles = true;
		} else if (strncasecmp(argv[iax],"--stepid=",9)==0) {
			posValue = &argv[iax][9];
			if (strcmp(posValue,"*") != 0) {
				xstepid = (int) strtol(posValue, NULL, 10);
				if (xstepid < 0) {
					printf("stepid (%d) must be > 0\n",
							xstepid);
					errors++;
				}
			}
			mergeMode = false;
		} else if (strncasecmp(argv[iax],"--extract",9)==0) {
			mergeMode = false;
		} else if (strncasecmp(argv[iax],"--level=",8)==0) {
			posValue = &argv[iax][8];
			if (strlen(posValue) > MAX_GROUP_NAME) {
				printf("--level is too long\n");
				errors++;
			} else {
				strcpy(levelName,posValue);
				mergeMode = false;
			}
		} else if (strncasecmp(argv[iax],"--node=",7)==0) {
			xfree(xnode);
			posValue = &argv[iax][7];
			xnode = xstrdup(posValue);
			mergeMode = false;
		} else if (strncasecmp(argv[iax],"--output=",9)==0) {
			posValue = &argv[iax][9];
			if (strlen(posValue) > MAX_PROFILE_PATH) {
				printf("--output is too long\n");
				errors++;
			} else {
				strcpy(outputFile,posValue);
				mergeMode = false;
			}
		} else if (strncasecmp(argv[iax],"--series=",9)==0) {
			posValue = &argv[iax][9];
			if (strlen(posValue) > MAX_GROUP_NAME) {
				printf("--series is too long\n");
				errors++;
			} else {
				strcpy(seriesName,posValue);
				if (strstr(seriesName,GRP_TASK)) {
					posDash = strchr(seriesName,'-');
					// Task name have '~' not '-',
					// user may not  recognize this
					if (posDash > 0) {
						posDash[0] = '~';
					}
				}
			}
			mergeMode = false;
		} else {
			printf("%s is an unknown option",argv[iax]);
			errors++;
		}
	}
	if (errors) {
		printf("Too many errors\n\n");
		usage();
		exit(1);
	}
}

/* ============================================================================
 * ============================================================================
 * Functions for merging samples from node step files into a job file
 * ============================================================================
 * ========================================================================= */

void* get_all_samples(hid_t gidSeries, char* namSeries, char* type,
		int nSamples) {
	void*   data = NULL;
#ifdef HAVE_HDF5
	hid_t   idDataSet, dtypMemory, gSample, szDset;
	herr_t  ec;
	int     smpx ,len;
	void    *dataPrior = NULL, *dataCur = NULL;
	char 	namSample[MAX_GROUP_NAME+1];
	profile_hdf5_ops_t* ops;

	ops = profile_factory(type);
	if (ops == NULL) {
		info("Failed to create operations for %s", type);
		return NULL;
	}
	data = (*(ops->init_job_series))(nSamples);
	if (data == NULL) {
		xfree(ops);
		info("Failed to get memory for combined data");
		return NULL;
	}
	dtypMemory = (*(ops->create_memory_datatype))();
	if (dtypMemory < 0) {
		xfree(ops);
		xfree(data);
		info("Failed to create %s memory datatype", type);
		return NULL;
	}
	for (smpx=0; smpx<nSamples; smpx++) {
		len = H5Gget_objname_by_idx(gidSeries, smpx, namSample,
				MAX_GROUP_NAME);
		if (len<1 || len>MAX_GROUP_NAME) {
			info("Invalid group name %s", namSample);
			continue;
		}
		gSample = H5Gopen(gidSeries, namSample, H5P_DEFAULT);
		if (gSample < 0) {
			info("Failed to open %s", namSample);
		}
		idDataSet = H5Dopen(gSample, DataSetName(namSample),
				H5P_DEFAULT);
		if (idDataSet < 0) {
			H5Gclose(gSample);
			info("Failed to open %s dataset", type);
			continue;
		}
		szDset = (*(ops->dataset_size))();
		dataCur = xmalloc(szDset);
		if (dataCur == NULL) {
			H5Dclose(idDataSet);
			H5Gclose(gSample);
			info("Failed to get memory for prior data");
			continue;
		}
		ec = H5Dread(idDataSet, dtypMemory, H5S_ALL, H5S_ALL,
				H5P_DEFAULT, dataCur);
		if (ec < 0) {
			xfree(dataCur);
			H5Dclose(idDataSet);
			H5Gclose(gSample);
			info("Failed to read %s data", type);
			continue;
		}
		(*(ops->merge_step_series))(gSample, dataPrior, dataCur,
				       data+(smpx)*szDset);

		xfree(dataPrior);
		dataPrior = dataCur;
		H5Dclose(idDataSet);
		H5Gclose(gSample);
	}
	xfree(dataCur);
	H5Tclose(dtypMemory);
	xfree(ops);
#endif
	return data;
}

void merge_series_data(hid_t jgidTasks,
		       hid_t jgNode, hid_t nsgNode)
{
#ifdef HAVE_HDF5

	hid_t   jgSamples, nsgSamples;
	hid_t   gSeries, objType, gSeriesTotal = -1;
	hsize_t numSamples, nSeries;
	int     idsx, len;
	void    *data = NULL, *seriesTotal = NULL;
	char    *dataType = NULL;
	char    namSeries[MAX_GROUP_NAME+1];
	char    namSample1[MAX_GROUP_NAME+1];
	profile_hdf5_ops_t* ops = NULL;

	if (jgNode < 0) {
		info("Job Node is not HDF5 object");
		return;
	}
	if (nsgNode < 0) {
		info("Node-Step is not HDF5 object");
		return;
	}

	jgSamples = H5Gcreate(jgNode, GRP_SAMPLES,
			H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (jgSamples < 0) {
		info("Failed to create job node Samples");
		return;
	}
	nsgSamples = get_group(nsgNode, GRP_SAMPLES);
	if (nsgSamples < 0) {
		H5Gclose(jgSamples);
#ifdef PROFILE_HDF5_DEBUG
		info("Failed to get node-step Samples");
#endif
		return;
	}
	H5Gget_num_objs(nsgSamples, &nSeries);
	if (nSeries < 1) {
		// No series?
		H5Gclose(jgSamples);
		H5Gclose(nsgSamples);
		info("No Samples");
		return;
	}
	for (idsx = 0; idsx < nSeries; idsx++) {
		objType = H5Gget_objtype_by_idx(nsgSamples, idsx);
		if (objType != H5G_GROUP)
			continue;
		len = H5Gget_objname_by_idx(nsgSamples, idsx, namSeries,
				MAX_GROUP_NAME);
		if (len<1 || len>MAX_GROUP_NAME) {
			info("Invalid group name %s", namSeries);
			continue;
		}
		gSeries = H5Gopen(nsgSamples, namSeries, H5P_DEFAULT);
		if (gSeries < 0) {
			info("Failed to open %s", namSeries);
			continue;
		}
		H5Gget_num_objs(gSeries, &numSamples);
		if (numSamples <= 0) {
			H5Gclose(gSeries);
			info("Series %s has no samples", namSeries);
			continue;
		}
		// Get first sample in series to find out how big the data is.
		dataType = get_string_attribute(gSeries, ATTR_DATATYPE);
		if (dataType == NULL) {
			H5Gclose(gSeries);
			info("Failed to get datatype for Time Series Dataset");
			continue;
		}
		data = get_all_samples(gSeries, namSeries, dataType,
				numSamples);
		if (data == NULL) {
			xfree(dataType);
			H5Gclose(gSeries);
			info("Failed to get memory for Time Series Dataset");
			continue;
		}
		put_hdf5_data(jgSamples, dataType, SUBDATA_SERIES, namSeries,
				data, numSamples);
		ops = profile_factory(dataType);
		if (ops == NULL) {
			xfree(data);
			xfree(dataType);
			H5Gclose(gSeries);
			info("Failed to create operations for %s", dataType);
			continue;
		}
		seriesTotal = (*(ops->series_total))(numSamples, data);
		if (seriesTotal != NULL) {
			// Totals for series attaches to node
			gSeriesTotal = make_group(jgNode, GRP_TOTALS);
			if (gSeriesTotal < 0) {
				H5Gclose(gSeries);
				xfree(seriesTotal);
				xfree(data);
				xfree(dataType);
				xfree(ops);
				info("Failed to make Totals for Node");
				continue;
			}
			put_hdf5_data(gSeriesTotal, dataType, SUBDATA_SUMMARY,
					namSeries, seriesTotal, 1);
			H5Gclose(gSeriesTotal);
		}
		xfree(seriesTotal);
		xfree(ops);
		xfree(data);
		xfree(dataType);
		H5Gclose(gSeries);
	}
#endif
	return;
}

/* ============================================================================
 * Functions for merging tasks data into a job file
   ==========================================================================*/

void merge_task_totals(hid_t jgTasks, hid_t nsgNode, char* nodeName) {
#ifdef HAVE_HDF5

	hid_t   jgTask, jgTotals, nsgTotals, gTotal, nsgTasks, nsgTask = -1;
	hsize_t nobj, ntasks = -1;
	int	i, len, taskx, taskid, taskcpus, sizeData;
	void    *data;
	char    *type;
	char    buf[MAX_GROUP_NAME+1];
	char    groupName[MAX_GROUP_NAME+1];

	if (jgTasks < 0) {
		info("Job Tasks is not HDF5 object");
		return;
	}
	if (nsgNode < 0) {
		info("Node-Step is not HDF5 object");
		return;
	}

	nsgTasks = get_group(nsgNode, GRP_TASKS);
	if (nsgTasks < 0) {
#ifdef PROFILE_HDF5_DEBUG
		info("No Tasks group in node-step file");
#endif
		return;
	}
	H5Gget_num_objs(nsgTasks, &ntasks);
	for (taskx = 0; ((int)ntasks>0) && (taskx<((int)ntasks)); taskx++) {
		// Get the name of the group.
		len = H5Gget_objname_by_idx(nsgTasks, taskx, buf,
				MAX_GROUP_NAME);
		if (len<1 || len>MAX_GROUP_NAME) {
			info("Invalid group name %s", buf);
			continue;
		}
		nsgTask = H5Gopen(nsgTasks, buf, H5P_DEFAULT);
		if (nsgTask < 0) {
#ifdef PROFILE_HDF5_DEBUG
			info("Failed to open %s", buf);
#endif
			continue;
		}
		taskid = get_int_attribute(nsgTask, ATTR_TASKID);
		sprintf(groupName,"%s~%d", GRP_TASK, taskid);
		jgTask = H5Gcreate(jgTasks, groupName,
				H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		if (jgTask < 0) {
			H5Gclose(nsgTask);
			info("Failed to create job task group");
			continue;
		}
		put_string_attribute(jgTask, ATTR_NODENAME, nodeName);
		put_int_attribute(jgTask, ATTR_TASKID, taskid);
		taskcpus = get_int_attribute(nsgTask, ATTR_CPUPERTASK);
		put_int_attribute(jgTask, ATTR_CPUPERTASK, taskcpus);
		nsgTotals = get_group(nsgTask, GRP_TOTALS);
		if (nsgTotals < 0) {
			H5Gclose(jgTask);
			H5Gclose(nsgTask);
			continue;
		}
		jgTotals = H5Gcreate(jgTask, GRP_TOTALS,
				H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		if (jgTotals < 0) {
			H5Gclose(jgTask);
			H5Gclose(nsgTask);
			info("Failed to create job task totals");
			continue;
		}
		H5Gget_num_objs(nsgTotals, &nobj);
		for (i = 0; (nobj>0) && (i<nobj); i++) {
			// Get the name of the group.
			len = H5Gget_objname_by_idx(nsgTotals, i, buf,
					MAX_GROUP_NAME);

			if (len<1 || len>MAX_GROUP_NAME) {
				info("Invalid group name %s", buf);
				continue;
			}
			gTotal = H5Gopen(nsgTotals, buf, H5P_DEFAULT);
			if (gTotal < 0) {
				info("Failed to open %s", buf);
				continue;
			}
			type = get_string_attribute(gTotal, ATTR_DATATYPE);
			if (type == NULL) {
				H5Gclose(gTotal);
				info("No %s attribute", ATTR_DATATYPE);
				continue;
			}
			data = get_hdf5_data(gTotal, type, buf, &sizeData);
			if (data == NULL) {
				xfree(type);
				H5Gclose(gTotal);
				info("Failed to get group %d type %d data", buf,
						type);
				continue;
			}
			put_hdf5_data(jgTotals,type,SUBDATA_DATA,buf,data,1);
			xfree(data);
			xfree(type);
			H5Gclose(gTotal);
		}
		H5Gclose(nsgTotals);
		H5Gclose(nsgTask);
		H5Gclose(jgTotals);
		H5Gclose(jgTask);
	}
	H5Gclose(nsgTasks);
#endif
}

/* ============================================================================
 * Functions for merging node totals into a job file
   ==========================================================================*/

void merge_node_totals(hid_t jgNode, hid_t nsgNode) {
#ifdef HAVE_HDF5

	hid_t	jgTotals, nsgTotals, gTotal;
	hsize_t nobj;
	int 	i, len, sizeData;
	void  	*data;
	char 	*type;
	char 	buf[MAX_GROUP_NAME+1];

	if (jgNode < 0) {
		info("Job Node is not HDF5 object");
		return;
	}
	if (nsgNode < 0) {
		info("Node-Step is not HDF5 object");
		return;
	}
	jgTotals = H5Gcreate(jgNode, GRP_TOTALS,
			H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (jgTotals < 0) {
		info("Failed to create job node totals");
		return;
	}
	nsgTotals = get_group(nsgNode, GRP_TOTALS);
	if (nsgTotals < 0) {
		H5Gclose(jgTotals);
		return;
	}
	H5Gget_num_objs(nsgTotals, &nobj);
	for (i = 0; (nobj>0) && (i<nobj); i++) {
		// Get the name of the group.
		len = H5Gget_objname_by_idx(nsgTotals, i, buf,
				MAX_GROUP_NAME);

		if (len<1 || len>MAX_GROUP_NAME) {
			info("invalid group name %s", buf);
			continue;
		}
		gTotal = H5Gopen(nsgTotals, buf, H5P_DEFAULT);
		if (gTotal < 0) {
			info("Failed to open %s", buf);
			continue;
		}
		type = get_string_attribute(gTotal, ATTR_DATATYPE);
		if (type == NULL) {
			H5Gclose(gTotal);
			info("No %s attribute", ATTR_DATATYPE);
			continue;
		}
		data = get_hdf5_data(gTotal, type, buf, &sizeData);
		if (data == NULL) {
			xfree(type);
			H5Gclose(gTotal);
			info("Failed to get group %d type %d data", buf, type);
			continue;

		}
		put_hdf5_data(jgTotals, type, SUBDATA_DATA, buf, data, 1);
		xfree(data);
		xfree(type);
		H5Gclose(gTotal);
	}
	H5Gclose(nsgTotals);
	H5Gclose(jgTotals);
#endif
	return;
}

/* ============================================================================
 * Functions for merging step data into a job file
   ==========================================================================*/

void merge_node_step_data(hid_t fid_job, char* fileName, int nodeIndex,
		char* nodeName, hid_t jgidNodes, hid_t jgidTasks) {
#ifdef HAVE_HDF5

	hid_t	fid_nodestep, jgidNode, nsgidRoot, nsgidNode;
	char	*startTime;
	char	groupName[MAX_GROUP_NAME+1];

	jgidNode = H5Gcreate(jgidNodes, nodeName,
			H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (jgidNode < 0) {
		error("Failed to create group %s",nodeName);
		return;
	}
	put_string_attribute(jgidNode, ATTR_NODENAME, nodeName);
	// Process node step file
	// Open the file and the node group.
	fid_nodestep = H5Fopen(fileName, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (fid_nodestep < 0) {
		H5Gclose(jgidNode);
		error("Failed to open %s",fileName);
		return;
	}
	nsgidRoot = H5Gopen(fid_nodestep,"/", H5P_DEFAULT);
	sprintf(groupName,"/%s~%s", GRP_NODE, nodeName);
	nsgidNode = H5Gopen(nsgidRoot, groupName, H5P_DEFAULT);
	if (nsgidNode < 0) {
		H5Gclose(fid_nodestep);
		H5Gclose(jgidNode);
		error("Failed to open node group");
		return;;
	}
	startTime = get_string_attribute(nsgidNode,ATTR_STARTTIME);
	if (startTime == NULL) {
		info("No %s attribute", ATTR_STARTTIME);
	} else {
		put_string_attribute(jgidNode,ATTR_STARTTIME,startTime);
		xfree(startTime);
	}
	merge_node_totals(jgidNode, nsgidNode);
	merge_task_totals(jgidTasks, nsgidNode, nodeName);
	merge_series_data(jgidTasks, jgidNode, nsgidNode);
	H5Gclose(nsgidNode);
	H5Fclose(fid_nodestep);
	H5Gclose(jgidNode);
	if (!keepfiles)
		remove(fileName);
	return;
#endif
}

void merge_step_files() {
#ifdef HAVE_HDF5

	hid_t 	fid_job = -1, jgidStep = -1, jgidNodes = -1, jgidTasks = -1;
	DIR    *dir;
	struct  dirent *de;
	char	jobprefix[MAX_PROFILE_PATH+1];
	char	stepnode[MAX_PROFILE_PATH+1];
	char	profileStepDir[MAX_PROFILE_PATH+1];
	char	profileStepPath[MAX_PROFILE_PATH+1];
	char	jgrpStepName[MAX_GROUP_NAME+1];
	char	jgrpNodesName[MAX_GROUP_NAME+1];
	char	jgrpTasksName[MAX_GROUP_NAME+1];
	char 	*possquiggle, *posdot,*profileJobFileName;
	int	stepx = 0, numSteps = 0, nodex = -1;
	bool	foundFiles = false;

	sprintf(profileStepDir,"%s/tmp",slurmDataRoot);
	profileJobFileName = make_job_profile_path(slurmDataRoot, jobid);
	while (nodex != 0) {
		if ((dir = opendir(profileStepDir)) == NULL) {
			error("opendir for job profile directory): %m");
			exit(1);
		}
		sprintf(jobprefix,"job~%d~%d~",jobid,stepx);
		nodex = 0;
		while ((de = readdir(dir)) != NULL) {
			if (strncmp(jobprefix,de->d_name,strlen(jobprefix))
					== 0) {
				// Found a node step file for this job
				if (!foundFiles) {
					// Need to create the job file
					fid_job = H5Fcreate(profileJobFileName,
							H5F_ACC_TRUNC,
							H5P_DEFAULT,
							H5P_DEFAULT);
					if (fid_job < 0) {
						fatal("Failed to %s %s",
							"create HDF5 file:",
							profileJobFileName);
					}
					foundFiles = true;
				}
				possquiggle = de->d_name+strlen(jobprefix);
				strcpy(stepnode,possquiggle);
				posdot = strchr(stepnode,'.');
				posdot[0] = '\0'; // remove extension
				if (nodex == 0) {
					numSteps++;
					sprintf(jgrpStepName,"/%s~%d",GRP_STEP,
							stepx);
					jgidStep = make_group(fid_job,
							jgrpStepName);
					if (jgidStep < 0) {
						error("Failed to create %s",
								jgrpStepName);
						continue;
					}
					sprintf(jgrpNodesName,"%s/%s",
							jgrpStepName,
							GRP_NODES);
					jgidNodes = make_group(jgidStep,
							jgrpNodesName);
					if (jgidNodes < 0) {
						error("Failed to create %s",
								jgrpNodesName);
						continue;
					}
					sprintf(jgrpTasksName,"%s/%s",
							jgrpStepName,
							GRP_TASKS);
					jgidTasks = make_group(jgidStep,
							jgrpTasksName);
					if (jgidTasks < 0) {
						error("Failed to create %s",
								jgrpTasksName);
						continue;
					}
				}
				sprintf(profileStepPath,"%s/%s",profileStepDir,
						de->d_name);
#ifdef PROFILE_HDF5_DEBUG
				printf("Adding %s to the job file\n",
						profileStepPath);
#endif

				merge_node_step_data(fid_job, profileStepPath,
						nodex, stepnode,
						jgidNodes, jgidTasks);

				nodex++;
			}
		}
		closedir(dir);
		if (nodex > 0) {
			put_int_attribute(jgidStep, ATTR_NNODES, nodex);
			H5Gclose(jgidTasks);
			H5Gclose(jgidNodes);
			H5Gclose(jgidStep);
		}
		stepx++;
	}
	put_int_attribute(fid_job, ATTR_NSTEPS, numSteps);
	if (!foundFiles)
		info("No node step files found for jobid=%d",jobid);
	if (fid_job != -1)
		H5Fclose(fid_job);
#endif
}

/* ============================================================================
 * ============================================================================
 * Functions for data extraction
 * ============================================================================
 * ========================================================================= */

hid_t get_series_parent(hid_t group) {
	hid_t gidLevel = -1;
#ifdef HAVE_HDF5
	if (strcasecmp(levelName,"Node:Totals") == 0) {
		gidLevel = get_group(group, GRP_TOTALS);
		if (gidLevel < 0) {
			info("Failed to open  group %s", GRP_TOTALS);
		}
	} else if (strcasecmp(levelName,"Node:TimeSeries") == 0) {
		gidLevel = get_group(group, GRP_SAMPLES);
		if (gidLevel < 0) {
			info("Failed to open  group %s", GRP_SAMPLES);
		}
	} else {
		info("%s is an illegal level", levelName);
		return -1;

	}
#endif
	return gidLevel;
}


void get_series_names(hid_t group) {
#ifdef HAVE_HDF5
	int i, len;
	hsize_t nobj;
	char buf[MAX_GROUP_NAME+1];
	H5Gget_num_objs(group, &nobj);
	numSeries = (int) nobj;
	if (numSeries < 0) {
#ifdef PROFILE_HDF5_DEBUG
		info("No Data Series in group");
		hdf5_obj_info(group, "???");
#endif
		return;
	}
	seriesNames = xmalloc(sizeof(char*)*numSeries);
	for (i = 0; (numSeries>0) && (i<numSeries); i++) {
		len = H5Gget_objname_by_idx(group, i, buf, MAX_GROUP_NAME);
		if ((len < 0) || (len > MAX_GROUP_NAME)) {
			info("Invalid series name=%s", buf);
			// put into list anyway so list doesn't have a null.
		}
		seriesNames[i] = xstrdup(buf);
	}
#endif
}


void extract_node_level(FILE* fOt, int stepx, hid_t jgidNodes, int nnodes,
		bool header, char* dataSetName) {
#ifdef HAVE_HDF5
	hid_t	jgidNode, gidLevel, gidSeries;
	int 	nodex, len, sizeData;
	void	*data;
	char	*dataType, *subtype;
	char    jgrpNodeName[MAX_GROUP_NAME+1];
	profile_hdf5_ops_t* ops;

	for (nodex=0;nodex<nnodes;nodex++) {
		len = H5Gget_objname_by_idx(jgidNodes, nodex,
				jgrpNodeName, MAX_GROUP_NAME);
		if ((len < 0) || (len > MAX_GROUP_NAME)) {
			info("Invalid node name=%s", jgrpNodeName);
			continue;
		}
		jgidNode = get_group(jgidNodes, jgrpNodeName);
		if (jgidNode < 0) {
			info("Failed to open group %s", jgrpNodeName);
			continue;
		}
		if (strcmp(xnode,"*")!=0 && strcmp(xnode,jgrpNodeName)!=0)
			continue;
		gidLevel = get_series_parent(jgidNode);
		if (gidLevel == -1) {
			H5Gclose(jgidNode);
			continue;
		}
		gidSeries = get_group(gidLevel, dataSetName);
		if (gidSeries < 0) {
			// This is okay, may not have ran long enough for
			// a sample (hostname????)
			H5Gclose(gidLevel);
			H5Gclose(jgidNode);
			continue;
		}
		dataType = get_string_attribute(gidSeries, ATTR_DATATYPE);
		if (dataType == NULL) {
			H5Gclose(gidSeries);
			H5Gclose(gidLevel);
			H5Gclose(jgidNode);
			info("No datatype in %s", dataSetName);
			continue;
		}
		subtype = get_string_attribute(gidSeries, ATTR_SUBDATATYPE);
		if (subtype == NULL) {
			xfree(dataType);
			H5Gclose(gidSeries);
			H5Gclose(gidLevel);
			H5Gclose(jgidNode);
			info("No %s attribute", ATTR_SUBDATATYPE);
			continue;
		}
		ops = profile_factory(dataType);
		if (ops == NULL) {
			xfree(subtype);
			xfree(dataType);
			H5Gclose(gidSeries);
			H5Gclose(gidLevel);
			H5Gclose(jgidNode);
			info("Failed to create operations for %s", dataType);
			continue;
		}
		data = get_hdf5_data(gidSeries,dataType,dataSetName,&sizeData);
		if (data != NULL) {
			if (strcmp(subtype,SUBDATA_SUMMARY) != 0)
				(*(ops->extract_series))
					(fOt, header, jobid, stepx,
					jgrpNodeName,dataSetName,
					data,sizeData);
			else
				(*(ops->extract_total))
					(fOt, header, jobid, stepx,
					jgrpNodeName,dataSetName,
					data,sizeData);

			header = false;
			xfree(data);
		} else {
			fprintf(fOt,"%d,%d,%s,No %s Data\n",
					jobid,stepx,jgrpNodeName,dataSetName);
		}
		xfree(ops);
		xfree(dataType);
		H5Gclose(gidSeries);
		H5Gclose(gidLevel);
		H5Gclose(jgidNode);
	}
#endif
}


void extract_data() {
#ifdef HAVE_HDF5
	hid_t	fid_job, jgidRoot, jgidStep, jgidNodes, jgidNode, jgidLevel;
	int	nsteps, nnodes, stepx, isx, len;
	char	jgrpStepName[MAX_GROUP_NAME+1];
	char	jgrpNodeName[MAX_GROUP_NAME+1];
	char	fileName[MAX_PROFILE_PATH+1];
	bool    header;

	FILE* fOt = fopen(outputFile,"w");
	if (fOt == NULL) {
		error("Failed to create output file %s -- %m",outputFile);
	}
	len = snprintf(fileName,MAX_PROFILE_PATH,"%s/job~%d.h5",
				slurmDataRoot, jobid);
	if (len >= MAX_PROFILE_PATH) {
		error("path is too big");
		exit(1);
	}
	fid_job = H5Fopen(fileName, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (fid_job < 0) {
		error("Failed to open %s", fileName);
		return;
	}
	jgidRoot = H5Gopen(fid_job,"/", H5P_DEFAULT);
	if (jgidRoot < 0) {
		H5Fclose(fid_job);
		error("Failed to open  root");
		return;
	}
	nsteps = get_int_attribute(jgidRoot,ATTR_NSTEPS);
	for (stepx=0;stepx<nsteps;stepx++) {
		if ((xstepid!=-1) && (stepx!=xstepid))
			continue;
		sprintf(jgrpStepName,"%s~%d",GRP_STEP,stepx);
		jgidStep = get_group(jgidRoot, jgrpStepName);
		if (jgidStep < 0) {
			error("Failed to open  group %s", jgrpStepName);
			continue;
		}
		if (strncasecmp(levelName,"Node:",5)== 0) {
			nnodes = get_int_attribute(jgidStep,ATTR_NNODES);
			jgidNodes = get_group(jgidStep, GRP_NODES);
			if (jgidNodes < 0) {
				H5Gclose(jgidStep);
				error("Failed to open  group %s",GRP_NODES);
				continue;
			}
			len = H5Gget_objname_by_idx(jgidNodes, 0, jgrpNodeName,
					MAX_GROUP_NAME);
			if ((len < 0) || (len > MAX_GROUP_NAME)) {
				H5Gclose(jgidNodes);
				H5Gclose(jgidStep);
				error("Invalid node name %s",jgrpNodeName);
				continue;
			}
			jgidNode = get_group(jgidNodes, jgrpNodeName);
			if (jgidNode < 0) {
				H5Gclose(jgidNodes);
				H5Gclose(jgidStep);
				info("Failed to open  group %s", jgrpNodeName);
				continue;
			}
			jgidLevel = get_series_parent(jgidNode);
			if (jgidLevel == -1) {
				H5Gclose(jgidNode);
				H5Gclose(jgidNodes);
				H5Gclose(jgidStep);
				continue;
			}
			get_series_names(jgidLevel);
			H5Gclose(jgidLevel);
			H5Gclose(jgidNode);
			if (strcmp(seriesName,"*") == 0) {
				for (isx=0; isx<numSeries; isx++) {
					extract_node_level(fOt,stepx,jgidNodes,
						nnodes,true,seriesNames[isx]);
				}
			} else if (strcmp(seriesName,GRP_TASKS) == 0) {
				header = true;
				for (isx=0; isx<numSeries; isx++) {
					if (strstr(seriesNames[isx],GRP_TASK))
					{
						extract_node_level(fOt,stepx,
							jgidNodes, nnodes,
							header,
							seriesNames[isx]);
						header = false;
					}
				}
			} else {
				extract_node_level(fOt, stepx, jgidNodes,
					nnodes, true, seriesName);
			}
			delete_string_list(seriesNames, numSeries);
			seriesNames = NULL;
			numSeries = 0;
			H5Gclose(jgidNodes);
//		} else if (strncasecmp(levelName,"Task:",5)== 0) {
			// TODO: do task (currently no task data
		} else {
			info("%s is an illegal level", levelName);
		}
		H5Gclose(jgidStep);
	}
	H5Gclose(jgidRoot);
	H5Fclose(fid_job);
	fclose(fOt);
#endif
}


int main (int argc, char **argv)
{
	if (argc <= 1) {
		usage();
		exit(0);
	}
	opts(argc, argv);

	ProfileInit();
	if (mergeMode) {
		printf("Merging node-step files into %s\n",
			make_job_profile_path(slurmDataRoot, jobid));
		merge_step_files();
	} else {
		printf("Extracting job data from %s into %s\n",
			make_job_profile_path(slurmDataRoot, jobid),
			outputFile);
		extract_data();
	}
	ProfileFinish();
	xfree(slurmDataRoot);
	xfree(xnode);
	return 0;
}
