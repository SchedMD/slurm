#! /usr/bin/perl -w
###############################################################################
#
# qstat - queries slurm jobs in familar pbs format.
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
#  Based off code with permission copyright 2006, 2007 Cluster Resources, Inc.
###############################################################################


use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Slurmdb ':all'; # needed for getting the correct cluster dims

# Parse Command Line Arguments
my (
    $all,              $diskReservation, $executable,   $full,
    $giga,             $help,            $idle,         $mega,
    $man,              $nodes,           $one,          $queueList,
    $queueStatus,      $running,         $serverStatus, $siteSpecific,
    $userList,         $hostname,        $rc,           $header_printed
);

GetOptions(
	'a'      => \$all,
	'B=s'    => \$serverStatus,
	'e'      => \$executable,
	'f'      => \$full,
	'help|?' => \$help,
	'i'      => \$idle,
	'G'      => \$giga,
	'man'    => \$man,
	'M'      => \$mega,
	'n'      => \$nodes,
	'1'      => \$one,
	'r'      => \$running,
	'R'      => \$diskReservation,
	'q'      => \$queueStatus,
	'Q:s'    => \$queueList,
	'u=s'    => \$userList,
	'W=s'    => \$siteSpecific,
	) or pod2usage(2);

$rc = 153; # if we don't find what we are looking for return this.

# Display usage if necessary
pod2usage(0) if $help;
if ($man)
{
	if ($< == 0)    # Cannot invoke perldoc as root
	{
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

if($^O eq 'aix') {
	$hostname = `hostname`;
} else {
	$hostname = `hostname -f`;
}
chomp $hostname;

# Handle unsupported arguments
#unsupportedOption("-e", DIE) if $executable;
#unsupportedOption("-W", DIE) if $siteSpecific;
#unsupportedOption("-R", DIE) if $diskReservation;
#unsupportedOption("-q", DIE) if $queueStatus;
#unsupportedOption("-Q <queue_destination>", DIE) if $queueDestination;
#unsupportedOption("-B", DIE) if $serverStatus;

# Build command

# foreach my $job (@{$resp->{job_array}}) {
# 	while(my ($key, $value) = each(%$job)) {
# 		print "$key => $value\n";
# 	}
# }
my $now_time = time();
my $job_flags = SHOW_ALL | SHOW_DETAIL;

if(defined($queueList)) {
	my @queueIds = split(/,/, $queueList) if $queueList;

	my $resp = Slurm->load_partitions(0, SHOW_ALL);
	if(!$resp) {
		die "Problem loading jobs.\n";
	}

	my $line = 0;
	foreach my $part (@{$resp->{partition_array}}) {
		if (@queueIds) {
			next unless grep /^$part->{'name'}$/, @queueIds;
		}

		if ($full) { # Full
			print_part_full($part);
		} else { # Brief
			print_part_brief($part, $line);
			$line++;
		}
		$rc = 0;
	}
} elsif($queueStatus) {
	my $jresp = Slurm->load_jobs(0, $job_flags);
	die "Problem loading jobs.\n" if(!$jresp);
	my $resp = Slurm->load_partitions(0, SHOW_ALL);
	die "Problem loading partitions.\n" if(!$resp);
	my $total_running = 0;
	my $total_queued = 0;
	my $line = 0;
	foreach my $part (@{$resp->{partition_array}}) {
		$part->{'running_jobs'} = 0;
		$part->{'queued_jobs'} = 0;
		foreach my $job (@{$jresp->{job_array}}) {
			next if($job->{'partition'} ne $part->{'name'});
			$part->{'running_jobs'}++
				if($job->{'job_state'} == JOB_RUNNING);
			$part->{'queued_jobs'}++
				if($job->{'job_state'} == JOB_PENDING);
		}
		$total_running += $part->{'running_jobs'};
		$total_queued += $part->{'queued_jobs'};
		print_part_limits($part, $line);
		$line++;
        }
	printf("                                               ----- -----\n");
	printf("                                               %5d %5d\n",
	       $total_running, $total_queued);
} else {
	my @jobIds = @ARGV;
	my @userIds = split(/,/, $userList) if $userList;

	my $resp = Slurm->load_jobs(0, $job_flags);
	if(!$resp) {
		die "Problem loading jobs.\n";
	}

	my $line = 0;
	foreach my $job (@{$resp->{job_array}}) {
		my $use_time      = $now_time;
		my $state = $job->{'job_state'};
		if($job->{'end_time'} && $job->{'end_time'} < $now_time) {
			$use_time = $job->{'end_time'};
		}
		$job->{'statPSUtl'} = job_time_used($job);
		$job->{'aWDuration'} = $job->{'statPSUtl'};

		$job->{'allocNodeList'} = $job->{'nodes'} || "--";
		$job->{'stateCode'} = stateCode($job->{'job_state'});
		$job->{'user_name'} = getpwuid($job->{'user_id'}) || "nobody";
		$job->{'name'} = "Allocation" if !$job->{'name'};

		# Filter jobs according to options and arguments
		if (@jobIds) {
			next unless grep /^$job->{'job_id'}$/, @jobIds;
		} else {
			if ($running) {
				next unless ($job->{'stateCode'} eq 'R'
					     || $job->{'stateCode'} eq 'S');
			}
			if ($idle) {
				next unless ($job->{'stateCode'} eq 'Q');
			}
			if (@userIds) {
				next unless
					grep /^$job->{'user_name'}$/, @userIds;
			}
		}

		if ($all || $idle || $nodes || $running
		    || $giga || $mega || $userList) { # Normal
			print_job_select($job);
		} elsif ($full) { # Full
			print_job_full($job);
		} else { # Brief
			print_job_brief($job, $line);
			$line++;
		}
		$rc = 0;
	}

	# return 0 even if no records printed when using -u flag
	if (@userIds) {
		$rc = 0;
	}
}

# Exit with status code
exit $rc;

################################################################################
# $stateCode = stateCode($state)
# Converts a slurm state into a one-character pbs state code
################################################################################
sub stateCode
{
	my ($state) = @_;

	if(!defined($state)) {
		return 'U';
	}
	my $base_state = $state & JOB_STATE_BASE;
	if ($base_state == JOB_COMPLETE
		|| $base_state == JOB_CANCELLED
		|| $base_state == JOB_TIMEOUT
		|| $base_state == JOB_NODE_FAIL
		|| $base_state == JOB_PREEMPTED
		|| $base_state == JOB_BOOT_FAIL
		|| $base_state == JOB_FAILED)    { return 'C' }
	elsif ($base_state == JOB_RUNNING)   { return 'R' }
	elsif ($base_state == JOB_PENDING)   { return 'Q' }
	elsif ($base_state == JOB_SUSPENDED) { return 'S' }
	else                                 { return 'U' }   # Unknown
	return 'U';
}


################################################################################
# $hhmm = hhmm($hhmmss)
# Converts a duration in hhmmss to hhmm
################################################################################
sub hhmm
{
	my ($hhmmss) = @_;

	if ($hhmmss == INFINITE) {
		return "Infinite";
	}

	# Convert hhmmss to duration in minutes
	$hhmmss = 0 unless $hhmmss;
	$hhmmss =~ /(?:(\d+):)?(?:(\d+):)?([\d\.]+)/;
	my ($hh, $mm, $ss) = ($1 || 0, $2 || 0, $3 || 0);

	my $duration = int($ss / 60);
	$duration += $mm * 1;
	$duration += $hh * 60;

	# Convert duration in minutes to hhmm
	my $hours = int($duration / 60);
	$duration = $duration - $hours * 60;
	my $minutes = $duration;
	my $hhmm = sprintf('%02d:%02d', $hours, $minutes);
	return $hhmm;
}

################################################################################
# $hhmmss = hhmmss($hhmmss)
# Converts a duration in hhmmss to hhmmss
################################################################################
sub hhmmss
{
	my ($hhmmss) = @_;
	if ($hhmmss == INFINITE) {
		return "Infinite";
	}
	# Convert hhmmss to duration in seconds
	$hhmmss = 0 unless $hhmmss;
	$hhmmss =~ /(?:(\d+):)?(?:(\d+):)?([\d\.]+)/;
	my ($hh, $mm, $ss) = ($1 || 0, $2 || 0, $3 || 0);

	my $duration = int($ss);
	$duration += $mm * 60;
	$duration += $hh * 3600;

	# Convert duration in seconds to hhmmss
	my $hours = int($duration / 3600);
	$duration = $duration - $hours * 3600;
	my $minutes = int($duration / 60);
	$duration = $duration - $minutes * 60;
	my $seconds = $duration;
	my $hhmmss2 = sprintf('%02d:%02d:%02d', $hours, $minutes, $seconds);
	return $hhmmss2;
}

################################################################################
# $ddhhmm = ddhhmm($hhmmss)
# Converts a duration in hhmmss to ddhhmm
################################################################################
sub ddhhmm
{
	my ($hhmmss) = @_;

	if ($hhmmss && $hhmmss == INFINITE) {
		return "Infinite";
	}
	# Convert hhmmss to duration in minutes
	$hhmmss = 0 unless $hhmmss;
	$hhmmss =~ /(?:(\d+):)?(?:(\d+):)?([\d\.]+)/;
	my ($hh, $mm, $ss) = ($1 || 0, $2 || 0, $3 || 0);

	my $duration = int($ss / 60);
	$duration += $mm * 1;
	$duration += $hh * 60;

	# Convert duration in minutes to ddhhmm
	my $days = int($duration / 1440);
	$duration = $duration - $days * 1440;
	my $hours = int($duration / 60);
	$duration = $duration - $hours * 60;
	my $minutes = $duration;
	my $ddhhmm = sprintf('%02d:%02d:%02d', $days, $hours, $minutes);
	return $ddhhmm;
}

################################################################################
# $humanReadableTime = hRTime($epochTime)
# Converts an epoch time into a human readable time
################################################################################
sub hRTime
{
	my ($epochTime) = @_;

	if ($epochTime == INFINITE) {
		return "Infinite";
	}

	return scalar localtime $epochTime;
}

sub job_time_used
{
	my ($job) = @_;

	my $end_time;

	return 0 if ($job->{'start_time'} == 0)
		|| ($job->{'job_state'} == JOB_PENDING);

	return $job->{'pre_sus_time'} if $job->{'job_state'} == JOB_SUSPENDED;

	if (($job->{'job_state'} == JOB_RUNNING)
	    ||  ($job->{'end_time'} == 0)) {
		$end_time = time;
	} else {
		$end_time = $job->{'end_time'};
	}

	return ($end_time - $job->{'suspend_time'} + $job->{'pre_sus_time'})
		if $job->{'suspend_time'};
	return ($end_time - $job->{'start_time'});
}

sub yes_no
{
	my ($query) = @_;
	return "yes" if $query;
	return "no";
}

sub en_dis
{
	my ($query) = @_;
	return "E" if $query;
	return "D";
}

sub running_stopped
{
	my ($query) = @_;
	return "R" if $query;
	return "S";
}

sub get_exec_host
{
	my ($job) = @_;

	my $execHost = "--";
	if ($job->{'nodes'} && $job->{'job_resrcs'}) {
		my @allocNodes = ();
		my $hl = Slurm::Hostlist::create($job->{'nodes'});
		my $inx = 0;
		my $cpu_cnt = 0;
		while((my $host = Slurm::Hostlist::shift($hl))) {
			push(@allocNodes, "$host/" .
			     Slurm->job_cpus_allocated_on_node_id(
				     $job->{'job_resrcs'}, $inx++));
		}
		$execHost = join '+', @allocNodes;
	}
	return $execHost;
}

###############################################################################
# Job Functions
###############################################################################
sub print_job_brief
{
	my ($job, $line_num) = @_;

	if(!$line_num) {
		printf("%-19s %-16s %-15s %-8s %-1s %-15s\n",
		       "Job id", "Name", "Username", "Time Use",  "S", "Queue");
		printf("%-19s %-16s %-15s %-8s %-1s %-15s\n",
		       '-' x 19, '-' x 16, '-' x 15, '-' x 8, '-' x 1, '-' x 15);
	}
	printf("%-19.19s %-16.16s %-15.15s %-8.8s %-1.1s %-15.15s\n",
	       $job->{'job_id'}, $job->{'name'}, $job->{'user_name'},
	       ddhhmm($job->{'statPSUtl'}), $job->{'stateCode'},
	       $job->{'partition'});
}

sub print_job_select
{
	my ($job) = @_;

	my $sessID = "--";
	my $execHost;

	if (!defined $header_printed) {
		print "\n${hostname}:\n";

		printf("%-20s %-8s %-8s %-20s %-6s %-5s %-5s %-6s %-5s %-1s %-5s\n",
		       "", "", "", "", "", "", "", "Req'd", "Req'd", "", "Elap");
		printf(
			"%-20s %-8s %-8s %-20s %-6s %-5s %-5s %-6s %-5s %-1s %-5s\n",
			"Job id", "Username", "Queue", "Name", "SessID", "NDS",
			"TSK",    "Memory",   "Time Use",  "S",       "Time"
			);
		printf(
			"%-20s %-8s %-8s %-20s %-6s %-5s %-5s %-6s %-5s %-1s %-5s\n",
			'-' x 20, '-' x 8, '-' x 8, '-' x 20, '-' x 6, '-' x 5,
			'-' x 5,  '-' x 6, '-' x 5, '-',      '-' x 5
			);
		$header_printed = 1;
	}
	$execHost = get_exec_host($job) if $nodes;

	printf("%-20.20s %-8.8s %-8.8s %-20.20s " .
	       "%-6.6s %5.5s %5.5s %6.6s %-5.5s %-1s %-5.5s",
	       $job->{'job_id'},
	       $job->{'user_name'},
	       $job->{'partition'},
	       $job->{'name'},
	       $sessID,
	       $job->{'num_nodes'} || "--",
	       $job->{'num_cpus'} || "--",
	       $job->{'job_min_memory'} || "--",
	       hhmm($job->{'time_limit'} * 60),
	       $job->{'stateCode'},
	       hhmm($job->{'aWDuration'}));

	if($execHost) {
		if(!$one) {
			printf("\n");
		}
		printf("   %s\n", $execHost);
	} else {
		printf("\n");
	}
}

sub print_job_full
{
	my ($job) = @_;
	# Print the job attributes
	printf("Job Id:\t%s\n", $job->{'job_id'});
	printf("\tJob_Name = %s\n", $job->{'name'}) if $job->{'name'};
	printf("\tJob_Owner = %s@%s\n",
	       $job->{'user_name'}, $job->{'alloc_node'});

	printf "\tinteractive = True\n" if !$job->{'batch_flag'};

	printf("\tjob_state = %s\n", $job->{'stateCode'});
	printf("\tqueue = %s\n", $job->{'partition'});

	printf("\tqtime = %s\n", hRTime($job->{'submit_time'}));
	printf("\tmtime = %s\n", hRTime($job->{'start_time'}))
		if $job->{'start_time'};
	printf("\tctime = %s\n", hRTime($job->{'end_time'}))
		if $job->{'end_time'};

	printf("\tAccount_Name = %s\n", $job->{'account'}) if $job->{'account'};

	my $execHost = get_exec_host($job);
	printf("\texec_host = %s\n", $execHost) if $execHost ne '--';

	printf("\tPriority = %u\n", $job->{'priority'});
	printf("\teuser = %s(%d)\n", $job->{'user_name'}, $job->{'user_id'});

	# can't run getgrgid inside printf it appears the input gets set to
	# x if ran there.
	my $user_group = getgrgid($job->{'group_id'});
	printf("\tegroup = %s(%d)\n", $user_group, $job->{'group_id'});

	printf("\tResource_List.walltime = %s\n", hhmmss($job->{'time_limit'} * 60));
	printf("\tResource_List.nodect = %d\n", $job->{'num_nodes'})
		if $job->{'num_nodes'};
	printf("\tResource_List.ncpus = %s\n", $job->{'num_cpus'})
		if $job->{'num_cpus'};

	if ($job->{'reqNodes'}) {
		my $nodeExpr = $job->{'reqNodes'};
		$nodeExpr .= ":ppn=" . $job->{'ntasks_per_node'}
		        if $job->{'ntasks_per_node'};

		printf("\tResource_List.nodes = %s\n", $nodeExpr);
	}

	print "\n";
}

###############################################################################
# Partition Functions
###############################################################################
sub print_part_brief
{
	my ($part, $line_num) = @_;

	if(!$line_num) {
		printf("%-16s %5s %5s %5s %5s %5s %5s %5s %5s %5s %5s %1s\n",
		       "Queue", "Max", "Tot", "Ena",  "Str", "Que", "Run",
		       "Hld", "Wat", "Trn", "Ext", "T");
		printf("%-16s %5s %5s %5s %5s %5s %5s %5s %5s %5s %5s %1s\n",
		       "----------------", "---", "---", "---",  "---", "---",
		       "---", "---", "---", "---", "---", "-");
	}
	printf("%-16.16s %5.5s %5.5s %5.5s %5.5s %5.5s %5.5s %5.5s " .
	       "%5.5s %5.5s %5.5s %1.1s\n",
	       $part->{'name'}, '?', '?', yes_no($part->{'state_up'}),
	       yes_no($part->{'state_up'}), '?', '?', '?', '?', '?', '?',
	       en_dis($part->{'state_up'}));
}

sub print_part_full
{
	my ($part) = @_;
	# Print the part attributes
	printf("Queue:\t%s\n", $part->{'name'});
	printf("\tqueue_type = Execution\n");
	printf("\tresources_default.nodes = %d\n", $part->{'total_nodes'});

	printf("\tenabled = %s\n", yes_no($part->{'state_up'}));

	printf("\tstarted = %s\n", yes_no($part->{'state_up'}));

	print "\n";
}

sub print_part_limits
{
	my ($part, $line_num) = @_;

	if(!$line_num) {
		printf("%-16s %6s %8s %8s %4s  %3s %3s %2s %5s\n",
		       "Queue", "Memory", "CPU Time", "Walltime",
		       "Node", "Run", "Que", "Lm", "State");
		printf("%-16s %6s %8s %8s %4s  %3s %3s %2s %5s\n",
		       "----------------", "------", "--------", "--------",
		       "----", "---", "---", "--", "-----");
	}


	printf("%-16.16s   --      --    ", $part->{'name'});
	if($part->{'max_time'} != INFINITE) {
		print(hhmmss($part->{'max_time'}*60));
	} else {
		printf("   --    ");

	}

	if($part->{'max_nodes'} != INFINITE) {
		printf("%4u  ", $part->{'max_nodes'});
	} else {
		printf("  --  ");
	}

	printf("%3u %3u --  %1.1s %1.1s \n", $part->{'running_jobs'},
	       $part->{'queued_jobs'}, en_dis($part->{'state_up'}),
	       running_stopped($part->{'state_up'}));
}


##############################################################################

__END__

=head1 NAME

B<qstat> - display job/partition information in a familiar pbs format

=head1 SYNOPSIS

B<qstat> [B<-f>] [B<-a>|B<-i>|B<-r>] [B<-n> [B<-1>]] [B<-G>|B<-M>] [B<-u> I<user_list>] [B<-? | --help>] [B<--man>] [I<job_id>...]

B<qstat> -Q [-f]

B<qstat> -q

=head1 DESCRIPTION

The B<qstat> command displays information about jobs.

=head1 OPTIONS

=over 4

=item B<-a>

Displays all jobs in a single-line format. See the STANDARD OUTPUT section for format details.

=item B<-i>

Displays information about idle jobs. This includes jobs which are queued or held.

=item B<-f>

Displays the full information for each selected job in a multi-line format. See the STANDARD OUTPUT section for format details.

=item B<-G>

Display size information in gigabytes.

=item B<-M>

Show size information, disk or memory in mega-words.  A word is considered to be 8 bytes.

=item B<-n>

Displays nodes allocated to a job in addition to the basic information.

=item B<-1>

In combination with -n, the -1 option puts all of the nodes on the same line as the job id.

=item B<-r>

Displays information about running jobs. This includes jobs which are running or suspended.

=item B<-u> I<user_list>

Display job information for all jobs owned by the specified user(s). The format of I<user_list> is: I<user_name>[,I<user_name>...].

=item B<-? | --help>

brief help message

=item B<--man>

full documentation

=back

=head1 STANDARD OUTPUT

Displaying Job Status

If the -a, -i, -f, -r, -u, -n, -G, and -M options are not specified, the brief single-line display format is used. The following items are displayed on a single line, in the specified order, separated by white space:

=over 4

=item the job id

=item the job name

=item the job owner

=item the cpu time used

=item the job state

C -  Job is completed after having run
E -  Job is exiting after having run.
H -  Job is held.
Q -  job is queued, eligible to run or routed.
R -  job is running.
T -  job is being moved to new location.
W -  job is waiting for its execution time
(-a option) to be reached.
S -  job is suspended.

=item the queue that the job is in

=back

If the -f option is specified, the multi-line display format is used. The output for each job consists of the header line:
B<Job Id>:  job identifier
followed by one line per job attribute of the form:
B<attribute_name = value>

If any of the options -a, -i, -r, -u, -n, -G or -M are specified, the normal single-line display format is used. The following items are displayed on a single line, in the specified order, separated by white space:

=over 4

=item the job id

=item the job owner

=item the queue the job is in

=item the job name

=item the session id (if the job is running)

=item the number of nodes requested by the job

=item the number of cpus or tasks requested by the job

=item the amount of memory requested by the job

=item either the cpu time, if specified, or wall time requested  by the job, (in hh:mm)

=item the job state

=item The amount of cpu time or wall time used by the job (in hh:mm)

=back

=head1 EXIT STATUS

On success, B<qstat> will exit with a value of zero. On failure, B<qstat> will exit with a value greater than zero.

=cut

