#ifndef _SLURMD_PIPES_H_
#define _SLURMD_PIPES_H_

/*pipes.c*/
/* init_parent_pipes
 * initializes pipes in the parent to be used for child io ipc after fork and exec
 * IN pipes	- array of six file desciptors
 * OUT int	- return_code
 */
int init_parent_pipes(int *pipes);

/* setup_parent_pipes 
 * setups the parent side of the pipes after fork 
 * IN pipes	- array of six file desciptors
 */
void setup_parent_pipes(int *pipes);


/* setup_child_pipes
 * setups the child side of the pipes after fork
 * IN pipes	- array of six file desciptors
 * OUT int	- return_code
 */
int setup_child_pipes(int *pipes);

/* cleanup_parent_pipes
 * cleans up the parent side of the pipes after task exit
 * IN pipes	- array of six file desciptors
 */
void cleanup_parent_pipes(int *pipes);

#endif /* !_SLURMD_PIPES_H */
