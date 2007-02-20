/*****************************************************************************\
 *  Function: ll_init_job
 *
 *  Description: This function initializes the JobManagement object and creates
 *  a listen socket which will be used by the daemons to contact the calling
 *  process.
 *
 *  Arguments:
 *    OUT jobmgmtObj : Address of the LL_element which is a handle to the
 *                     JobManagement object created.
 *    RET Success: 0
 *        Failure: -1 : Error creating listen socket.
 *                 -5 : System error.
 *                 -16 : API_NO_DCE_ID : No valid DCE login.
 *                 -17 : API_NO_DCE_CRED : Credentials have expired.
 *                 -18 : API_INSUFFICIENT_DCE_CRED : DCE credentials
 *                       within 300 secs of expiration.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Morris Jette <jette1@llnl.gov>
 * 
 *  This file is part of slurm_ll_api, a collection of LoadLeveler-compatable
 *  interfaces to Simple Linux Utility for Resource Managment (SLURM).  These 
 *  interfaces are used by POE (IBM's Parallel Operating Environment) to 
 *  initiated SLURM jobs. For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  This notice is required to be provided under our contract with the U.S.
 *  Department of Energy (DOE).  This work was produced at the University
 *  of California, Lawrence Livermore National Laboratory under Contract
 *  No. W-7405-ENG-48 with the DOE.
 * 
 *  Neither the United States Government nor the University of California
 *  nor any of their employees, makes any warranty, express or implied, or
 *  assumes any liability or responsibility for the accuracy, completeness,
 *  or usefulness of any information, apparatus, product, or process
 *  disclosed, or represents that its use would not infringe
 *  privately-owned rights.
 *
 *  Also, reference herein to any specific commercial products, process, or
 *  services by trade name, trademark, manufacturer or otherwise does not
 *  necessarily constitute or imply its endorsement, recommendation, or
 *  favoring by the United States Government or the University of
 *  California.  The views and opinions of authors expressed herein do not
 *  necessarily state or reflect those of the United States Government or
 *  the University of California, and shall not be used for advertising or
 *  product endorsement purposes.
 * 
 *  The precise terms and conditions for copying, distribution and
 *  modification are specified in the file "COPYING".
\*****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "common.h"
#include "config.h"
#include "llapi.h"
#include "msg_thread.h"

#define SLURM_DEBUG 0
#if SLURM_DEBUG
/* Excerpt from slurm's "common/src/log.h" for 
 * detailed debugging in slurm code components. */
#include <syslog.h>

typedef enum {
	SYSLOG_FACILITY_USER =          LOG_USER
}	log_facility_t;

typedef enum {
	LOG_LEVEL_QUIET = 0,
	LOG_LEVEL_FATAL,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_INFO,
	LOG_LEVEL_VERBOSE,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_DEBUG2,
	LOG_LEVEL_DEBUG3,
}	log_level_t;

typedef struct {
	log_level_t stderr_level;   /* max level to log to stderr         */
	log_level_t syslog_level;   /* max level to log to syslog         */
	log_level_t logfile_level;  /* max level to log to logfile        */
	unsigned    prefix_level:1; /* prefix level (e.g. "debug: ") if 1 */
	unsigned    buffered:1;     /* Use internal buffer to never block */
}	log_options_t;

#define LOG_OPTS_ALL_STDERR \
	{LOG_LEVEL_DEBUG3, LOG_LEVEL_QUIET, LOG_LEVEL_QUIET, 1, 0 }

int log_init(char *argv0, log_options_t opts,
		log_facility_t fac, char *logfile);
#endif

static int _valid_slurm_config(void);

extern int ll_init_job(LL_element **jobmgmtObj)
{
	slurm_elem_t *		slurm_elem;
	slurm_job_init_t *	slurm_job_init;
	job_desc_msg_t *	slurm_job_desc;

#if SLURM_DEBUG
	log_options_t log_opts = LOG_OPTS_ALL_STDERR;
	log_init("poe", log_opts, LOG_USER, "");
#endif

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_init_job\n");

	if (_valid_slurm_config() == 0)
		goto error0;

	slurm_elem = calloc(1, sizeof(slurm_elem_t));
	if (slurm_elem == NULL)
		goto error1;
	slurm_job_init = calloc(1, sizeof(slurm_job_init_t)); 
	if (slurm_job_init == NULL)
		goto error2;
	slurm_job_desc = calloc(1, sizeof(job_desc_msg_t));
	if (slurm_job_desc == NULL)
		goto error3;

	slurm_init_job_desc_msg(slurm_job_desc);
	slurm_job_desc->user_id = getuid();
	slurm_job_desc->name = "poe";

	slurm_job_init->slurm_job_desc	= slurm_job_desc;
	slurm_job_init->job_state	= JOB_PENDING;
	slurm_job_init->task_dist	= SLURM_DIST_BLOCK;
	slurm_job_init->host_list	= hostlist_create(NULL);

	slurm_elem->type = JOB_INIT;
	slurm_elem->data = slurm_job_init;
	*jobmgmtObj = slurm_elem;
	 
	/* start msg thread */
	slurm_job_init->forked_msg = malloc(sizeof(forked_msg_t));
	slurm_job_init->forked_msg->job_state = &slurm_job_init->job_state;
	msg_thr_create(slurm_job_init->forked_msg);
	
	VERBOSE("--------------------------------------------------\n");
	return 0;

error3:
	free(slurm_job_init);
error2:
	free(slurm_elem);
error1:
	ERROR("error: calloc failure\n");
error0:
	VERBOSE("--------------------------------------------------\n");
	return -5;
}

/* Validate slurm's configuration for POE.
 * RET 0 if non-usable, 1 otherwise */
static int _valid_slurm_config(void)
{
	int rc = 1;
	slurm_ctl_conf_t  *slurm_config_ptr = NULL;

	if (slurm_load_ctl_conf((time_t) 0, &slurm_config_ptr)) {
		ERROR("error: slurm_load_ctl_conf() failed\n");
		return 1;	/* can't check, so continue */
	}

	if (strcmp(slurm_config_ptr->switch_type, "switch/federation")) {
		ERROR("error: bad slurm SwitchType configured\n");
		rc = 0;
	}

	slurm_free_ctl_conf(slurm_config_ptr);

	return rc;
}

