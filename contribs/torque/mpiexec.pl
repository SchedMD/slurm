#! /usr/bin/perl -w
###############################################################################
#
# mpiexec - wrapper script for mpiexec to run srun instead.
#
#
###############################################################################
#  Copyright (C) 2007 The Regents of the University of California.
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Danny Auble <auble1@llnl.gov>.
#  CODE-OCEC-09-009. All rights reserved.
#
#  This file is part of Slurm, a resource management program.
#  For details, see <https://slurm.schedmd.com/>.
#  Please also read the included file: DISCLAIMER.
#
#  Slurm is free software; you can redistribute it and/or modify it under
#  the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 2 of the License, or (at your option)
#  any later version.
#
#  In addition, as a special exception, the copyright holders give permission
#  to link the code of portions of this program with the OpenSSL library under
#  certain conditions as described in each individual source file, and
#  distribute linked combinations including the two. You must obey the GNU
#  General Public License in all respects for all of the code used other than
#  OpenSSL. If you modify file(s) with this exception, you may extend this
#  exception to your version of the file(s), but you are not obligated to do
#  so. If you do not wish to do so, delete this exception statement from your
#  version.  If you delete this exception statement from all source files in
#  the program, then also delete it here.
#
#  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#
#  You should have received a copy of the GNU General Public License along
#  with Slurm; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
#
###############################################################################

use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case require_order);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Switch;

my $srun = "${FindBin::Bin}/srun";

my ($nprocs, $hostname, $verbose, $nostdin, $allstdin, $nostdout, $pernode,
    $perif, $no_shem, $gige, $kill_it, $tv, $config_file, $timeout,
    $help, $man);

sub get_new_config() {

	my @file_parts = split(/\//, $config_file);
	my $new_config = "/tmp/$file_parts[$#file_parts].slurm";
	my $task_cnt = 0;
	my $end_cnt = 0;

	open OLD_FILE, "$config_file" or
		die "$config_file doesn't exsist!";
	open FILE, ">$new_config" or
		die "Can't open $new_config";

	foreach my $line (<OLD_FILE>) {
		my @parts = split(/\:/, $line);
		if(!$parts[0] || !$parts[1]
		   || ($parts[0] eq "")
		   || ($parts[1] eq "")
		   || ($parts[0] =~ '#')) {
			next;
		} elsif ($parts[0] =~ '\-n *(\d+)') {
			$end_cnt = $task_cnt+$1-1;
			print FILE "$task_cnt-$end_cnt\t$parts[1]";
			$task_cnt = $end_cnt+1;
		} else {
			print "We don't have support for hostname task layout in a config file right now.\nPlease use srun with the -m arbitrary mode to layout tasks on specific nodes.\n";
		}
	}

	close FILE;
	close OLD_FILE;

	return ($new_config, $task_cnt);
}


GetOptions('n=i'      => \$nprocs,
	   'host=s'   => \$hostname,
	   'verbose+'  => \$verbose,
	   'nostdin'  => \$nostdin,
	   'allstdin' => \$allstdin,
	   'nostdout' => \$nostdout,
	   'pernode'  => \$pernode,
	   'perif'    => \$perif, # n/a
	   'no-shmem' => \$no_shem, # n/a
	   'gige'     => \$gige, # n/a
	   'kill'     => \$kill_it, # n/a
	   'tv|totalview' => \$tv, # n/a
	   'config=s' => \$config_file,
	   'help|?'   => \$help,
	   'man'      => \$man
	   ) or pod2usage(2);

# Display usage if necessary
pod2usage(0) if $help;
if ($man) {
	if ($< == 0) {   # Cannot invoke perldoc as root
		my $id = eval { getpwnam("nobody") };
		$id = eval { getpwnam("nouser") } unless defined $id;
		$id = -2                          unless defined $id;
		$<  = $id;
        }
        $> = $<;                         # Disengage setuid
        $ENV{PATH} = "/bin:/usr/bin";    # Untaint PATH
        delete @ENV{'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};
        if ($0 =~ /^([-\/\w\.]+)$/) { $0 = $1; }    # Untaint $0
        else { die "Illegal characters were found in \$0 ($0)\n"; }
        pod2usage(-exitstatus => 0, -verbose => 2);
}

# Use sole remaining argument as jobIds
my $script;
if ($ARGV[0]) {
	foreach (@ARGV) {
	        $script .= "$_ ";
	}
} elsif(!$config_file) {
        pod2usage(2);
}

my $new_config;


my $command = "$srun";

# write stdout and err to files instead of stdout
$command .= " -o job.o\%j -e job.e\%j" if $nostdout;
$command .= " -inone" if $nostdin;
$command .= " -i0" if !$allstdin; #default only send stdin to first node
$command .= " -n$nprocs" if $nprocs; # number of tasks
$command .= " -w$hostname" if $hostname; # Hostlist provided
$command .= " -t '" . $ENV{"MPIEXEC_TIMEOUT"} . "'" if $ENV{"MPIEXEC_TIMEOUT"};

if($verbose) {
	$command .= " -"; # verbose
	for(my $i=0; $i<$verbose; $i++) {
		$command .= "v";
	}
}

if($config_file) {
	($new_config, my $new_nprocs) = get_new_config();
	$command .= " -n$new_nprocs" if !$nprocs;
	$command .= " --multi-prog $new_config";
} else {
	$command .= " $script";
}

#print "$command\n";
system($command);

system("rm -f $new_config") if($new_config);

__END__

=head1 NAME

B<mpiexec.slurm> - Run an MPI program under Slurm

=head1 SYNOPSIS

mpiexec.slurm args executable pgmargs

where args are comannd line arguments for mpiexec (see below), executable is
the name of the eecutable and pgmargs are command line arguments for the
executable. For example the following command will run the MPI progam a.out on
4 processes:

	mpiexec.slurm -n 4 a.out

mpiexec.slurm supports the following options:

         [-n nprocs]
         [-host hostname]
         [-verbose]
         [-nostdin]
         [-allstdin]
         [-nostdout]
         [-pernode]
         [-config config_file]
         [-help|-?]
         [-man]

=head1 DESCRIPTION

The B<mpiexec.slurm> 

=head1 OPTIONS

=over 4

=item B<-n <np>>

Specify the number of processes to use

=item B<-host hostname>

Name of host on which to run processes

=item B<-verbose>

Increase the verbosity of mpiexec.slurm informational messages. Multiple
-verbose's  will further increase mpiexec.slurm's verbosity. By default only
errors will be displayed.

=item B<-nostdin>

Do not connect the standard input stream of process 0 to the mpiexec process.
If the process attempts to read from stdin, it will see an end-of-file.

=item B<-allstdin>

Send the standard input stream of mpiexec.slurm to all processes. Each
character typed to mpiexec (or read from a file) is duplicated numproc times,
and sent to each process. This permits every process to read, for example,
configuration information from the input stream. 

=item B<-nostdout>

Do not connect the standard output and error streams of each process back to
the mpiexec.slurm process. Standard output and error will be respectively
writte in files of the form job.ojobid and job.ejobid for batch jobs, and
directly to the controlling terminal for interactive jobs.

=item B<-pernode>

Allocate only one process per compute node. For SMP nodes, only one processor
will be allocated a job. This flag is used to implement multiple level
parallelism with MPI between nodes, and threads within a node, assmuming the
code is set up to do that.

=item B<-config <config_file>>

Process executable and arguments are specified in the given configuration file.
This flag permits the use of heterogeneous jobs using multiple executables. No
executable is given on the command line when using the -config flag. If
config_file is "-", then the configuration is read from standard input. In this
case the flag -nostdin is mandatory, as it is not possible to separate the
contents of the configuration file from process input. The config_file can
contain lines beginning with "#", that are considered comments and ignored and
and one or more lines with the following format:

	-n XX : executable [args]

where XX is the number of processes to be used, executable is the name of the
program to run and args are its arguments. For example:

	# Sample mpiexec config file
	# Launch two instance of foo
	-n 2 : foo
	# and three instances of bar
	-n 3 bar

There is no support for hostname task layout in a config file at the moment.

=item B<-help|-?>

Display a brief help page

=back

=cut
