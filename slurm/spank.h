/*****************************************************************************\
 *  spank.h - Stackable Plug-in Architecture for Node job Kontrol
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  CODE-OCEC-09-009. All rights reserved.
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
 *   slurmd
 *        `-> slurmd_init()
 *        |
 *        `-> job_prolog()
 *        |
 *        | `-> slurmstepd
 *        |      `-> init ()
 *        |       -> process spank options
 *        |       -> init_post_opt ()
 *        |      + drop privileges (initgroups(), seteuid(), chdir())
 *        |      `-> user_init ()
 *        |      + for each task
 *        |      |       + fork ()
 *        |      |       |
 *        |      |       + reclaim privileges
 *        |      |       `-> task_init_privileged ()
 *        |      |       |
 *        |      |       + become_user ()
 *        |      |       `-> task_init ()
 *        |      |       |
 *        |      |       + execve ()
 *        |      |
 *        |      + reclaim privileges
 *        |      + for each task
 *        |      |     `-> task_post_fork ()
 *        |      |
 *        |      + for each task
 *        |      |       + wait ()
 *        |      |          `-> task_exit ()
 *        |      `-> exit ()
 *        |
 *        `---> job_epilog()
 *        |
 *        `-> slurmd_exit()
 *
 *   In srun only the init(), init_post_opt() and local_user_init(), and exit()
 *    callbacks are used.
 *
 *   In sbatch/salloc only the init(), init_post_opt(), and exit() callbacks
 *    are used.
 *
 *   In slurmd proper, only the slurmd_init(), slurmd_exit(), and
 *    job_prolog/epilog callbacks are used.
 *
 */

extern spank_f slurm_spank_init;
extern spank_f slurm_spank_slurmd_init;
extern spank_f slurm_spank_job_prolog;
extern spank_f slurm_spank_init_post_opt;
extern spank_f slurm_spank_local_user_init;
extern spank_f slurm_spank_user_init;
extern spank_f slurm_spank_task_init_privileged;
extern spank_f slurm_spank_task_init;
extern spank_f slurm_spank_task_post_fork;
extern spank_f slurm_spank_task_exit;
extern spank_f slurm_spank_job_epilog;
extern spank_f slurm_spank_slurmd_exit;
extern spank_f slurm_spank_exit;


/*  Items which may be obtained from the spank handle using the
 *   spank_get_item () call. The expected list of variable arguments may
 *   be found in the comments below.
 *
 *  For example, S_JOB_NCPUS takes (uint16_t *), a pointer to uint16_t, so
 *   the get item call would look like:
 *
 *    uint16_t ncpus;
 *    spank_err_t rc = spank_get_item (spank, S_JOB_NCPUS, &ncpus);
 *
 *   while  S_JOB_PID_TO_GLOBAL_ID takes (pid_t, uint32_t *), so it would
 *   be called as:
 *
 *    uint32_t global_id;
 *    spank_err_t rc;
 *    rc = spank_get_item (spank, S_JOB_PID_TO_GLOBAL_ID, pid, &global_id);
 */
enum spank_item {
    S_JOB_UID,               /* User id (uid_t *)                            */
    S_JOB_GID,               /* Primary group id (gid_t *)                   */
    S_JOB_ID,                /* SLURM job id (uint32_t *)                    */
    S_JOB_STEPID,            /* SLURM job step id (uint32_t *)               */
    S_JOB_NNODES,            /* Total number of nodes in job (uint32_t *)    */
    S_JOB_NODEID,            /* Relative id of this node (uint32_t *)        */
    S_JOB_LOCAL_TASK_COUNT,  /* Number of local tasks (uint32_t *)           */
    S_JOB_TOTAL_TASK_COUNT,  /* Total number of tasks in job (uint32_t *)    */
    S_JOB_NCPUS,             /* Number of CPUs used by this job (uint16_t *) */
    S_JOB_ARGV,              /* Command args (int *, char ***)               */
    S_JOB_ENV,               /* Job env array (char ***)                     */
    S_TASK_ID,               /* Local task id (int *)                        */
    S_TASK_GLOBAL_ID,        /* Global task id (uint32_t *)                  */
    S_TASK_EXIT_STATUS,      /* Exit status of task if exited (int *)        */
    S_TASK_PID,              /* Task pid (pid_t *)                           */
    S_JOB_PID_TO_GLOBAL_ID,  /* global task id from pid (pid_t, uint32_t *)  */
    S_JOB_PID_TO_LOCAL_ID,   /* local task id from pid (pid_t, uint32_t *)   */
    S_JOB_LOCAL_TO_GLOBAL_ID,/* local id to global id (uint32_t, uint32_t *) */
    S_JOB_GLOBAL_TO_LOCAL_ID,/* global id to local id (uint32_t, uint32_t *) */
    S_JOB_SUPPLEMENTARY_GIDS,/* Array of suppl. gids (gid_t **, int *)       */
    S_SLURM_VERSION,         /* Current SLURM version (char **)              */
    S_SLURM_VERSION_MAJOR,   /* SLURM version major release (char **)        */
    S_SLURM_VERSION_MINOR,   /* SLURM version minor release (char **)        */
    S_SLURM_VERSION_MICRO,   /* SLURM version micro release (char **)        */
    S_STEP_CPUS_PER_TASK,    /* CPUs allocated per task (=1 if --overcommit
                              * option is used, uint32_t *)                  */
    S_JOB_ALLOC_CORES,       /* Job allocated cores in list format (char **) */
    S_JOB_ALLOC_MEM,         /* Job allocated memory in MB (uint32_t *)      */
    S_STEP_ALLOC_CORES,      /* Step alloc'd cores in list format  (char **) */
    S_STEP_ALLOC_MEM,        /* Step alloc'd memory in MB (uint32_t *)       */
    S_SLURM_RESTART_COUNT    /* Job restart count (uint32_t *)               */
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
    ESPANK_ENV_NOEXIST = 5, /* No such environment variable                  */
    ESPANK_NOSPACE     = 6, /* Buffer too small.                             */
    ESPANK_NOT_REMOTE  = 7, /* Function only may be called in remote context */
    ESPANK_NOEXIST     = 8, /* Id/pid doesn't exist on this node             */
    ESPANK_NOT_EXECD   = 9, /* Lookup by pid requested, but no tasks running */
    ESPANK_NOT_AVAIL   = 10,/* SPANK item not available from this callback   */
    ESPANK_NOT_LOCAL   = 11,/* Function only valid in local/alloc context    */
};

typedef enum spank_err spank_err_t;

/*
 *  SPANK plugin context
 */
enum spank_context {
    S_CTX_ERROR,             /* Error obtaining current context              */
    S_CTX_LOCAL,             /* Local context (srun)                         */
    S_CTX_REMOTE,            /* Remote context (slurmstepd)                  */
    S_CTX_ALLOCATOR,         /* Allocator context (sbatch/salloc)            */
    S_CTX_SLURMD,            /* slurmd context                               */
    S_CTX_JOB_SCRIPT         /* prolog/epilog context                        */
};

#define HAVE_S_CTX_SLURMD 1     /* slurmd context supported                  */
#define HAVE_S_CTX_JOB_SCRIPT 1 /* job script (prolog/epilog) supported      */

typedef enum spank_context spank_context_t;

/*
 *  SPANK plugin options
 */

/*
 *  SPANK option callback. `val' is an integer value provided by
 *   the plugin to distinguish between plugin-local options, `optarg'
 *   is an argument passed by the user (if applicable), and `remote'
 *   specifies whether this call is being made locally (e.g. in srun)
 *   or remotely (e.g. in slurmstepd/slurmd).
 */
typedef int (*spank_opt_cb_f) (int val, const char *optarg, int remote);

struct spank_option {
    char *         name;    /* long option provided by plugin               */
    char *         arginfo; /* one word description of argument if required */
    char *         usage;   /* Usage text                                   */
    int            has_arg; /* Does option require argument?                */
    int            val;     /* value to return using callback               */
    spank_opt_cb_f cb;      /* Callback function to check option value      */
};

/*
 *  Plugins may export a spank_options option table as symbol "spank_options".
 *   This method only works in "local" and "remote" mode. To register options
 *   in "allocator" mode (sbatch/salloc), use the preferred
 *   spank_option_register function described below.
 */
extern struct spank_option spank_options [];

/*
 *  SPANK plugin option table must end with the following entry:
 */
#define SPANK_OPTIONS_TABLE_END { NULL, NULL, NULL, 0, 0, NULL }

/*
 *  Maximum allowed length of SPANK option name:
 */
#define SPANK_OPTION_MAXLEN      75


/*  SPANK interface prototypes
 */
BEGIN_C_DECLS

/*
 *  Return the string representation of a spank_err_t error code.
 */
const char *spank_strerror (spank_err_t err);

/*
 *  Determine whether a given spank plugin symbol is supported
 *   in this version of SPANK interface.
 *
 *  Returns:
 *  = 1   The symbol is supported
 *  = 0   The symbol is not supported
 *  = -1  Invalid argument
 */
int spank_symbol_supported (const char *symbol);

/*
 *  Determine whether plugin is loaded in "remote" context
 *
 *  Returns:
 *  = 1   remote context, i.e. plugin is loaded in /slurmstepd.
 *  = 0   not remote context
 *  < 0   spank handle was not valid.
 */
int spank_remote (spank_t spank);

/*
 *  Return the context in which the calling plugin is loaded.
 *
 *  Returns the spank_context for the calling plugin, or SPANK_CTX_ERROR
 *   if the current context cannot be determined.
 */
spank_context_t spank_context (void);

/*
 *  Register a plugin-provided option dynamically. This function
 *   is only valid when called from slurm_spank_init(), and must
 *   be guaranteed to be called in all contexts in which it is
 *   used (local, remote, allocator).
 *
 *  This function is the only method to register options in
 *   allocator context.
 *
 *  May be called multiple times to register many options.
 *
 *  Returns ESPANK_SUCCESS on successful registration of the option
 *   or ESPANK_BAD_ARG if not called from slurm_spank_init().
 */
spank_err_t spank_option_register (spank_t spank, struct spank_option *opt);

/*
 *  Check whether spank plugin option [opt] has been activated.
 *   If the option takes an argument, then the option argument
 *   (if found) will be returned in *optarg.
 *
 *  Returns
 *   ESPANK_SUCCESS if the option was used by user. In this case
 *    *optarg will contain the option argument if opt->has_arg != 0.
 *   ESPANK_ERROR if the option wasn't used.
 *   ESPANK_BAD_ARG if an invalid argument was passed to the function,
 *    such as NULL opt, NULL opt->name, or NULL optarg when opt->has_arg != 0.
 *   ESPANK_NOT_AVAIL if called from improper context.
 */
spank_err_t spank_option_getopt (spank_t spank, struct spank_option *opt,
	char **optarg);


/*  Get the value for the current job or task item specified,
 *   storing the result in the subsequent pointer argument(s).
 *   Refer to the spank_item_t comments for argument types.
 *   For S_JOB_ARGV, S_JOB_ENV, and S_SLURM_VERSION* items
 *   the result returned to the caller should not be freed or
 *   modified.
 *
 *  Returns ESPANK_SUCCESS on success, ESPANK_NOTASK if an S_TASK*
 *   item is requested from outside a task context, ESPANK_BAD_ARG
 *   if invalid args are passed to spank_get_item or spank_get_item
 *   is called from an invalid context, and ESPANK_NOT_REMOTE
 *   if not called from slurmstepd context or spank_user_local_init.
 */
spank_err_t spank_get_item (spank_t spank, spank_item_t item, ...);

/*  Place a copy of environment variable "var" from the job's environment
 *   into buffer "buf" of size "len."
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *    ESPANK_BAD_ARG      = spank handle invalid or len < 0.
 *    ESPANK_ENV_NOEXIST  = environment variable doesn't exist in job's env.
 *    ESPANK_NOSPACE      = buffer too small, truncation occurred.
 *    ESPANK_NOT_REMOTE   = not called in remote context (i.e. from slurmd).
 */
spank_err_t spank_getenv (spank_t spank, const char *var, char *buf, int len);

/*
 *  Set the environment variable "var" to "val" in the environment of
 *   the current job or task in the spank handle. If overwrite != 0 an
 *   existing value for var will be overwritten.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *     ESPANK_ENV_EXISTS  = var exists in job env and overwrite == 0.
 *     ESPANK_BAD_ARG     = spank handle invalid or var/val are NULL.
 *     ESPANK_NOT_REMOTE  = not called from slurmstepd.
 */
spank_err_t spank_setenv (spank_t spank, const char *var, const char *val,
        int overwrite);

/*
 *  Unset environment variable "var" in the environment of current job or
 *   task in the spank handle.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *    ESPANK_BAD_ARG   = spank handle invalid or var is NULL.
 *    ESPANK_NOT_REMOTE = not called from slurmstepd.
 */
spank_err_t spank_unsetenv (spank_t spank, const char *var);

/*
 *  Set an environment variable "name" to "value" in the "job control"
 *   environment, which is an extra set of environment variables
 *   included in the environment of the SLURM prolog and epilog
 *   programs. Environment variables set via this function will
 *   be prepended with SPANK_ to differentiate them from other env
 *   vars, and to avoid security issues.
 *
 *  Returns ESPANK_SUCCESS on success, o/w/ spank_err_t on failure:
 *     ESPANK_ENV_EXISTS  = var exists in control env and overwrite == 0.
 *     ESPANK_NOT_LOCAL   = not called in local context
 */
spank_err_t spank_job_control_setenv (spank_t sp, const char *name,
        const char *value, int overwrite);

/*
 *  Place a copy of environment variable "name" from the job control
 *   environment into a buffer buf of size len.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *     ESPANK_BAD_ARG     = invalid spank handle or len <= 0
 *     ESPANK_ENV_NOEXIST = environment var does not exist in control env
 *     ESPANK_NOSPACE     = buffer too small, truncation occurred.
 *     ESPANK_NOT_LOCAL   = not called in local context
 */
spank_err_t spank_job_control_getenv (spank_t sp, const char *name,
        char *buf, int len);

/*
 *  Unset environment variable "name" in the job control environment.
 *
 *  Returns ESPANK_SUCCESS on success, o/w spank_err_t on failure:
 *     ESPANK_BAD_ARG   = invalid spank handle or var is NULL
 *     ESPANK_NOT_LOCAL   = not called in local context
 */
spank_err_t spank_job_control_unsetenv (spank_t sp, const char *name);

/*
 *  SLURM logging functions which are exported to plugins.
 */
extern void slurm_info (const char *format, ...)
  __attribute__ ((format (printf, 1, 2)));
extern void slurm_error (const char *format, ...)
  __attribute__ ((format (printf, 1, 2)));
extern void slurm_verbose (const char *format, ...)
  __attribute__ ((format (printf, 1, 2)));
extern void slurm_debug (const char *format, ...)
  __attribute__ ((format (printf, 1, 2)));
extern void slurm_debug2 (const char *format, ...)
  __attribute__ ((format (printf, 1, 2)));
extern void slurm_debug3 (const char *format, ...)
  __attribute__ ((format (printf, 1, 2)));

END_C_DECLS

/*
 *  All spank plugins must issue the following for the SLURM plugin
 *   loader.
 */
#define SPANK_PLUGIN(__name, __ver) \
    const char plugin_name [] = #__name; \
    const char plugin_type [] = "spank"; \
    const unsigned int plugin_version = __ver;


#endif /* !SPANK_H */
