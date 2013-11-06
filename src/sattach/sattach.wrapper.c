/*
 * sattach.wrapper.c - sattach command wrapper for use with debuggers
 *	sattach is a SLURM command that can be used to attach to an
 *	active parallel job
 *
 * For TotalView, a parallel job debugger from
 * TotalView Technologies, LLC <http://www.TotalViewTech.com>
 *	Type "<ctrl-a>" to specify arguments for sattach
 *	Type "g" to start the program
 *
 * Information for other debuggers may be submitted to slurm-dev@schedmd.com
 */

extern int sattach(int argc, char **argv);

int main(int argc, char **argv)
{
	return sattach(argc, argv);
}
