/* 
 * slaunch.wrapper.c - slaunch command wrapper for use with debuggers
 *	slaunch is the SLURM parallel application launcher
 *
 * For TotalView, a parallel job debugger from Etnus <http://www.etnus.com>
 *	Type "<ctrl-a>" to specify arguments for slaunch
 *	Type "g" to start the program
 *
 * Information for other debuggers may be submitted to slurm-dev@lists.llnl.gov
 */

extern int slaunch(int argc, char **argv);

int main(int argc, char **argv)
{
	return slaunch(argc, argv);
}
