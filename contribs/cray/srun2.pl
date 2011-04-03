#! /usr/bin/perl -w
###############################################################################
#
# srun - Wrapper for Cray's "aprun" command. If not executed within a job
#	 allocation, then also use "salloc" to create the allocation before
#	 executing "aprun".
#
###############################################################################
#  Copyright (C) 2011 SchedMD LLC
#  Written by Morris Jette <jette1@schedmd.gov>.
#  CODE-OCEC-09-009. All rights reserved.
#
#  This file is part of SLURM, a resource management program.
#  For details, see <https://computing.llnl.gov/linux/slurm/>.
#  Please also read the included file: DISCLAIMER.
#
#  SLURM is free software; you can redistribute it and/or modify it under
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
#  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#
#  You should have received a copy of the GNU General Public License along
#  with SLURM; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
#
###############################################################################

use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case require_order);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Switch;

my (	$account,
	$help,
	$man,
	$num_tasks,
	$num_nodes,
	$time_limit,
	$time_secs
);

my $aprun  = "${FindBin::Bin}/apbin";
my $salloc = "${FindBin::Bin}/salloc";
my $echo   = "${FindBin::Bin}/echo";

my $have_job;
$have_job = 0;

foreach (keys %ENV) {
#	print "$_=$ENV{$_}\n";
	$have_job = 1			if $_ eq "SLURM_JOBID";
	$account = $ENV{$_}		if $_ eq "SLURM_ACCOUNT";
	$num_tasks = $ENV{$_}		if $_ eq "SLURM_NTASKS";
	$num_nodes = $ENV{$_}		if $_ eq "SLURM_NNODES";
	$time_limit = $ENV{$_}		if $_ eq "SLURM_TIMELIMIT";
}

GetOptions(
	'A|account=s'			=> \$account,
	'help|?'			=> \$help,
	'man'				=> \$man,
	'n|ntasks=s'			=> \$num_tasks,
	'N|nodes=s'			=> \$num_nodes,
	't|time=s'			=> \$time_limit,
) or pod2usage(2);

# Display usage if necessary
pod2usage(0) if $help;
if ($man) {
	if ($< == 0) {   # Cannot invoke perldoc as root
		my $id = eval { getpwnam("nobody") };
		$id = eval { getpwnam("nouser") } unless defined $id;
		$id = -2			  unless defined $id;
		$<  = $id;
	}
	$> = $<;			# Disengage setuid
	$ENV{PATH} = "/bin:/usr/bin";	# Untaint PATH
	delete @ENV{'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};
	if ($0 =~ /^([-\/\w\.]+)$/) { $0 = $1; }    # Untaint $0
	else { die "Illegal characters were found in \$0 ($0)\n"; }
	#pod2usage(-exitstatus => 0, -verbose => 2);
}

my $script;
if ($ARGV[0]) {
	foreach (@ARGV) {
		$script .= "$_ ";
	}
} else {
	pod2usage(2);
}
my %res_opts;
my %node_opts;

my $command;

if ($have_job) {
	$command = "$salloc";
	$command .= " --account=$account" if $account;
	$command .= " --ntasks=$num_tasks" if $num_tasks;
	$command .= " --nodes=$num_nodes" if $num_nodes;
	$command .= " --time=$time_limit" if $time_limit;
	$command .= " $aprun";
} else {
	$command = "$aprun";
	$command .= " -n $num_tasks" if $num_tasks;
	$time_secs = get_seconds($time_limit);
	$command .= " -t $time_secs" if $time_limit;
}

$command .= " $script";

print "command=$command\n";
#system($command);

sub get_seconds {
	my ($duration) = @_;
	$duration = 0 unless $duration;
	my $seconds = 0;

	# Convert [[HH:]MM:]SS to duration in seconds
	if ($duration =~ /^(?:(\d+):)?(\d*):(\d+)$/) {
		my ($hh, $mm, $ss) = ($1 || 0, $2 || 0, $3);
		$seconds += $ss;
		$seconds += $mm * 60;
		$seconds += $hh * 60;
	} elsif ($duration =~ /^(\d+)$/) {  # Convert number in minutes to seconds
		$seconds = $duration * 60;
	} else { # Unsupported format
		die("Invalid time limit specified ($duration)\n");
	}

	return $seconds;
}

##############################################################################

__END__

=head1 NAME

B<srun> - Run a parallel job

=head1 SYNOPSIS

srun  [OPTIONS...] executable [arguments...]

=head1 DESCRIPTION

Run a parallel job on cluster managed by SLURM.  If necessary, srun will
first create a resource allocation in whit|timech to run the parallel job.

=head1 OPTIONS

=over 4

=item B<-A> | B<--account=account>

Charge resources used by this job to specified account.

=item B<-n> | B<--ntasks=num_tasks>

Number of tasks to launch.

=item B<-N> | B<--nodes=num_nodes>

Number of nodes to use.

=item B<-?> | B<--help>

Brief help message

=item B<--man>

Full documentation

=item B<-t> | B<--time>

Time limit.

=back

=cut

