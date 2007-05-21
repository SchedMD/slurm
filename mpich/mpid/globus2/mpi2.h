#ifndef __globus2_mpi2__
#define __globus2_mpi2__

/* 
 * NOTE: discussion about G2_MAXHOSTNAMELEN
 *
 * We define an MPICH-G2-defined value for MAXHOSTNAMELEN rather than using 
 * the OS-provided value because it is imperative that * this value be exactly 
 * the same on all systems within any single computation.
 *
 * We made a design error when writing the code by making extensive use of 
 * MAXHOSTNAMELEN throughout the MPICH-G2 code.  This proved to be a fatal 
 * error when running on a set of machines where MAXHOSTNAMELEN had different 
 * values (e.g., on many Linux systems MAXHOSTNAMELEN is 64 while on most 
 * other Unix systems it is 256).  MPICH-G2 hung during initialization because
 * one proc would globus_io_write 64 bytes to another proc that was hanging on 
 * a blocking globus_io_read for a minimum of 256 bytes.  There were other
 * problems throughout the MPICH-G2 code, particularly in the MPI-2 extensions,
 * that were also rooted in this possible MAXHOSTNAMELEN value mismatch.
 *
 * We have added the (G2_MAXHOSTNAMELEN >= MAXHOSTNAMELEN) test during MPICH-G2
 * initialization, and if that fails, we abort printing an error message 
 * telling the user to (a) increase G2_MAXHOSTNAMELEN here, (b) re-build 
 * MPICH-G2, and (c) do the same on _all_ systems that you plan to run a 
 * single application on with this system ...  in other words ... the value 
 * of G2_MAXHOSTNAMELEN must match exactly across all systems you plan to run 
 * a single application on.
 *
 * This is _not_ the correct long-term solution to this problem.  The correct 
 * solution is to use MAXHOSTNAMELEN on each system, even if they are different
 * on different systems, and change the code and protocols to add message sizes
 * to all inter-system messaging and malloc (rather than statically allocate) 
 * buffs for remote machine names.  This, of course, represents a significant
 * amount of work (much more than simply defining G2_MAXHOSTNAMELEN) and will 
 * make MPICH-G2 non-backward compatable because of the change in protocols.  
 * Defining G2_MAXHOSTNAMELEN here to 256 is not only much easier, but will 
 * result in backward compatability with most systems (at least the non-Linux 
 * based systems). The Linux installations will have to upgrade _if_ they want
 * to work with with {G2_}MAXHOSTNAMELEN=256 systems.
 *
 */

#define G2_MAXHOSTNAMELEN 256

/* COMMWORLDCHANNELSNAMELEN must accomodate hostname+pid */
#define COMMWORLDCHANNELSNAMELEN (G2_MAXHOSTNAMELEN+20)

#endif
