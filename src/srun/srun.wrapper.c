/* 
 * srun.wrapper.c - srun command wrapper for use with the TotalView debugger
 *	srun is the SLURM parallel job initiator and resource allocator
 *	TotalView is a parallel job debugger from Etnus <http://www.etnus.com>
 *
 * Type "<ctrl-a>" to specify arguments for srun
 * Type "g" to start the program
 */

extern int srun(int argc, char **argv);

int main(int argc, char **argv)
{
	return srun(argc, argv);
}
