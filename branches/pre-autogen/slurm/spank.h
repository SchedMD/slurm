/*****************************************************************************\
 *  spank.h - Stackable Plug-in Architecture for Node job Kontrol
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#ifndef SPANK_H
#define SPANK_H

#undef BEGIN_C_DECLS
#undef END_C_DECLS
#ifdef __cplusplus
#  define BEGIN_C_DECLS         extern "C" {
#  define END_C_DECLS           }
#else  /* !__cplusplus */
#  define BEGIN_C_DECLS         /* empty */
#  define END_C_DECLS           /* empty */
#endif /* !__cplusplus */

/*  SPANK handle. Plug-in's context for running SLURM job
 */
typedef struct spank_handle * spank_t;

/*  Prototype for all spank plugin operations
 */
typedef int (spank_f) (spank_t spank, int ac, char *argv[]);

/*  SPANK plugin operations. SPANK plugin should have at least one of 
 *   these functions defined non-NULL.
 *
 *  Plug-in callbacks are completed at the following points in slurmd:
 *
 *   slurmd -> slurmstepd
 *               `-> init ()
 *               + drop privileges (initgroups(), seteuid(), chdir()) 
 *               `-> user_init ()  
 *               + for each task
 *               |       + fork ()
 *               |       `-> user_task_init ()
 *               |       + execve ()
 *               |
 *               + reclaim privileges
 *               + for each task 
 *               |     `-> task_post_fork ()
 *               |
 *               + for each task
 *               |       + wait ()
 *               |          `-> task_exit ()
 *               `-> fini ()
 *
 */

extern spank_f slurm_spank_init;
extern spank_f slurm_spank_user_init;
extern spank_f slurm_spank_task_init;
extern spank_f slurm_spank_task_post_fork;
extern spank_f slurm_spank_task_exit;
extern spank_f slurm_spank_exit;


/*  Items which may be obtained from the spank handle using the
 *   spank_get_item () call.
 */
enum spank_item {
    S_JOB_UID,              /* User id (uid_t)                               */
    S_JOB_GID,              /* Primary group id (gid_t)                      */
    S_JOB_ID,               /* SLURM job id (uint32_t)                       */ 
    S_JOB_STEPID,           /* SLURM job step id (uint32_t)                  */ 
    S_JOB_NNODES,           /* Total number of nodes in job (uint32_t)       */ 
    S_JOB_NODEID,           /* Relative id of this node (uint32_t)           */
    S_JOB_LOCAL_TASK_COUNT, /* Number of local tasks (uint32_t)              */
    S_JOB_TOTAL_TASK_COUNT, /* Total number of tasks in job (uint32_t)       */ 
    S_JOB_NCPUS,            /* Number of CPUs used by this job (uint16_t)    */
    S_JOB_ARGV,             /* Command args (int, char **)                   */
    S_JOB_ENV,              /* Job env array (char **)                       */
    S_TASK_ID,              /* Local task id (int)                           */
    S_TASK_GLOBAL_ID,       /* Global task id (uint32_t)                     */ 
    S_TASK_EXIT_STATUS,     /* Exit status of task if exited (int)           */
    S_TASK_PID              /* Task pid (pid_t)                              */
};

typedef enum spank_item spank_item_t;

/*  SPANK error codes.
 */
enum spank_err {
    ESPANK_SUCCESS     = 0, /* Success.                                      */
    ESPANK_ERROR       = 1, /* Generic error.                                */
    ESPANK_BAD_ARG     = 2, /* Bad argument.                                 */
    ESPANK_NOT_TASK    = 3, /* Not in task context.                          */
    ESPANK_ENV_EXISTS  = 4, /* Environment variable exists && !overwrite     */
    ESPANK_ENV_NOEXIST = 5, /* No such environemtn variable                  */
    ESPANK_NOSPACE     = 6  /* Buffer too small.                             */
};

typedef enum spank_err spank_err_t;


/*  SPANK interface prototypes
 */
BEGIN_C_DECLS

/*  Get the value for the current job or task item specified, 
 *   storing the result in the subsequent pointer argument(s).
 *   Refer to the spank_item_t comments for argument types.
 *   For S_JOB_ARGV and S_JOB_ENV items the result returned to
 *   the caller should not be freed or modified.
 *   
 *  Returns ESPANK_SUCCESS on success, ESPANK_NOTASK if an S_TASK*
 *   item is requested from outside a task context, ESPANK_BAD_ARG
 *   if invalid args are passed to spank_get_item.
 */
spank_err_t spank_get_item (spank_t spank, spank_item_t item, ...);

/*  Place a copy of environment variable "var" from the job's environment
 *   into buffer "buf" of size "len." 
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *    ESPANK_BAD_ARG      = spank handle invalid or len < 0.
 *    ESPANK_ENV_NOEXIST  = environment variable doesn't exist in job's env.
 *    ESPANK_NOSPACE      = buffer too small, truncation occurred.
 */
spank_err_t spank_getenv (spank_t spank, const char *var, char *buf, int len);

/* 
 *  Set the environment variable "var" to "val" in the environment of
 *   the current job or task in the spank handle. If overwrite != an
 *   existing value for var will be overwritten.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *     ESPANK_ENV_EXISTS  = var exists in job env and overwrite == 0.
 *     ESPANK_BAD_ARG     = spank handle invalid or var/val are NULL.
 */
spank_err_t spank_setenv (spank_t spank, const char *var, const char *val, 
        int overwrite);

/*
 *  SLURM logging functions which are exported to plugins.
 */
extern void slurm_info (const char *format, ...);
extern void slurm_error (const char *format, ...);
extern void slurm_verbose (const char *format, ...);
extern void slurm_debug (const char *format, ...);
extern void slurm_debug2 (const char *format, ...);
extern void slurm_debug3 (const char *format, ...);

END_C_DECLS

/*
 *  All spank plugins must issue the following for the SLURM plugin
 *   loader.
 */
#define SPANK_PLUGIN(__name, __ver) \
    const char plugin_name [] = "__name"; \
    const char plugin_type [] = "spank/__name"; \
    const unsigned int plugin_version = __ver;


#endif /* !SPANK_H */
