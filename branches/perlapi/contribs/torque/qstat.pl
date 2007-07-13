#! /usr/bin/perl -w
###############################################################################
#
# qstat - queries slurm jobs in familar pbs format.
#
#              Copyright (c) 2006, 2007 Cluster Resources, Inc.
#
###############################################################################
#  Copyright (C) 2007 The Regents of the University of California.
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Danny Auble <auble1@llnl.gov>.
#  UCRL-CODE-226842.
#  
#  This file is part of SLURM, a resource management program.
#  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#  Based off code with permission copyright 2006, 2007 Cluster Resources, Inc.
###############################################################################


use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Switch;
# Parse Command Line Arguments
my (
    $all,              $diskReservation, $executable,   $full,
    $giga,             $help,            $idle,         $mega,
    $man,              $nodes,           $one,          $queueStatus,
    $queueDestination, $running,         $serverStatus, $siteSpecific,
    $userList
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
    'Q=s'    => \$queueDestination,
    'u=s'    => \$userList,
    'W=s'    => \$siteSpecific,
) or pod2usage(2);

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
my @jobIds = @ARGV;

# Handle unsupported arguments
#unsupportedOption("-e", DIE) if $executable;
#unsupportedOption("-W", DIE) if $siteSpecific;
#unsupportedOption("-R", DIE) if $diskReservation;
#unsupportedOption("-q", DIE) if $queueStatus;
#unsupportedOption("-Q <queue_destination>", DIE) if $queueDestination;
#unsupportedOption("-B", DIE) if $serverStatus;

# Build command
my $resp = Slurm->load_jobs(1);
if(!$resp) {
	die "Problem loading jobs.\n";
}

# foreach my $job (@{$resp->{job_array}}) {
# 	while(my ($key, $value) = each(%$job)) {
# 		print "$key => $value\n";
# 	}
# }
my $now_time = time();

# Normal
if ($all || $idle || $nodes || $running || $giga || $mega || $userList) {
	my $hostname = `hostname -f`;
	chomp $hostname;
	print "\n${hostname}:\n";
	
	printf("%-20s %-8s %-8s %-10s %-6s %-5s %-3s %-6s %-5s %-1s %-5s\n",
	       "", "", "", "", "", "", "", "Req'd", "Req'd", "", "Elap");
	printf(
	       "%-20s %-8s %-8s %-10s %-6s %-5s %-3s %-6s %-5s %-1s %-5s\n",
	       "Job ID", "Username", "Queue", "Jobname", "SessID", "NDS",
	       "TSK",    "Memory",   "Time",  "S",       "Time"
	       );
	printf(
	       "%-20s %-8s %-8s %-10s %-6s %-5s %-3s %-6s %-5s %-1s %-5s\n",
	       '-' x 20, '-' x 8, '-' x 8, '-' x 10, '-' x 6, '-' x 5,
	       '-' x 3,  '-' x 6, '-' x 5, '-',      '-' x 5
	       );
	
	foreach my $job (@{$resp->{job_array}}) {
		my $jobId         = $job->{'job_id'};
		my $SRMJID        = $job->{'job_id'};
		my $user          = $job->{'user_id'};
		my $class         = $job->{'partition'};
		my $jobName       = $job->{'name'} || "Allocation";
		my $reqNodes      = $job->{'num_nodes'} || "--";
		my $reqProcs      = $job->{'num_procs'} || "--";
		my $reqMemPerTask = $job->{'job_min_memory'};
		my $reqAWDuration = $job->{'time_limit'};
		my $state         = $job->{'job_state'};
		my $use_time      = $now_time;
		if($job->{'end_time'} && $job->{'end_time'} < $now_time) {
			$use_time = $job->{'end_time'};
		} 
		
		my $statPSUtl     = $use_time - 
			$job->{'start_time'} - $job->{'suspend_time'};
		my $aWDuration    = $use_time - 
			$job->{'start_time'} - $job->{'suspend_time'};
		my $allocNodeList = $job->{'nodes'} || "--";
		
		my $stateCode = stateCode($state);
		
		my @userIds = split /,/, $userList || '';
		
		# Filter jobs according to options and arguments
		if (@jobIds) {
			next unless grep /^$jobId/, @jobIds;
		} else {
			if ($running) {
				next unless ($stateCode eq 'R' || $stateCode eq 'S');
			}
			if ($idle) {
				next unless ($stateCode eq 'Q' || $stateCode eq 'H');
			}
			if (@userIds) {
				next unless grep /^$user$/, @userIds;
			}
		}
		
		if ($reqMemPerTask) {
			if ($giga) {
				$reqMemPerTask = int($reqMemPerTask / 1024);
			} elsif ($mega) {
				$reqMemPerTask = int($reqMemPerTask / 8);
			}
		} else {
			$reqMemPerTask = "--";
		}
		
		if ($nodes) {
			my $execHost = "--";
			if ($allocNodeList) {
				my @allocNodes = ();
				foreach my $allocNode (split /,/, $allocNodeList) {
					$allocNode =~ /([^:]+):(\d*)/;
					my ($host, $num) = ($1, $2 || 1);
					for (my $i = $num - 1; $i >= 0; $i--) {
						push @allocNodes, "$host/$i";
					}
				}
				$execHost = join '+', @allocNodes;
			}
			if ($one) {
				printf(
				       "%-20.20s %-8.8s %-8.8s %-10.10s %-6.6s %5.5s %3.3s %6.6s %-5.5s %-1s %-5.5s   %s\n",
				       $SRMJID,              $user,
				       $class,               $jobName,
				       "--",                 $reqNodes,
				       $reqProcs,            $reqMemPerTask,
				       hhmm($reqAWDuration), $stateCode,
				       hhmm($aWDuration),    $execHost
				       );
			} else {
				printf(
				       "%-20.20s %-8.8s %-8.8s %-10.10s %-6.6s %5.5s %3.3s %6.6s %-5.5s %-1s %-5.5s\n",
				       $SRMJID,              $user,
				       $class,               $jobName,
				       "??????",             $reqNodes,
				       $reqProcs,            $reqMemPerTask,
				       hhmm($reqAWDuration), $stateCode,
				       hhmm($aWDuration)
				       );
				printf("   %s\n", $execHost);
			}
		} else {
			printf(
			       "%-20.20s %-8.8s %-8.8s %-10.10s %-6.6s %5.5s %3.3s %6.6s %-5.5s %-1s %-5.5s\n",
			       $SRMJID,    $user,          $class,
			       $jobName,   "??????",       $reqNodes,
			       $reqProcs,  $reqMemPerTask, hhmm($reqAWDuration),
			       $stateCode, hhmm($aWDuration)
			       );
		}
	}
} elsif ($full) { # Full
	foreach my $job (@{$resp->{job_array}}) {
		my $jobId          = $job->{'JobID'};
		my $SRMJID         = $job->{'SRMJID'};
		my $jobName        = $job->{'JobName'};
		my $user           = $job->{'User'};
		my $sysSMinTime    = $job->{'SysSMinTime'};
		my $state          = $job->{'State'};
		my $class          = $job->{'Class'};
		my $account        = $job->{'Account'};
		my $submissionTime = $job->{'SubmissionTime'};
		my $allocNodeList  = $job->{'reqs'}->[0]->{'AllocNodeList'};
		my $hold           = $job->{'Hold'};
		my $startPriority  = $job->{'StartPriority'};
		my $flags          = $job->{'Flags'};
		my $reqNodes       = $job->{'ReqNodes'};
		my $TPN            = $job->{'reqs'}->[0]->{'TPN'};
		my $reqAWDuration  = $job->{'ReqAWDuration'};
		my $group          = $job->{'Group'};
		my $reqArch        = $job->{'reqs'}->[0]->{'ReqArch'};
		my $reqMemPerTask  = $job->{'reqs'}->[0]->{'ReqMemPerTask'};
		my $reqProcs       = $job->{'ReqProcs'};
		my $reqSwapPerTask = $job->{'reqs'}->[0]->{'ReqSwapPerTask'};
		my $statPSUtl      = $job->{'StatPSUtl'};
		my $aWDuration     = $job->{'AWDuration'};

		# Filter jobs according to options and arguments
		if (@jobIds) {
			next unless grep /^$jobId/, @jobIds;
		}

		# Prepare variables
		my $execHost;
		my $stateCode = stateCode($state);
		my $hostname  = `hostname -f`;
		chomp $hostname;
		if ($allocNodeList) {
			my @allocNodes = ();
			foreach my $allocNode (split /,/, $allocNodeList) {
				if ($allocNode =~ /([^:]+):(\d*)/) {
					my ($host, $num) = ($1, $2 || 1);
					for (my $i = $num - 1; $i >= 0; $i--) {
						push @allocNodes, "$host/$i";
					}
				}
			}
			$execHost = join '+', @allocNodes;
		}
		my $holdTypes = "";
		if ($hold) {
			if ($hold =~ /System/) { $holdTypes .= 's'; }
			if ($hold =~ /Batch/)  { $holdTypes .= 'o'; }
			if ($hold =~ /User/)   { $holdTypes .= 'u'; }
		}
		else {
			$holdTypes = "n";
		}
		my $rerunable = "False";
		$flags = "" unless defined $flags;
		if ($flags =~ /Restartable/i) { $rerunable = "True"; }

		# Print the job attributes
		printf "Job Id: %s\n",              $SRMJID;
		printf "    Execution_Time = %s\n", hRTime($sysSMinTime)
			if $sysSMinTime;
		printf "    interactive = True\n" if $flags =~ /Interactive/i;
		printf "    Job_Name = %s\n", $jobName if $jobName;
		printf "    Job_Owner = %s@%s\n", $user, $hostname;
		printf "    resources_used.cput = %s\n", hhmmss($statPSUtl)
			if $statPSUtl;
		printf "    resources_used.walltime = %s\n", hhmmss($aWDuration)
			if $aWDuration;
		printf "    job_state = %s\n",    $stateCode;
		printf "    queue = %s\n",        $class;
		printf "    Account_Name = %s\n", $account if $account;
		printf "    exec_host = %s\n",    $execHost if $execHost;
		printf "    Hold_Types = %s\n",   $holdTypes;
		printf "    Priority = %s\n",     $startPriority if $startPriority;
		printf "    qtime = %s\n",        hRTime($submissionTime)
			if $submissionTime;
		printf "    Rerunable = %s\n",           $rerunable;
		printf "    Resource_List.arch = %s\n",  $reqArch if $reqArch;
		printf "    Resource_List.mem = %smb\n", $reqMemPerTask
			if $reqMemPerTask;
		printf "    Resource_List.nodect = %s\n", $reqNodes if $reqNodes;

		if ($reqNodes)
		{
			my $nodeExpr = $reqNodes;
			$reqNodes .= ":ppn=$TPN" if $TPN;
			printf "    Resource_List.nodes = %s\n", $nodeExpr;
		}
		printf "    Resource_List.ncpus = %s\n", $reqProcs if $reqProcs;
		printf "    Resource_List.vmem = %smb\n", $reqSwapPerTask
			if $reqSwapPerTask;
		printf "    Resource_List.walltime = %s\n", hhmmss($reqAWDuration)
			if $reqAWDuration;
		printf "    euser = %s\n", $user;
		printf "    egroup = %s\n", $group if $group;
		print "\n";
	}
} else { # Brief
	printf("%-19s %-16s %-15s %-8s %-1s %-5s\n",
	       "Job id", "Name", "User", "Time Use", "S", "Queue");
	printf("%-19s %-16s %-15s %-8s %-1s %-5s\n",
	       '-' x 19, '-' x 16, '-' x 15, '-' x 8, '-', '-' x 5);

	foreach my $job (@{$resp->{job_array}}) {
		my $jobId      = $job->{'JobID'};
		my $jobName    = $job->{'JobName'};
		my $user       = $job->{'User'};
		my $statPSUtil = $job->{'StatPSUtl'};
		my $state      = $job->{'State'};
		my $class      = $job->{'Class'};
		printf("%-19.19s %-16.16s %-15.15s %-8.8s %-1.1s %-15.15s\n",
		       $jobId, $jobName, $user, ddhhmm($statPSUtil), stateCode($state),
		       $class);
	}
}

# Exit with status code
exit 0;


################################################################################
# $hhmm = hhmm($hhmmss)
# Converts a duration in hhmmss to hhmm
################################################################################
sub hhmm
{
	my ($hhmmss) = @_;

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
# $stateCode = stateCode($state)
# Converts a slurm state into a one-character pbs state code
################################################################################
sub stateCode
{
	my ($state) = @_;
	if(!defined($state)) {
		print "No state given\n";
		return 'U';
	}

	switch($state) {
		case [JOB_COMPLETE, 
		      JOB_CANCELLED,
		      JOB_TIMEOUT,
		      JOB_FAILED]    { return 'C' }
		case [JOB_RUNNING]   { return 'R' }
		case [JOB_PENDING]   { return 'Q' }
		case [JOB_SUSPENDED] { return 'S' }
		else                 { return 'U' }   # Unknown
	}
}

################################################################################
# $humanReadableTime = hRTime($epochTime)
# Converts an epoch time into a human readable time
################################################################################
sub hRTime
{
	my ($epochTime) = @_;

	return scalar localtime $epochTime;
}

##############################################################################

__END__

	=head1 NAME

	B<qstat> - display job information in a familiar pbs format

	=head1 SYNOPSIS

	B<qstat> [B<-f>] [B<-a>|B<-i>|B<-r>] [B<-n> [B<-1>]] [B<-G>|B<-M>] [B<-u> I<user_list>] [B<-? | --help>] [B<--man>] [I<job_id>...]

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

