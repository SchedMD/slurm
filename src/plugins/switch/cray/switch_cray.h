/*****************************************************************************\
 *  switch_cray.h - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Copyright 2013 Cray Inc. All Rights Reserved.
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef SWITCH_CRAY_H
#define SWITCH_CRAY_H

#if     HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>

#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
#include "alpscomm_cn.h"
#include "alpscomm_sn.h"
#endif

/**********************************************************
 * Constants
 **********************************************************/
/* This allows for a BUILD time definition of LEGACY_SPOOL_DIR on the compile
 * line.
 * LEGACY_SPOOL_DIR can be customized to wherever the builder desires.
 * This customization could be important because the default is a hard-coded
 * path that does not vary regardless of where Slurm is installed.
 */
#ifndef LEGACY_SPOOL_DIR
#define LEGACY_SPOOL_DIR	"/var/spool/alps/"
#endif

// Magic value signifying that jobinfo wasn't NULL
#define CRAY_JOBINFO_MAGIC	0xCAFECAFE

// Magic value signifying that jobinfo was NULL, don't unpack
#define CRAY_NULL_JOBINFO_MAGIC	0xDEAFDEAF

// File to save plugin state to
#define CRAY_SWITCH_STATE	"/switch_cray_state"

// Temporary file containing new plugin state
#define CRAY_SWITCH_STATE_NEW	CRAY_SWITCH_STATE".new"

// File containing previous plugin state
#define CRAY_SWITCH_STATE_OLD	CRAY_SWITCH_STATE".old"

// Minimum PMI port to allocate
#define MIN_PORT		20000

// Maximum PMI port to allocate
#define MAX_PORT		60000

// Number of ports to allocate
#define PORT_CNT		(MAX_PORT - MIN_PORT + 1)

// Length of bitmap in bytes (see _bitstr_words in src/common/bitstring.c)
#define PORT_BITMAP_LEN	((((PORT_CNT + BITSTR_MAXPOS) >> BITSTR_SHIFT) \
			 + BITSTR_OVERHEAD) * sizeof(bitstr_t))

// Number of times to attempt allocating a port when none are available
#define ATTEMPTS		2

// Maximum network resource scaling (in percent)
#define MAX_SCALING		100

// Minimum network resource scaling (in percent)
#define MIN_SCALING		1

// Maximum concurrent job steps per node (based on network limits)
#define MAX_STEPS_PER_NODE	4

// alpsc_pre_suspend() timeout
#define SUSPEND_TIMEOUT_MSEC	(10*1000)

/**********************************************************
 * Type definitions
 **********************************************************/
// Opaque Cray jobinfo structure
typedef struct slurm_cray_jobinfo {
	uint32_t magic;
	uint32_t num_cookies;	/* The number of cookies sent to configure the
	                           HSN */
	/* Double pointer to an array of cookie strings.
	 * cookie values here as NULL-terminated strings.
	 * There are num_cookies elements in the array.
	 * The caller is responsible for free()ing
	 * the array contents and the array itself.  */
	char     **cookies;
	/* The array itself must be free()d when this struct is destroyed. */
	uint32_t *cookie_ids;

	// Number of ptags allocated
	int num_ptags;

	// Array of ptags
	int *ptags;

	// Port for PMI communications
	uint32_t port;

	// Slurm job id
	uint32_t jobid;

	// Slurm step id
	uint32_t stepid;

	// Cray Application ID (Slurm hash)
	uint64_t apid;
} slurm_cray_jobinfo_t;

/**********************************************************
 * Global variables
 **********************************************************/
#ifndef HAVE_CRAY_NETWORK
// Which ports are reserved (holds PORT_CNT bits)
extern bitstr_t *port_resv;

// Last allocated port index
extern uint32_t last_alloc_port;

// Mutex controlling access to port variables
extern pthread_mutex_t port_mutex;
#endif

// Debug flags
extern uint32_t debug_flags;

/**********************************************************
 * Function declarations
 **********************************************************/
// Implemented in ports.c
extern int assign_port(uint32_t *ret_port);
extern int release_port(uint32_t real_port);

// Implemented in pe_info.c
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
extern int build_alpsc_pe_info(stepd_step_rec_t *job,
			       slurm_cray_jobinfo_t *sw_job,
			       alpsc_peInfo_t *alpsc_pe_info);
extern void free_alpsc_pe_info(alpsc_peInfo_t *alpsc_pe_info);
#endif

// Implemented in gpu.c
extern int setup_gpu(stepd_step_rec_t *job);
extern int reset_gpu(stepd_step_rec_t *job);

// Implemented in scaling.c
extern int get_cpu_scaling(stepd_step_rec_t *job);
extern int get_mem_scaling(stepd_step_rec_t *job);

// Implemented in util.c
extern int list_str_to_array(char *list, int *cnt, int32_t **numbers);
extern void alpsc_debug(const char *file, int line, const char *func,
			int rc, int expected_rc, const char *alpsc_func,
			char **err_msg);
extern int create_apid_dir(uint64_t apid, uid_t uid, gid_t gid);
extern int set_job_env(stepd_step_rec_t *job, slurm_cray_jobinfo_t *sw_job);
extern void recursive_rmdir(const char *dirnm);

extern void print_jobinfo(slurm_cray_jobinfo_t *job);

// Implemented in iaa.c
#if defined(HAVE_NATIVE_CRAY_GA) || defined(HAVE_CRAY_NETWORK)
extern int write_iaa_file(stepd_step_rec_t *job, slurm_cray_jobinfo_t *sw_job,
		   int *ptags, int num_ptags, alpsc_peInfo_t *alpsc_pe_info);
extern void unlink_iaa_file(slurm_cray_jobinfo_t *job);
#endif /* HAVE_NATIVE_CRAY_GA || HAVE_CRAY_NETWORK */

/**********************************************************
 * Macros
 **********************************************************/
#define ALPSC_CN_DEBUG(f) alpsc_debug(THIS_FILE, __LINE__, __FUNCTION__, \
					rc, 1, f, &err_msg);
#define ALPSC_SN_DEBUG(f) alpsc_debug(THIS_FILE, __LINE__, __FUNCTION__, \
					rc, 0, f, &err_msg);
#define CRAY_ERR(fmt, ...) error("(%s: %d: %s) "fmt, THIS_FILE, __LINE__, \
				 __FUNCTION__, ##__VA_ARGS__);
#define CRAY_DEBUG(fmt, ...) debug2("(%s: %d: %s) "fmt, THIS_FILE, __LINE__, \
				    __FUNCTION__, ##__VA_ARGS__);
#define CRAY_INFO(fmt, ...) info("(%s: %d: %s) "fmt, THIS_FILE, __LINE__, \
				 __FUNCTION__, ##__VA_ARGS__);

#endif /* SWITCH_CRAY_H */
