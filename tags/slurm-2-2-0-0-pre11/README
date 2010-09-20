This is SLURM, the Simple Linux Utility for Resource Management. SLURM
is an open-source cluster resource management and job scheduling system
that strives to be simple, scalable, portable, fault-tolerant, and
interconnect agnostic. SLURM currently has been tested only under Linux.

As a cluster resource manager, SLURM provides three key functions. First,
it allocates exclusive and/or non-exclusive access to resources
(compute nodes) to users for some duration of time so they can perform
work. Second, it provides a framework for starting, executing, and
monitoring work (normally a parallel job) on the set of allocated
nodes. Finally, it arbitrates conflicting requests for resources by
managing a queue of pending work.

SLURM is provided "as is" and with no warranty. This software is
distributed under the GNU General Public License, please see the files
COPYING, DISCLAIMER, and LICENSE.OpenSSL for details.

This README presents an introduction to compiling, installing, and
using SLURM.


SOURCE DISTRIBUTION HIERARCHY
-----------------------------

The top-level distribution directory contains this README as well as
other high-level documentation files, and the scripts used to configure
and build SLURM (see INSTALL). Subdirectories contain the source-code
for SLURM as well as a DejaGNU test suite and further documentation. A
quick description of the subdirectories of the SLURM distribution follows:

  src/        [ SLURM source ]
     SLURM source code is further organized into self explanatory 
     subdirectories such as src/api, src/slurmctld, etc.

  doc/        [ SLURM documentation ]
     The documentation directory contains some latex, html, and ascii
     text papers, READMEs, and guides. Manual pages for the SLURM
     commands and configuration files are also under the doc/ directory.

  etc/        [ SLURM configuration ] 
     The etc/ directory contains a sample config file, as well as
     some scripts useful for running SLURM.

  slurm/      [ SLURM include files ]
     This directory contains installed include files, such as slurm.h
     and slurm_errno.h, needed for compiling against the SLURM API.

  testsuite/  [ SLURM test suite ]
     The testsuite directory contains the framework for a set of 
     DejaGNU and "make check" type tests for SLURM components.
     There is also an extensive collection of Expect scripts.

  auxdir/     [ autotools directory ]
     Directory for autotools scripts and files used to configure and
     build SLURM
  
  contribs/   [ helpful tools outside of SLURM proper ]
     Directory for anything that is outside of slurm proper such as a
     different api or such.  To have this build you need to do a 
     make contrib/install-contrib.

COMPILING AND INSTALLING THE DISTRIBUTION
-----------------------------------------

Please see the instructions at 
  http://www.llnl.gov/linux/slurm/quickstart_admin.html
Extensive documentation is available from our home page at 
  http://www.llnl.gov/linux/slurm

PROBLEMS
--------

If you experience problems compiling, installing, or running SLURM
please send e-mail to either slurm-dev@lists.llnl.gov.

$Id$
