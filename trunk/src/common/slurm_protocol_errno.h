#ifndef _SLURM_PROTOCOL_ERRNO_H
#define _SLURM_PROTOCOL_ERRNO_H

/* communcation layer RESPONSE_SLURM_RC message codes */
#define SLURM_NO_CHANGE_IN_DATA 100

/* general communication layer return codes */
#define SLURM_UNEXPECTED_MSG_ERROR 220
#define SLURM_PROTOCOL_VERSION_ERROR -100
#define SLURM_SOCKET_ERROR -1
#define SLURM_PROTOCOL_SUCCESS 0
#define SLURM_PROTOCOL_FAILURE -1

/* general return codes */
#define SLURM_SUCCESS 0
#define SLURM_FAILURE -1


#endif
