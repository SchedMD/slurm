#ifndef _SLURM_RETURN_CODES_H
#define _SLURM_RETURN_CODES_H

/* general return codes */
#define SLURM_SUCCESS 0
#define SLURM_ERROR -1 
#define SLURM_FAILURE -1
/* to mimick bash on task launch failure*/
#define SLURM_EXIT_FAILURE_CODE 127
#endif
