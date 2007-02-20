/*****************************************************************************\
 * Function:  ll_spawn_connect
 *
 * Description: This function is used to start a task on a specific
 * machine. A file descriptor connected to the spawned task is
 * returned. The caller is responsible for closing this socket. The
 * caller must also make sure that the task object specified has the
 * correct executable name to be started. The name of the machine
 * assigned to the specified task will be retrieved from the job
 * object. In some parallel programming models, the single process
 * which is spawned will start all the tasks that will run on the
 * machine so this function is called only once per machine allocated
 * to the job. A flag is used to indicate this situation so that
 * LoadLeveler will report the correct status for the job.
 *
 * Arguments:
 *   IN jobmgmtObj: Pointer to the LL_element handle returned
 *   			by the ll_init_job function.
 *   IN step: Pointer to the LL_element handle representing
 *   			the step that the task belongs to. This step
 *   			has to have been previously submitted via the 
 *   			ll_request function.
 *   IN executable: character string for the name of
 *   			executable to be started.
 *   IN taskI: If a single task is being spawned then this
 *   			argument will point to the taskInstance to be
 *   			started. If all tasks are being started by
 *   			this spawn then this argument will point to
 *   			one of the task instances to be started.
 *   IN flags: integer containing flag
 *   RET Success: Integer > 0 which is the socket connected to the task.
 *       Failure: -1: Invalid jobmgmtObj specified.
 *                -2: Invalid step specified.
 *                -3: Invalid taskInstance specified.
 *                -4: Cannot connect to the Schedd.
 *                -5: System Error.
 *                -6: NULL executable.
 *                -7: Task is already running on the taskI node.
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

extern char **environ;

static int 	_fetch_fd(LL_element *jobmgmtObj, LL_element *step,
			char *executable, LL_element *taskI, int flags);
static int 	_launch_pmd_daemons(LL_element *jobmgmtObj, LL_element *step,
			const char *executable, LL_element *taskI, int flags);

extern int ll_spawn_ready(int *fd, int fd_count, LL_element *jobmgmtObj, 
			  LL_element **error_object)
{
	return 0;
}
extern int ll_spawn_connect(int unused, LL_element *jobmgmtObj, 
			    LL_element *step, LL_element *machine,
			    char *executable, LL_element **error_object)
{
	slurm_elem_t *slurm_job_elem = (slurm_elem_t *) jobmgmtObj;
	slurm_elem_t *slurm_step_elem = (slurm_elem_t *) step;
	int rc, flags = 0;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_spawn_connect\n");

	if (jobmgmtObj == NULL) {
		ERROR("jobmgmtObj == NULL\n");
		rc = -1;
		goto done;
	}
	if (slurm_job_elem->type != JOB_INIT) {
		ERROR("invalid job elem type = %s\n",
			elem_name(slurm_job_elem->type));
		rc = -1;
		goto done;
	}

	if (step == NULL) {
		ERROR("step == NULL\n");
		rc = -2;
		goto done;
	}
	if (slurm_step_elem->type != STEP_ELEM) {
		ERROR("invalid step elem type = %s\n",
			elem_name(slurm_step_elem->type));
		rc = -2;
		goto done;
	}

	if (executable == NULL) {
		ERROR("executable == NULL\n");
		rc = -6;
		goto done;
	}

	/* get_data needs to set this before checking here */
	/* if (slurm_taski_elem->type != TASK_INST_ELEM) { */
/* 		ERROR("invalid taskI elem type = %s\n", */
/* 			elem_name(slurm_step_elem->type)); */
/* 		rc = -2; */
/* 		goto done; */
/* 	} */

	rc = _fetch_fd(jobmgmtObj, step, executable, machine, flags);

done:
	VERBOSE("--------------------------------------------------\n");
	return rc;
}


static int _fetch_fd(LL_element *jobmgmtObj, LL_element *step,
		char *executable, LL_element *machine, int flags)
{
	slurm_elem_t *step_elem = (slurm_elem_t *) step;
	slurm_step_elem_t *step_data;
	slurm_elem_t *machine_elem = (slurm_elem_t *) machine;
	slurm_taski_elem_t *taski_data;
	int fd_inx, rc;

	step_data = (slurm_step_elem_t *) step_elem->data;
	if (step_data->fd_array == NULL) {
		if ((rc = _launch_pmd_daemons(jobmgmtObj, step, executable,
					      machine, flags)) < 0) {
			ERROR("spawn failure");
			return rc; /* failure launching pmd daemons */
		}
	}
	taski_data = (slurm_taski_elem_t *) machine_elem->data;
	fd_inx = taski_data->node_inx;
	if (fd_inx < 0) {
		ERROR("Invalid node ID, task = %d\n",
			taski_data->task_id);
		return -1;
	}

	VERBOSE("Returning socket %d for node_inx=%d task_id[0]=%d\n",
		step_data->fd_array[fd_inx], fd_inx, taski_data->task_id);

	return step_data->fd_array[fd_inx];
}

static int _env_count(char **env)
{
	int i = 0;
	while (env[i] != NULL)
		i++;
	return i;
}

static int _launch_pmd_daemons(LL_element *jobmgmtObj, LL_element *step,
		const char *executable, LL_element *machine, int flags)
{
	slurm_elem_t *job_elem = (slurm_elem_t *) jobmgmtObj;
	slurm_elem_t *step_elem = (slurm_elem_t *) step;
	slurm_job_init_t *job_data;
	slurm_step_elem_t *step_data;
	char *pmd_argv[1];
	int num_fd;
	slurm_step_launch_params_t launch;
	slurm_step_launch_callbacks_t callbacks;
	char cwd[PATH_MAX];

	VERBOSE("executable = %s\n", executable);

	job_data  = (slurm_job_init_t *) job_elem->data;
	step_data = (slurm_step_elem_t *) step_elem->data;

	if (step_data->ctx == NULL) {
		ERROR("step context is NULL\n");
		return -5;
	}

	slurm_step_ctx_daemon_per_node_hack(step_data->ctx);

	slurm_step_launch_params_t_init(&launch);
	pmd_argv[0] = (char *)executable;
	launch.argv = pmd_argv;
	launch.argc = 1;
	/* Let slurm_step_launch pull in environ.  If we try to do it here
	   poe segfaults.  Not sure what is going on there... */
	launch.env = NULL;
	launch.envc = 0;
/* 	launch.env = environ; */
/* 	launch.envc = _env_count(environ); */
	getcwd(cwd, PATH_MAX);
	launch.cwd = cwd;
	launch.user_managed_io = true;

	/* not presently using callbacks */
	memset(&callbacks, 0, sizeof(callbacks));

	VERBOSE("Calling slurm_step_launch\n");
	if (slurm_step_launch(step_data->ctx, &launch, NULL)
	    != SLURM_SUCCESS) {
		ERROR("slurm_step_launch: %s\n",
		      slurm_strerror(slurm_get_errno()));
		return -5;
	}

	VERBOSE("Calling slurm_step_launch_wait_start\n");
	if (slurm_step_launch_wait_start(step_data->ctx) != SLURM_SUCCESS) {
		ERROR("slurm_step_launch_wait_start aborted");
		return -5;
	}

	VERBOSE("Calling SLURM_STEP_CTX_USER_MANGED_SOCKETS\n");
	slurm_step_ctx_get(step_data->ctx, SLURM_STEP_CTX_USER_MANAGED_SOCKETS,
			   &num_fd, &step_data->fd_array);
	if (step_data->fd_array == NULL) {
		ERROR("Unable to get pmd IO socket array\n");
		return -5;
	}

	return 0;
}

