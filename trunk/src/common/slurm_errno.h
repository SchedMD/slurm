#ifndef _SLURM_ERRNO_H
#define _SLURM_ERRNO_H

#include <src/common/slurm_return_codes.h> /* 0, -1 ERROOR CODES */
#include <src/common/slurm_protocol_errno.h> /* API ONLY ERROR CODES */

/* SLRUMD error codes */
#define ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN 		90000
#define ESLURMD_KILL_TASK_FAILED			90001
#define ESLURMD_OPENSSL_ERROR				90002
#define ESLURMD_NO_AVAILABLE_JOB_STEP_SLOTS_IN_SHMEM	90003
#define ESLURMD_NO_AVAILABLE_TASK_SLOTS_IN_SHMEM	90004
#endif
