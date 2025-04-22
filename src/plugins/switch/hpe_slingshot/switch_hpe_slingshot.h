/*****************************************************************************\
 *  switch_hpe_slingshot.h - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2021-2023 Hewlett Packard Enterprise Development LP
 *  Written by David Gloe <david.gloe@hpe.com>
 *  Written by Jim Nordby <james.nordby@hpe.com>
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

#ifndef _SWITCH_HPE_SLINGSHOT_H_
#define _SWITCH_HPE_SLINGSHOT_H_

#include <stdbool.h>
#include <stdint.h>

#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/* Version of the state file */
#define SLINGSHOT_STATE_VERSION 2
#define SLINGSHOT_STATE_VERSION_VER1 1

/* State file name */
#define SLINGSHOT_STATE_FILE "slingshot_state"

/* Environment variable for libcxi library name (for dlopen()) */
#define SLINGSHOT_CXI_LIB_VERSION_ENV "SLURM_SLINGSHOT_CXI_VERSION"

/* Suffix of versioned CXI library functions if environment variable not set */
#define SLINGSHOT_CXI_LIB_VERSION ""

/* Min/max VNI values */
#define SLINGSHOT_VNI_MIN 0
#define SLINGSHOT_VNI_MAX 65535
#define SLINGSHOT_VNIS    4     /* Max VNIs/service */

/* Default state values if not configured */
#define SLINGSHOT_VNI_MIN_DEF 1024
#define SLINGSHOT_VNI_MAX_DEF 65535

/* Number of retries for destroying CXI services */
#define SLINGSHOT_CXI_DESTROY_RETRIES 5

/* File path format to rdzv_get_en setting */
#define SLINGSHOT_RDZV_GET_EN_FMT \
	"/sys/class/cxi/cxi%d/device/properties/rdzv_get_en"

/* File path format to the default rdzv_get_en setting */
#define SLINGSHOT_RDZV_GET_EN_DEFAULT_FMT \
	"/sys/module/cxi_%s/parameters/rdzv_get_en_default"

extern int free_vnis; /* Number of free VNIs */

/* Set of valid auth types used for REST */
typedef enum {
	SLINGSHOT_AUTH_NONE = 0, /* No authentication */
	SLINGSHOT_AUTH_BASIC,    /* User name and password */
	SLINGSHOT_AUTH_OAUTH     /* OAuth2 client credentials grant */
} slingshot_rest_auth_t;

/* Per-job shared VNI structure */
typedef struct job_vni {
	uint32_t job_id;        /* Job ID */
	uint16_t vni;           /* Per-Job-ID shared VNI */
} job_vni_t;

/* Format for state file created by switch_p_libstate_save */
typedef struct slingshot_state {
	uint32_t version;       /* Version of this file format */
	uint16_t vni_min;       /* Minimum VNI to allocate */
	uint16_t vni_max;       /* Maximum VNI to allocate */
	uint16_t vni_last;      /* Last allocated VNI */
	bitstr_t *vni_table;    /* Bitmap of allocated VNIs */
	uint32_t num_job_vnis;  /* Number of per-job shared VNIs */
	job_vni_t *job_vnis;    /* Per-job shared VNI reservations */
	uint32_t num_job_hwcoll; /* Number of per-job shared VNIs */
	uint32_t *job_hwcoll;	/* Array of job IDs using collectives */
} slingshot_state_t;

/* Max NIC resources per application */
#define SLINGSHOT_TXQ_MAX     1024    /* Max transmit command queues */
#define SLINGSHOT_TGQ_MAX     512     /* Max target command queues */
#define SLINGSHOT_EQ_MAX      2047    /* Max event queues */
#define SLINGSHOT_CT_MAX      2047    /* Max counters */
#define SLINGSHOT_TLE_MAX     2048    /* Max trigger list entries */
#define SLINGSHOT_PTE_MAX     2048    /* Max portal table entries */
#define SLINGSHOT_LE_MAX      16384   /* Max list entries */
#define SLINGSHOT_AC_MAX      1022    /* Max addressing contexts */

/* Default per-thread NIC resources per application */
#define SLINGSHOT_TXQ_DEF     2       /* Per-thread transmit command queues */
#define SLINGSHOT_TGQ_DEF     1       /* Per-thread target command queues */
#define SLINGSHOT_EQ_DEF      2       /* Per-thread event queues */
#define SLINGSHOT_CT_DEF      1       /* Per-thread counters */
#define SLINGSHOT_TLE_DEF     1       /* Per-thread trigger list entries */
#define SLINGSHOT_PTE_DEF     6       /* Per-thread portal table entries */
#define SLINGSHOT_LE_DEF      16      /* Per-thread list entries */
#define SLINGSHOT_AC_DEF      4       /* Per-thread addressing contexts */

/* NIC resource limit structure */
typedef struct slingshot_limits {
	uint16_t max;  /* Max of this resource the application can use */
	uint16_t res;  /* Resources reserved for only this application */
	uint16_t def;  /* Per-thread resources to reserve */
} slingshot_limits_t;

/* Full set of NIC resource limits */
typedef struct slingshot_limits_set {
	slingshot_limits_t txqs;  /* Transmit command queue limits */
	slingshot_limits_t tgqs;  /* Target command queue limits */
	slingshot_limits_t eqs;   /* Event queue limits */
	slingshot_limits_t cts;   /* Counter limits */
	slingshot_limits_t tles;  /* Trigger list entry limits */
	slingshot_limits_t ptes;  /* Portal table entry limits */
	slingshot_limits_t les;   /* List entry limits */
	slingshot_limits_t acs;   /* Addressing context limits */
} slingshot_limits_set_t;

/*
 * Slingshot switch plugin global configuration state, based on defaults and
 * 'SwitchParameters' slurm.conf variable
 */
typedef struct slingshot_config {
	uint32_t destroy_retries; /* retry count for destroying services */
	uint8_t single_node_vni;        /* Allocate VNIs for single-node apps */
	uint8_t job_vni;                /* Allocate extra VNI per-job */
	uint32_t tcs;                   /* Bitmap of default traffic classes */
	uint32_t flags;                 /* Bitmap of configuration flags */
	slingshot_limits_set_t limits;  /* Set of NIC resource limits */
	char *jlope_url;                /* URL of jackaloped REST interface */
	slingshot_rest_auth_t jlope_auth; /* jackaloped authentication type */
	char *jlope_authdir;            /* jackaloped auth file directory */
	uint32_t hwcoll_addrs_per_job;  /* #Hardware collectives per job */
	uint32_t hwcoll_num_nodes;      /* Minimum job nodes for HW coll */
	char *fm_url;                   /* fabric manager REST interface URL */
	slingshot_rest_auth_t fm_auth;  /* fabric manager authentication type */
	char *fm_authdir;               /* fabric manager auth file directory */
} slingshot_config_t;

/* Values for slingshot_config_t.single_node_vni */
#define SLINGSHOT_SN_VNI_NONE   0  /* No VNIs allocated for single-node apps */
#define SLINGSHOT_SN_VNI_ALL    1  /* All single-node apps get a VNI */
#define SLINGSHOT_SN_VNI_USER   2  /* srun --network=single_node_vni */

/* Values for slingshot_config_t.job_vni */
#define SLINGSHOT_JOB_VNI_NONE   0  /* No job VNIs allocated */
#define SLINGSHOT_JOB_VNI_ALL    1  /* All jobs get a job VNI */
#define SLINGSHOT_JOB_VNI_USER   2  /* Job VNIs using srun --network=job_vni */

/* NIC communication profile structure (compute-node specific) */
typedef struct slingshot_comm_profile {
	uint32_t svc_id;        /* Slingshot service ID */
	uint16_t vnis[SLINGSHOT_VNIS]; /* VNIs for this service */
	uint16_t vnis_used;     /* Number of valid VNIs in vnis[] */
	uint32_t tcs;           /* Bitmap of allowed traffic classes */
	char device_name[16];   /* NIC device name (e.g. "cxi0") */
} slingshot_comm_profile_t;

/*
 * Slingshot HSN NIC information structure
 */
typedef enum {
	SLINGSHOT_ADDR_IPV4,
	SLINGSHOT_ADDR_IPV6,
	SLINGSHOT_ADDR_MAC
} slingshot_addr_type_t;
typedef struct {
	uint32_t nodeidx;       /* Node index this NIC belongs to */
	slingshot_addr_type_t address_type; /* Address type for this NIC */
	char address[64];      /* Address of this NIC */
	uint16_t numa_node;    /* NUMA node it is in */
	char device_name[16];  /* Device name */
} slingshot_hsn_nic_t;

/*
 * Information to support Slingshot HSN hardware collectives
 * and communication with Slingshot Fabric Manager (FM)
 */
typedef struct {
	uint32_t job_id; 	 /* job id */
	uint32_t step_id; 	 /* step id */
	char *mcast_token;       /* Session token returned from FM */
	char *fm_url;            /* FM URL for creating multicast trees */
	uint32_t addrs_per_job;  /* Collectives multicast addrs per job */
	uint32_t num_nodes;      /* Minimum #nodes to get multicast addrs */
} slingshot_hwcoll_t;

/* Denotes packing a null stepinfo structure */
#define SLINGSHOT_JOBINFO_NULL_VERSION 0xDEAFDEAF

typedef struct slingshot_jobinfo {
	uint32_t num_vnis; /* Number of VNIs */
	uint16_t *vnis; /* List of VNIs allocated for this job */
	char *extra; /* storage for mid-release extras */
} slingshot_jobinfo_t;

/* Jobinfo structure passed from slurmctld to slurmd */
typedef struct slingshot_stepinfo {
	uint32_t version;      /* Version of this structure */
	uint32_t num_vnis;     /* Number of VNIs */
	uint16_t *vnis;        /* List of VNIs allocated for this application */
	uint32_t tcs;          /* Bitmap of allowed traffic classes */
	slingshot_limits_set_t limits; /* Set of NIC resource limits */
	uint32_t depth;        /* Threads-per-task for limit calculation */
	uint32_t num_profiles; /* Number of communication profiles */
	slingshot_comm_profile_t *profiles; /* List of communication profiles */
	uint32_t flags;        /* Configuration flags */
	uint32_t num_nics;     /* Number of entries in 'nics' array */
	slingshot_hsn_nic_t *nics; /* HSN NIC information for instant on */
	slingshot_hwcoll_t *hwcoll; /* HSN HW collectives info */
} slingshot_stepinfo_t;

/* Slingshot traffic classes (bitmap) */
#define SLINGSHOT_TC_DEDICATED_ACCESS 0x1
#define SLINGSHOT_TC_LOW_LATENCY      0x2
#define SLINGSHOT_TC_BULK_DATA        0x4
#define SLINGSHOT_TC_BEST_EFFORT      0x8
#define SLINGSHOT_TC_DEFAULT          (SLINGSHOT_TC_LOW_LATENCY | \
				       SLINGSHOT_TC_BEST_EFFORT)

/* Values for slingshot_stepinfo_t.flags */
/*
 * If SLINGSHOT_FLAGS_ADJUST_LIMITS is set (default), slurmd will adjust
 * resource limit reservations by subtracting system service reserved/used
 * resources
 *
 * If SLINGSHOT_FLAGS_DISABLE_RDZV_GET is set, slurmd will disable rendezvous
 * gets in the Cassini NIC for the duration of the application
 */
#define SLINGSHOT_FLAGS_ADJUST_LIMITS 0x1
/*
 * #define SLINGSHOT_FLAGS_VNI_PIDS      0x2 DEPRECATED in 23.02, can be used in
 *					     25.02
 */
#define SLINGSHOT_FLAGS_DISABLE_RDZV_GET 0x4
#define SLINGSHOT_FLAGS_DEFAULT SLINGSHOT_FLAGS_ADJUST_LIMITS

/* Environment variables set for applications */
#define SLINGSHOT_SVC_IDS_ENV         "SLINGSHOT_SVC_IDS"
#define SLINGSHOT_VNIS_ENV            "SLINGSHOT_VNIS"
#define SLINGSHOT_DEVICES_ENV         "SLINGSHOT_DEVICES"
#define SLINGSHOT_TCS_ENV             "SLINGSHOT_TCS"
/* Slingshot collectives environment variables set for applications */
#define SLINGSHOT_FI_CXI_COLL_JOB_ID_ENV          "FI_CXI_COLL_JOB_ID"
#define SLINGSHOT_FI_CXI_COLL_JOB_STEP_ID_ENV     "FI_CXI_COLL_JOB_STEP_ID"
#define SLINGSHOT_FI_CXI_COLL_MCAST_TOKEN_ENV     "FI_CXI_COLL_MCAST_TOKEN"
#define SLINGSHOT_FI_CXI_COLL_FABRIC_MGR_URL_ENV  "FI_CXI_COLL_FABRIC_MGR_URL"
#define SLINGSHOT_FI_CXI_HWCOLL_ADDRS_PER_JOB_ENV "FI_CXI_HWCOLL_ADDRS_PER_JOB"
#define SLINGSHOT_FI_CXI_HWCOLL_MIN_NODES_ENV     "FI_CXI_HWCOLL_MIN_NODES"

/* Global variables */
extern slingshot_state_t slingshot_state;
extern slingshot_config_t slingshot_config;
extern bool active_outside_ctld;

/* Global functions */
/* apinfo.c */
extern bool create_slingshot_apinfo(const stepd_step_rec_t *step);
extern void remove_slingshot_apinfo(const stepd_step_rec_t *step);
/* collectives.c */
extern bool slingshot_init_collectives(void);
extern void slingshot_fini_collectives(void);
extern bool slingshot_setup_collectives(slingshot_stepinfo_t *job,
					uint32_t node_cnt, uint32_t job_id,
					uint32_t step_id);
extern void slingshot_collectives_env(slingshot_stepinfo_t *job, char ***env);
extern void slingshot_release_collectives_job_step(slingshot_stepinfo_t *job);
extern void slingshot_release_collectives_job(uint32_t job_id);
/* config.c */
extern void slingshot_free_config(void);
extern bool slingshot_stepd_init(const char *switch_params);
extern bool slingshot_setup_config(const char *switch_params);
extern int slingshot_update_vni_table(void);
extern bool slingshot_setup_job_vni_pool(job_record_t *job_ptr);
extern bool slingshot_setup_job_step_vni(
	slingshot_stepinfo_t *job, int node_cnt,
	uint32_t job_id, const char *network_params,
	const char *job_network_params);
extern void slingshot_free_job_step_vni(slingshot_stepinfo_t *job);
extern void slingshot_free_job_vni(uint32_t job_id);
extern void slingshot_free_job_vni_pool(slingshot_jobinfo_t *job);
extern void slingshot_free_jobinfo(slingshot_jobinfo_t *jobinfo);
/* setup_nic.c */
extern bool slingshot_open_cxi_lib(slingshot_stepinfo_t *job);
extern bool slingshot_create_services(slingshot_stepinfo_t *job, uint32_t uid,
				      uint16_t step_cpus, uint32_t job_id);
extern bool slingshot_destroy_services(slingshot_stepinfo_t *job,
				       uint32_t job_id);
extern void slingshot_free_services(void);
extern int slingshot_update_config(slingshot_jobinfo_t *jobinfo);

#endif
