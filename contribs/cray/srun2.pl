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
	$acctg_freq,
	$begin_time,
	$chdir,
	$check_time,
	$check_dir,
	$comment,
	$constraint,
	$contiguous,
	$cores_per_socket,
	$cpu_bind,
	$cpus_per_task,
	$dependency,
	$distribution,
	$error_file,
	$epilog,
	$exclusive,
	$extra_node_info,
	$group,
	$gres,
	$help,
	$hint,
	$hold,
	$immediate,
	$input_file,
	$job_id,
	$job_name,
	$kill_on_bad_exit,
	$label,
	$licenses,
	$mail_type,
	$mail_user,
	$man,
#RESUME AT --mem in srun man page
	$preserve_env,
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
	$acctg_freq = $ENV{$_}		if $_ eq "SLURM_ACCTG_FREQ";
	$chdir = $ENV{$_}		if $_ eq "SLURM_WORKING_DIR";
	$check_time = $ENV{$_}		if $_ eq "SLURM_CHECKPOINT";
	$check_dir = $ENV{$_}		if $_ eq "SLURM_CHECKPOINT_DIR";
	$cpu_bind = $ENV{$_}		if $_ eq "SLURM_CPU_BIND";
	$cpus_per_task = $ENV{$_}	if $_ eq "SLURM_CPUS_PER_TASK";
	$dependency = $ENV{$_}		if $_ eq "SLURM_DEPENDENCY";
	$distribution = $ENV{$_}	if $_ eq "SLURM_DISTRIBUTION";
	$epilog = $ENV{$_}		if $_ eq "SLURM_EPILOG";
	$error_file = $ENV{$_}		if $_ eq "SLURM_STDERRMODE";
	$exclusive  = 1			if $_ eq "SLURM_EXCLUSIVE";
	$input_file = $ENV{$_}		if $_ eq "SLURM_STDINMODE";
	$job_name = $ENV{$_}		if $_ eq "SLURM_JOB_NAME";
	$label = 1			if $_ eq "SLURM_LABELIO";
	$num_tasks = $ENV{$_}		if $_ eq "SLURM_NTASKS";
	$num_nodes = $ENV{$_}		if $_ eq "SLURM_NNODES";
	$time_limit = $ENV{$_}		if $_ eq "SLURM_TIMELIMIT";
}

GetOptions(
	'A|account=s'			=> \$account,
	'acctg-freq=i'			=> \$acctg_freq,
	'B|extra-node-info=s'		=> \$extra_node_info,
	'begin=s'			=> \$begin_time,
	'checkpoint=s'			=> \$check_time,
	'checkpoint-dir=s'		=> \$check_dir,
	'comment=s'			=> \$comment,
	'C|constraint=s'		=> \$constraint,
	'contiguous'			=> \$contiguous,
	'cores-per-socket=i'		=> \$cores_per_socket,
	'cpu_bind=s'			=> \$cpu_bind,
	'c|cpus-per-task=i'		=> \$cpus_per_task,
	'd|dependency=s'		=> \$dependency,
	'D|chdir=s'			=> \$chdir,
	'e|error=s'			=> \$error_file,
	'epilog=s'			=> \$epilog,
	'exclusive'			=> \$exclusive,
	'E|preserve-env'		=> \$preserve_env,
	'gid=s'				=> \$group,
	'gres=s'			=> \$gres,
	'help|?'			=> \$help,
	'hint=s'			=> \$hint,
	'H|hold'			=> \$hold,
	'I|immediate'			=> \$immediate,
	'i|input=s'			=> \$input_file,
	'jobid=i'			=> \$job_id,
	'J|job-name=s'			=> \$job_name,
	'K|kill-on-bad-exit'		=> \$kill_on_bad_exit,
	'l|label'			=> \$label,
	'L|licenses=s'			=> \$licenses,
	'm|distribution=s'		=> \$distribution,
	'mail-type=s'			=> \$mail_type,
	'mail-user=s'			=> \$mail_user,
	'man'				=> \$man,
	'n|ntasks=s'			=> \$num_tasks,
	'N|nodes=s'			=> \$num_nodes,
	't|time=s'			=> \$time_limit,
) or pod2usage(2);

# Display usage if necessary
pod2usage(0) if $man;
if ($help) {
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

if ($have_job == 0) {
	$command = "$salloc";
	$command .= " --account=$account"		if $account;
	$command .= " --acctg-freq=$acctg_freq"		if $acctg_freq;
	$command .= " --begin=$begin_time"		if $begin_time;
	$command .= " --chdir=$chdir"			if $chdir;
	$command .= " --comment=\"$comment\""		if $comment;
	$command .= " --constraint=\"$constraint\""	if $constraint;
	$command .= " --contiguous"			if $contiguous;
	$command .= " --cores-per-socket=$cores_per_socket" if $cores_per_socket;
	$command .= " --cpu_bind=$cpu_bind"		if $cpu_bind;
	$command .= " --cpus-per-task=$cpus_per_task"	if $cpus_per_task;
	$command .= " --dependency=$dependency"		if $dependency;
	$command .= " --distribution=$distribution"	if $distribution;
	$command .= " --epilog=$epilog"			if $epilog;
	$command .= " --error=$error_file"		if $error_file;
	$command .= " --exclusive"			if $exclusive;
	$command .= " --extra-node-info=$extra_node_info" if $extra_node_info;
	$command .= " --gid=$group"			if $group;
	$command .= " --gres=$gres"			if $gres;
	$command .= " --hint=$hint"			if $hint;
	$command .= " --hold"				if $hold;
	$command .= " --immediate"			if $immediate;
	$command .= " --input=$input_file"		if $input_file;
	$command .= " --jobid=$job_id"			if $job_id;
	$command .= " --job-name=$job_name"		if $job_name;
	$command .= " --kill-on-bad-exit"		if $kill_on_bad_exit;
	$command .= " --label"				if $label;
	$command .= " --licenses=$licenses"		if $licenses;
	$command .= " --mail-type=$mail_type"		if $mail_type;
	$command .= " --mail-user=$mail_user"		if $mail_user;
	$command .= " --ntasks=$num_tasks"		if $num_tasks;
	$command .= " --nodes=$num_nodes"		if $num_nodes;
	$command .= " --preserve_env"			if $preserve_env;
	$command .= " --time=$time_limit"		if $time_limit;
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
first create a resource allocation in whit|timech to run the parallel j	$command .= " --cpus-per-task=$cpus_per_task"	if $cpus_per_task;ob.

=head1 OPTIONS

=over 4

=item B<-A> | B<--account=account>

Charge resources used by this job to specified account.

=item B<-n> | B<--ntasks=num_tasks>

Number of tasks to launch.

=item B<--acctg-freq=seconds>

Specify the accounting sampling interval.

=item B<-B> | B<--extra-node-info=sockets[:cores[:threads]]>

Request a specific allocation of resources with details as to the
number and type of computational resources within a cluster:
number of sockets (or physical processors) per node,
cores per socket, and threads per core.
The individual levels can also be specified in separate options if desired:
B<--sockets-per-node=sockets>, B<--cores-per-socket=cores>, and
B<--threads-per-core=threads>.

=item B<--begin=time>

Defer job initiation until the specified time.cores_per_socket

=item B<--checkpoint=interval>

Specify the time interval between checkpoint creations.

=item B<--checkpoint-dir=directory>

Directory where the checkpoint image should be written.

=item B<--comment=string>

An arbitrary comment.

=item B<-C> | B<--constraint=string>

Constrain job allocation to nodes with the specified features.

=item B<--contiguous>

Constrain job allocation to contiguous nodes.

=item B<--cores-per-socket=number>

Count of cores to be allocated per per socket.

=item B<--cpu_bind=options>

Strategy to be used for binding tasks to the CPUs.
Options include: quiet, verbose, none, rank, map_cpu, mask_cpu, rank_ldom,
map_ldom, mask_ldom, sockets, cores, threads, ldoms and help.

=item B<-c> | B<--cpus-per-task=number>

Count of CPUs required per task.

=item B<-d> | B<--dependency=[condition:]jobid>

Wait for job(s) to enter specified condition before starting the job.
Valid conditions include after, afterany, afternotok, and singleton.

=item B<-D> | B<--chdir=directory>

Execute the program from the specified directory.

=item B<-e> | B<--error=filename>

Write stderr to the specified file.

=item B<--epilog=filename>

Execute the specified program after the job step completes.

=item B<--exclusive>

The job or job step will not share resources with other jobs or job steps.

=item B<-E> | B<--preserve-env>

Pass the current values of environment variables SLURM_NNODES and
SLURM_NTASKS through to the executable, rather than computing them
from command line parameters.

=item B<--gid=group>

If user root, then execute the job using the specified group access permissions.
Specify either a group name or ID.

=item B<--gres=gres_name[*count]>

Allocate the specified generic resources on each allocated node.

=item B<-?> | B<--help>

Print brief help message.

=item B<--hint=type>

Bind tasks according to application hints: compute_bound, memory_bound,
multithread, nomultithread, or help.

=item B<-H> | B<--hold>

Submit the job in a held state.

=item B<-I> | B<--immediate>

Exit if resources are not available immediately.

=item B<-i> | B<--input=filename>

Read stdin from the specified file.

=item B<--jobid=number>

Specify the job ID number. Usable only by SlurmUser or user root.

=item B<-J> | B<--job-name=name>

Specify a name for the job.

=item B<-K> | B<--kill-on-bad-exit>

Immediately terminate a job if any task exits with a non-zero exit code.

=item B<-l> | B<--label>

Prepend task number to lines of stdout/err.

=item B<-l> | B<--licenses=names>

Specification of licenses (or other resources available on all
nodes of the cluster) which must be allocated to this job.

=item B<-m> | B<--distribution=layout>

Specification of distribution of tasks across nodes.
Supported layouts include: block, cyclic, arbitrary, plane=size.
The options block and cyclic may be followed by :block or :cyclic
to control the distribution of tasks across sockets on the node.

=item B<--man>

Print full documentation.

=item B<--mail-type=event>

Send email when certain event types occur.
Valid events values are BEGIN, END, FAIL, REQUEUE, and ALL (any state change).

=item B<--mail-user=user>

Send email to the specified user(s). The default is the submitting user.

=item B<-t> | B<--time>

Time limit.

=item B<-n> | B<--ntasks=num_tasks>

Number of tasks to launch.

=item B<-N> | B<--nodes=num_nodes>

Number of nodes to use.

=item B<-t> | B<--time>

Time limit in minutes or hours:minutes:seconds.

=back

=cut
