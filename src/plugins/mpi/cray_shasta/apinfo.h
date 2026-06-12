/*****************************************************************************\
 *  apinfo.h - Cray Shasta PMI apinfo file creation header file
 *****************************************************************************
 *  Copyright 2019,2022 Hewlett Packard Enterprise Development LP
 *  Written by David Gloe <dgloe@cray.com>
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

#ifndef _MPI_CRAY_SHASTA_APINFO_H
#define _MPI_CRAY_SHASTA_APINFO_H

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/* Name of the directory to store Cray MPI data */
#define MPI_CRAY_DIR "mpi_cray_shasta"
#define HPE_SLINGSHOT_DIR "switch_hpe_slingshot"

/*
 * Application file format version
 */
#define PALS_APINFO_VERSION 5

/*
 * File header structure
 */
typedef struct {
	int version; /* Must be first */
	size_t total_size;
	size_t comm_profile_size;
	size_t comm_profile_offset;
	int ncomm_profiles;
	size_t cmd_size;
	size_t cmd_offset;
	int ncmds;
	size_t pe_size;
	size_t pe_offset;
	int npes;
	size_t node_size;
	size_t node_offset;
	int nnodes;
	size_t nic_size;
	size_t nic_offset;
	int nnics;
	size_t status_offset;
	size_t dist_size;
	size_t dist_offset;
} pals_header_t;

/*
 * Network communication profile structure
 */
typedef struct {
	uint32_t svc_id;	  /* CXI service ID */
	uint32_t traffic_classes; /* Bitmap of allowed traffic classes */
	uint16_t vnis[4];	  /* VNIs for this service */
	uint8_t nvnis;		  /* Number of VNIs */
	char device_name[16];	  /* NIC device for this profile */
} pals_comm_profile_t;

/*
 * MPMD command information structure
 */
typedef struct {
	int npes;	  /* Number of PEs in this command */
	int pes_per_node; /* Number of PEs per node */
	int cpus_per_pe;  /* Number of CPUs per PE */
} pals_cmd_t;

/*
 * PE information structure
 */
typedef struct {
	int localidx; /* Node-local PE index */
	int cmdidx;   /* Command index for this PE */
	int nodeidx;  /* Node index this PE is running on */
} pals_pe_t;

/*
 * Node information structure
 */
typedef struct {
	int nid;	   /* NOT Node ID: app-specific node index */
	char hostname[64]; /* Node hostname */
} pals_node_t;

/*
 * NIC address type
 */
typedef enum {
	PALS_ADDR_IPV4,
	PALS_ADDR_IPV6,
	PALS_ADDR_MAC
} pals_address_type_t;

/*
 * HSN NIC information structure
 */
typedef struct {
	int nodeidx;			  /* Node index this NIC belongs to */
	pals_address_type_t address_type; /* Address type for this NIC */
	char address[64];		  /* Address of this NIC */
	short numa_node;		  /* NUMA node it is in */
	char device_name[16];		  /* Device name */
	long _unused[2];
} pals_hsn_nic_t;

/*
 * Distances to each NIC. In the apinfo file, each of these will be the same
 * size, even if there are nodes with different counts of NICs or if some but
 * not all nodes have accelerators. The NIC distances are first and then the
 * accelerator distances, if they're provided.
 */
typedef struct {
	uint8_t num_nic_distances;     /* Number of CPU->NIC distances */
	uint8_t accelerator_distances; /* Accel distances too? (bool) */
	uint8_t distances[0]; /* One for each NIC, two if accelerators */
} pals_distance_t;

extern char *appdir; /* Application-specific spool directory */
extern char *apinfo; /* Application PMI file */
extern const char plugin_type[];

extern int create_apinfo(const stepd_step_rec_t *step, const char *spool);

#endif
