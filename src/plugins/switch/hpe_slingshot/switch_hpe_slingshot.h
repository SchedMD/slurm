/*****************************************************************************\
 *  switch_hpe_slingshot.h - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2021-2022 Hewlett Packard Enterprise Development LP
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
#define SLINGSHOT_STATE_VERSION 1

/* State file name */
#define SLINGSHOT_STATE_FILE "slingshot_state"

/* New state file name (for atomic replacing) */
#define SLINGSHOT_STATE_FILE_NEW "slingshot_state.new"

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

/* Number of Slingshot VNI "PIDs"/device */
#define SLINGSHOT_VNI_PIDS 256
/* Max size for Slingshot VNI "PIDs" job file (256 bits in hex) */
#define SLINGSHOT_VNI_PIDS_BUFSIZ ((SLINGSHOT_VNI_PIDS / 4) + 3)
/* Format for VNI "PIDs" job file name */
#define SLINGSHOT_VNI_PIDS_FMT "%s/vni_pids.%u" /* <spooldir>, <job_id> */

/* Number of retries for destroying CXI services */
#define SLINGSHOT_CXI_DESTROY_RETRIES 5

/*
 * Values/directories/filenames for jackaloped BASIC/OAUTH authentication
 */
typedef enum {
	SLINGSHOT_JLOPE_AUTH_NONE = 0, /* No authentication */
	SLINGSHOT_JLOPE_AUTH_BASIC,    /* User name and password */
	SLINGSHOT_JLOPE_AUTH_OAUTH     /* OAuth2 client credentials grant */
} jlope_auth_t;
#define SLINGSHOT_JLOPE_AUTH_BASIC_STR  "BASIC" /* jlope_auth token */
#define SLINGSHOT_JLOPE_AUTH_OAUTH_STR  "OAUTH" /* jlope_auth token */
#define SLINGSHOT_JLOPE_AUTH_BASIC_USER "cxi"   /* user name for BASIC auth */
#define SLINGSHOT_JLOPE_TIMEOUT         10      /* timeout for REST calls */
#define SLINGSHOT_JLOPE_CONNECT_TIMEOUT 10      /* timeout for REST connect */
#define SLINGSHOT_JLOPE_AUTH_BASIC_DIR                "/etc/jackaloped"
#define SLINGSHOT_JLOPE_AUTH_OAUTH_DIR                "/etc/wlm-client-auth"
#define SLINGSHOT_JLOPE_AUTH_BASIC_PWD_FILE           "passwd"
#define SLINGSHOT_JLOPE_AUTH_OAUTH_CLIENT_ID_FILE     "client-id"
#define SLINGSHOT_JLOPE_AUTH_OAUTH_CLIENT_SECRET_FILE "client-secret"
#define SLINGSHOT_JLOPE_AUTH_OAUTH_ENDPOINT_FILE      "endpoint"

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
	uint8_t single_node_vni;        /* Allocate VNIs for single-node apps */
	uint8_t job_vni;                /* Allocate extra VNI per-job */
	uint32_t tcs;                   /* Bitmap of default traffic classes */
	uint32_t flags;                 /* Bitmap of configuration flags */
	slingshot_limits_set_t limits;  /* Set of NIC resource limits */
	char *jlope_url;                /* URL of jackaloped REST interface */
	jlope_auth_t jlope_auth;        /* jackaloped authentication type */
	char *jlope_authdir;            /* directory containing auth files */
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

/* Denotes packing a null jobinfo structure */
#define SLINGSHOT_JOBINFO_NULL_VERSION 0xDEAFDEAF

/* Jobinfo structure passed from slurmctld to slurmd */
typedef struct slingshot_jobinfo {
	uint32_t version;      /* Version of this structure */
	uint32_t num_vnis;     /* Number of VNIs */
	uint16_t *vnis;        /* List of VNIs allocated for this application */
	uint32_t tcs;          /* Bitmap of allowed traffic classes */
	slingshot_limits_set_t limits; /* Set of NIC resource limits */
	uint32_t depth;        /* Threads-per-task for limit calculation */
	uint32_t num_profiles; /* Number of communication profiles */
	slingshot_comm_profile_t *profiles; /* List of communication profiles */
	bitstr_t *vni_pids;    /* Set of Slingshot job VNI allocated PIDs */
	uint32_t flags;        /* Configuration flags */
	uint32_t num_nics;     /* Number of entries in 'nics' array */
	slingshot_hsn_nic_t *nics; /* HSN NIC information for instant on */
} slingshot_jobinfo_t;

/* Slingshot traffic classes (bitmap) */
#define SLINGSHOT_TC_DEDICATED_ACCESS 0x1
#define SLINGSHOT_TC_LOW_LATENCY      0x2
#define SLINGSHOT_TC_BULK_DATA        0x4
#define SLINGSHOT_TC_BEST_EFFORT      0x8
#define SLINGSHOT_TC_DEFAULT          (SLINGSHOT_TC_LOW_LATENCY | \
				       SLINGSHOT_TC_BEST_EFFORT)

/* Values for slingshot_jobinfo_t.flags */
/*
 * If SLINGSHOT_FLAGS_ADJUST_LIMITS is set (default), slurmd will adjust
 * resource limit reservations by subtracting system service reserved/used
 * resources
 * If SLINGSHOT_FLAGS_VNI_PIDS is set, slurmd will create a set of Slingshot
 * "VNI PIDs" to support overlapping job steps on compute nodes (deprecated)
 */
#define SLINGSHOT_FLAGS_ADJUST_LIMITS 0x1
#define SLINGSHOT_FLAGS_VNI_PIDS      0x2
#define SLINGSHOT_FLAGS_DEFAULT SLINGSHOT_FLAGS_ADJUST_LIMITS

/* Environment variables set for applications */
#define SLINGSHOT_SVC_IDS_ENV         "SLINGSHOT_SVC_IDS"
#define SLINGSHOT_VNIS_ENV            "SLINGSHOT_VNIS"
#define SLINGSHOT_DEVICES_ENV         "SLINGSHOT_DEVICES"
#define SLINGSHOT_TCS_ENV             "SLINGSHOT_TCS"
#define SLINGSHOT_INTER_VNI_PIDS_ENV  "SLINGSHOT_INTER_VNI_PIDS"

/* Global variables */
extern slingshot_state_t slingshot_state;
extern slingshot_config_t slingshot_config;

/* Global functions */
/* apinfo.c */
extern bool create_slingshot_apinfo(const stepd_step_rec_t *step);
extern void remove_slingshot_apinfo(const stepd_step_rec_t *step);
/* config.c */
extern void slingshot_free_config(void);
extern bool slingshot_setup_config(const char *switch_params);
extern bool slingshot_setup_job_step(slingshot_jobinfo_t *job, int node_cnt,
	uint32_t job_id, const char *network_params,
	const char *job_network_params);
extern void slingshot_free_job_step(slingshot_jobinfo_t *job);
extern void slingshot_free_job(uint32_t job_id);
/* instant_on.c */
extern bool slingshot_init_instant_on(void);
extern void slingshot_fini_instant_on(void);
extern bool slingshot_fetch_instant_on(slingshot_jobinfo_t *job,
				       char *node_list, uint32_t node_cnt);
/* setup_nic.c */
extern bool slingshot_open_cxi_lib(slingshot_jobinfo_t *job);
extern bool slingshot_create_services(slingshot_jobinfo_t *job, uint32_t uid,
				      uint16_t step_cpus, uint32_t job_id);
extern bool slingshot_destroy_services(slingshot_jobinfo_t *job,
				       uint32_t job_id);
extern void slingshot_free_services(void);

#endif
