#include <unistd.h>
#include <errno.h>

#include <src/common/slurm_errno.h>
#include <src/common/log.h>
#include <src/slurmd/pipes.h>
#include <src/slurmd/io.h>

void setup_parent_pipes(int *pipes)
{
	close(pipes[CHILD_IN_RD_PIPE]);
	close(pipes[CHILD_OUT_WR_PIPE]);
	close(pipes[CHILD_ERR_WR_PIPE]);
}

void cleanup_parent_pipes(int *pipes)
{
	close(pipes[CHILD_IN_WR_PIPE]);
	close(pipes[CHILD_OUT_RD_PIPE]);
	close(pipes[CHILD_ERR_RD_PIPE]);
}

int init_parent_pipes(int *pipes)
{
	int rc;

	/* open pipes to be used in dup after fork */
	if ((rc = pipe(&pipes[CHILD_IN_PIPE]))) 
		slurm_seterrno_ret(ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN);
	if ((rc = pipe(&pipes[CHILD_OUT_PIPE]))) 
		slurm_seterrno_ret(ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN);
	if ((rc = pipe(&pipes[CHILD_ERR_PIPE]))) 
		slurm_seterrno_ret(ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN);

	return SLURM_SUCCESS;
}

int setup_child_pipes(int *pipes)
{
	int error_code = SLURM_SUCCESS;
	int local_errno;

	/* dup stdin */
	/* close ( STDIN_FILENO ); */

	if (SLURM_ERROR ==
	    (error_code |= dup2(pipes[CHILD_IN_RD_PIPE], STDIN_FILENO))) {
		error("dup failed on child standard in pipe %d: %m",
		     pipes[CHILD_IN_RD_PIPE]);
	}
	close(pipes[CHILD_IN_RD_PIPE]);
	close(pipes[CHILD_IN_WR_PIPE]);

	/* dup stdout */
	/* close ( STDOUT_FILENO ); */
	if (SLURM_ERROR ==
	    (error_code |=
	     dup2(pipes[CHILD_OUT_WR_PIPE], STDOUT_FILENO))) {
		error("dup failed on child standard out pipe %i: %m",
				pipes[CHILD_OUT_WR_PIPE]);
	}
	close(pipes[CHILD_OUT_RD_PIPE]);
	close(pipes[CHILD_OUT_WR_PIPE]);

	/* dup stderr  */
	/* close ( STDERR_FILENO ); */
	if (SLURM_ERROR ==
	    (error_code |=
	     dup2(pipes[CHILD_ERR_WR_PIPE], STDERR_FILENO))) {
		error("dup failed on child standard err pipe %i: %m",
		     pipes[CHILD_ERR_WR_PIPE]);
	}
	close(pipes[CHILD_ERR_RD_PIPE]);
	close(pipes[CHILD_ERR_WR_PIPE]);
	return error_code;
}
