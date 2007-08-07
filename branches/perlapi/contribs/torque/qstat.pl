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
    $userList,         $hostname
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

$hostname = `hostname -f`;
chomp $hostname;
		
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

my @userIds = split /,/, $userList || '';

foreach my $job (@{$resp->{job_array}}) {
	my $use_time      = $now_time;
	my $state = $job->{'job_state'};
	if($job->{'end_time'} && $job->{'end_time'} < $now_time) {
		$use_time = $job->{'end_time'};
	} 	
	$job->{'statPSUtl'} =
		$use_time - $job->{'start_time'} - $job->{'suspend_time'};
	$job->{'aWDuration'} =
		$use_time - $job->{'start_time'} - $job->{'suspend_time'};

	$job->{'allocNodeList'} = $job->{'nodes'} || "--";
	$job->{'stateCode'} = stateCode($job->{'job_state'});
	$job->{'user_name'} = getpwuid($job->{'user_id'});
	if(!$job->{'name'}) {
		$job->{'name'} = "Allocation";	       
	}
	       
	switch($job->{'job_state'}) {
		case [JOB_COMPLETE, 
		      JOB_CANCELLED,
		      JOB_TIMEOUT,
		      JOB_FAILED]    { $job->{'stateCode'} = 'C' }
		case [JOB_RUNNING]   { $job->{'stateCode'} = 'R' }
		case [JOB_PENDING]   { $job->{'stateCode'} = 'Q' }
		case [JOB_SUSPENDED] { $job->{'stateCode'} = 'S' }
		else                 { $job->{'stateCode'} = 'U' }   # Unknown
	}


	# Filter jobs according to options and arguments
	if (@jobIds) {
		next unless grep /^$job->{'job_id'}/, @jobIds;
	} else {
		if ($running) {
			next unless ($job->{'stateCode'} eq 'R'
				     || $job->{'stateCode'} eq 'S');
		}
		if ($idle) {
			next unless ($job->{'stateCode'} eq 'Q');
		}
		if (@userIds) {
			next unless grep /^$job->{'user_name'}$/, @userIds;
		}
	}
	
	if ($all || $idle || $nodes || $running
	    || $giga || $mega || $userList) { # Normal
		print_select($job);
	} elsif ($full) { # Full
		print_full($job);
	} else { # Brief
		print_brief($job);
	}	
}


# Exit with status code
exit 0;

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

sub get_exec_host 
{
	my ($job) = @_;

	my $execHost = "--";
	if ($job->{'nodes'}) {
		my @allocNodes = ();
		my $hl = Slurm::Hostlist::create($job->{'nodes'});
		my $inx = 0;
		my $cpu_cnt = 0;
		while((my $host = Slurm::Hostlist::shift($hl))) {
			push(@allocNodes,
			     "$host/" . $job->{'cpus_per_node'}[$inx]);
			
			$cpu_cnt++;
			if($cpu_cnt >= $job->{'cpu_count_reps'}[$inx]) {
				$cpu_cnt = 0;
				$inx++;
			}			
		}
		$execHost = join '+', @allocNodes;
	}
	return $execHost;
}

sub print_brief 
{
	my ($job) = @_;

	printf("%-19s %-16s %-15s %-8s %-1s %-15s\n",
	       "Job ID", "Jobname", "Username", "Time",  "S", "Queue");
	printf("%-19.19s %-16.16s %-15.15s %-8.8s %-1.1s %-15.15s\n",
	       $job->{'job_id'}, $job->{'name'}, $job->{'user_name'},
	       ddhhmm($job->{'statPSUtil'}), $job->{'stateCode'},
	       $job->{'partition'});
}

sub print_select
{
	my ($job) = @_;

	my $sessID = "??????";
	my $execHost;

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

	if ($nodes) {
		$execHost = get_exec_host($job);
		
		if ($one) {
			$sessID = "--";
		}
	} 
	
	printf("%-20.20s %-8.8s %-8.8s %-10.10s " .
	       "%-6.6s %5.5s %3.3s %6.6s %-5.5s %-1s %-5.5s",
	       $job->{'job_id'}, 
	       $job->{'user_name'},
	       $job->{'partition'},
	       $job->{'name'},
	       $sessID,
	       $job->{'num_nodes'} || "--",
	       $job->{'num_procs'} || "--",
	       $job->{'job_min_memory'} || "--",
	       hhmm($job->{'time_limit'}),
	       $job->{'stateCode'},
	       hhmm($job->{'aWDuration'}));

	if($execHost) {
		if(!$one) {
			printf("\n");
		}
		printf("   %s\n", $execHost);
	} else {
		printf("\n", $execHost);
	}
}

sub print_full
{
	my ($job) = @_;
	# Print the job attributes
	printf("Job Id:\t%s\n", $job->{'job_id'});
	if($job->{'start_time'}) {
		printf("\tExecution_Time = %s\n",
		       hRTime($job->{'start_time'}));
		}
	if(!$job->{'batch_flag'}) {
		printf "\tinteractive = True\n"; 
	}
	if($job->{'name'}) {
		printf("\tJob_Name = %s\n", $job->{'name'}); 
	}
	printf("\tJob_Owner = %s@%s\n", 
	       $job->{'user_name'}, $job->{'alloc_node'});

	if($job->{'statPSUtl'}) {
		printf("\tresources_used.cput = %s\n",
		       hhmmss($job->{'statPSUtl'}));
	}

	if($job->{'aWDuration'}) {
		printf("\tresources_used.walltime = %s\n",
		       hhmmss($job->{'aWDuration'}));
	}
		
	printf("\tjob_state = %s\n", $job->{'stateCode'});
	printf("\tqueue = %s\n", $job->{'partition'});
	
	if($job->{'account'}) {				       
		printf("\tAccount_Name = %s\n", $job->{'account'});
	}

	my $execHost = get_exec_host($job);
	if($execHost ne '--') { 
		printf("\texec_host = %s\n", $execHost);
	}
	
	printf("\tPriority = %s\n", $job->{'priority'});
	
	printf("\tqtime = %s\n", hRTime($job->{'submit_time'}));
	
	if($job->{'num_nodes'}) {
		printf("\tResource_List.nodect = %d\n", $job->{'num_nodes'});
	}
	
	if ($job->{'reqNodes'}) {
		my $nodeExpr = $job->{'reqNodes'};
		if($job->{'ntasks_per_node'}) {
			$nodeExpr .= ":ppn=" . $job->{'ntasks_per_node'}; 
		}
		printf("\tResource_List.nodes = %s\n", $nodeExpr);
	}
	if($job->{'num_procs'}) {
		printf("\tResource_List.ncpus = %s\n", $job->{'num_procs'}); 
	} 

	
	if($job->{'reqAWDuration'}) {
		printf("\tResource_List.walltime = %s\n",
		       hhmmss($job->{'reqAWDuration'}));
	}

	printf("\teuser = %s\n", $job->{'user_name'});
	printf("\tegroup = %s\n", getgrgid($job->{'group_id'}));

	print "\n";
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

