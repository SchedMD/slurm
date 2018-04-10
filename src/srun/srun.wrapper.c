/*
 * srun.wrapper.c - srun command wrapper for use with debuggers
 *	srun is the Slurm parallel job initiator and resource allocator
 *
 * For TotalView, a parallel job debugger from
 * TotalView Technologies, LLC <http://www.TotalViewTech.com>
 *	Type "<ctrl-a>" to specify arguments for srun
 *	Type "g" to start the program
 *
 * Information for other debuggers may be submitted to slurm-dev@schedmd.com
 */

extern int srun(int argc, char **argv);

int main(int argc, char **argv)
{
	return srun(argc, argv);
}
