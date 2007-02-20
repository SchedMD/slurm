
/*****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************/

Proctrack is an AIX 5.2 Kernel Extension which keeps track of processes
created in association with a registered 'job'.

Created for use in the Open Source project SLURM (see above notice).


How to make/install:
  - 'make' creates two primary binary files (and a number of tests):
	- proctrackext - is the loadable kernel extension
	- proctrack - is the binary which loads the proctrackext module

The [un]install process is engaged by using the proctrack program as
follows: (You must be <em>root</em> to [un]install)
	proctrack start -f ./proctrackext -n 2048
	proctrack stop -f ./proctrackext

Looking to see the Kernel Extension
 	The command 'genkex' will display a list of kernel extensions.
	you should see proctrackext in the list after 'start'ing it.

Starting proctrack:
	- If you do not have a leading directory in the filename, the kernel
       extension (which is used primarily for device drivers) is assumed
       to be in a directory something like "/dev/0/...".   Best to use
       a "/path/to/file" or "./file" for the location of the module
	- the "-n" argument is to tell proctrack how many processes to expect
       to track.  Currently this defaults to 2048 if not otherwise set.
       Some of the data structures are walked through linearly (like to
       kill a job), so a larger number may affect the responsiveness of the
       Operating System.  It was presumed that the extension would be used
       in the spirit of the "Livermore model", which is one (or a small
       number) of processes per logical cpu, and that the number of cpu's
       per SMP would be in the small 2-3 digits (64, 128, etc).

Stopping proctrack:
	- AIX kernel extensions are removed by their magic number.  To find
       the magic number, (command line genkex), the extension binary can
       be used.   Thus, you need to pass the 'stop' command the extension
       binary.


Debugging information:
	- There is a _LDEBUG define (set to 0 by default) at the top of the
       extension (proctrackext.c).  Setting this to non-zero, recompiling
       and re-installing the extension will result in a trace file named
       'proctrackext.log' <em>in the directory in which you start
       proctrack</em>.


External Usage:
	- One loaded, the extension provides the new system calls listed
	in proctrack.h.
