/*****************************************************************************\
 *  switch_cray.h - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Copyright 2013 Cray Inc. All Rights Reserved.
 *  Written by Danny Auble <da@schedmd.com>
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

#ifndef SWITCH_CRAY_H
#define SWITCH_CRAY_H

#include "config.h"

#include "src/common/slurm_xlator.h"

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
#define LEGACY_SPOOL_DIR	"/var/opt/cray/alps/spool/"
#endif

// Magic value signifying that jobinfo wasn't NULL
#define CRAY_JOBINFO_MAGIC	0xCAFECAFE

// Magic value signifying that jobinfo was NULL, don't unpack
#define CRAY_NULL_JOBINFO_MAGIC	0xDEAFDEAF

// Maximum network resource scaling (in percent)
#define MAX_SCALING		100

// Minimum network resource scaling (in percent)
#define MIN_SCALING		1

// Maximum concurrent job steps per node (based on network limits)
#define MAX_STEPS_PER_NODE	4

// alpsc_pre_suspend() timeout
#define SUSPEND_TIMEOUT_MSEC	(10*1000)

// Environment variables set for each task
#define CRAY_NUM_COOKIES_ENV	"CRAY_NUM_COOKIES"
#define CRAY_COOKIES_ENV	"CRAY_COOKIES"
#define PMI_CONTROL_PORT_ENV	"PMI_CONTROL_PORT"
#define PMI_CRAY_NO_SMP_ENV	"PMI_CRAY_NO_SMP_ORDER"

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

	// Port (for compatibility with 14.03, remove in the future)
	uint32_t port;

	// Cray Application ID (Slurm hash)
	uint64_t apid;
} slurm_cray_jobinfo_t;

/**********************************************************
 * Global variables
 **********************************************************/
// Debug flags
extern uint64_t debug_flags;

/**********************************************************
 * Function declarations
 **********************************************************/
// Implemented in pe_info.c
#if defined(HAVE_NATIVE_CRAY) || defined(HAVE_CRAY_NETWORK)
extern int build_alpsc_pe_info(stepd_step_rec_t *job,
			       alpsc_peInfo_t *alpsc_pe_info, int *cmd_index);
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
extern int remove_spool_files(uint64_t apid);
extern int create_apid_dir(uint64_t apid, uid_t uid, gid_t gid);
extern int set_job_env(stepd_step_rec_t *job, slurm_cray_jobinfo_t *sw_job);

extern void print_jobinfo(slurm_cray_jobinfo_t *job);

// Implemented in iaa.c
#if defined(HAVE_NATIVE_CRAY_GA) || defined(HAVE_CRAY_NETWORK)
extern int write_iaa_file(stepd_step_rec_t *job, slurm_cray_jobinfo_t *sw_job,
		   int *ptags, int num_ptags, alpsc_peInfo_t *alpsc_pe_info);
extern void unlink_iaa_file(slurm_cray_jobinfo_t *job);

// Implemented in cookies.c
extern int start_lease_extender(void);
extern int cleanup_lease_extender(void);
extern int lease_cookies(slurm_cray_jobinfo_t *job, int32_t *nodes,
			 int32_t num_nodes);
extern int track_cookies(slurm_cray_jobinfo_t *job);
extern int release_cookies(slurm_cray_jobinfo_t *job);
#endif /* HAVE_NATIVE_CRAY_GA || HAVE_CRAY_NETWORK */

/**********************************************************
 * Macros
 **********************************************************/
#define ALPSC_CN_DEBUG(f) alpsc_debug(THIS_FILE, __LINE__, __func__, \
					rc, 1, f, &err_msg);
#define ALPSC_SN_DEBUG(f) alpsc_debug(THIS_FILE, __LINE__, __func__, \
					rc, 0, f, &err_msg);
#define CRAY_ERR(fmt, ...) error("(%s: %d: %s) "fmt, THIS_FILE, __LINE__, \
				 __func__, ##__VA_ARGS__);
#define CRAY_DEBUG(fmt, ...) debug2("(%s: %d: %s) "fmt, THIS_FILE, __LINE__, \
				    __func__, ##__VA_ARGS__);
#define CRAY_INFO(fmt, ...) info("(%s: %d: %s) "fmt, THIS_FILE, __LINE__, \
				 __func__, ##__VA_ARGS__);

#endif /* SWITCH_CRAY_H */
