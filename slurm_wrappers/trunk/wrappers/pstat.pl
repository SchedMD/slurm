#! /usr/bin/perl -w

#
# pstat - Show SLURM jobs in LCRM format.
#
# Modified:	07/14/2010
# By:		Phil Eckert
#

#
# For debugging.
#
#use lib "/var/opt/slurm_dawn/lib64/perl5/site_perl/5.8.8/x86_64-linux-thread-multi/";


BEGIN {
#
#	Just dump the man page in *roff format and exit if --roff specified.
#
	foreach my $arg (@ARGV) {
		last if ($arg eq "--");
		if ($arg eq "--roff") {
			use Pod::Man;
			my $parser = Pod::Man->new (section => 1);
			$parser->parse_from_file($0, \*STDOUT);
			exit 0;
		}
	}
}

use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib qw(/opt/freeware/lib/site_perl/5.8.2/aix-thread-multi);

use autouse 'Pod::Usage' => qw(pod2usage);
use Switch;
use strict;
use Slurm ':all';
use Slurmdb ':all';

#
# Each entry in this hash table must have a subhash.  The possible keys are:
#   short - short format, falls back to length of key name
#   long - long format, falls back to short
#   obsolete - SPECIAL CASE, when first parsed, this spec will be
#              replaced by the spec specified in the 'obsolete' subkey
#
my $spectable = {
	'aging_time'       => {'short'    => '%-19.19s'},
	'bank'             => {'short'    => '%-11.11s', 'long' => '%-31s'},
	'batchid'          => {'short'    => '%-10s'},
	'cl'               => {'short'    => '%-2s'},
	'constraint'       => {'short'    => '%-10.10s'},
	'cpus'             => {'short'    => '%5s'},
	'cpn'              => {'short'    => '%6s'},
	'default'          => {}, # SPECIAL CASE!  Expanded later in the code.
	'depend'           => {'short'    => '%-6s'},
	'ecomptime'        => {},
	'earliest_start'   => {'short'    => '%-17s'},
	'exehost'          => {'short'    => '%-9s'},
	'highwater'        => {},
	'jid'              => {'short'    => '%12.12s'},
	'maxcputime'       => {'short'    => '%10s'},
	'maxmem'           => {},
	'maxnodes'         => {'obsolete' => 'nodes'},
	'maxphyss'         => {},
	'maxrss'           => {},
	'maxruntime'       => {'short'    => '%10s'},
	'maxtime'          => {'obsolete' => 'maxcputime'},
	'memint'           => {},
	'memsize'          => {'short'    => '%8.8s'},
	'name'             => {'short'    => '%-17.17s'},
	'nodes'            => {'short'    => '%6s'},
	'not_before_time'  => {'obsolete' => 'earliest_start'},
	'pool'             => {'short'    => '%8s'},
	'preempted_by'     => {},
	'priority'         => {'short'    => '%8.8s'},
	'runtime'          => {'short'    => '%10s'},
	'sid'              => {},
	'status'           => {'short'    => '%-10s'},
	'stoptime'         => {},
	'submitted'        => {'short'    => '%-17s'},
	'suspended_by'     => {},
	'tasks'            => {},
	'timecharged'      => {},
	'timeleft'         => {'short'    => '%10s'},
	'used'             => {'short'    => '%10.10s'},
	'user'             => {'short'    => '%-11.11s', 'long' => '%-31s'},
	'vmemint'          => {}
};

#
# Parse Command Line Arguments
#
my (
	$all,          $bank,      $constraints, $delayed,
	$full,         $help,      $jobList,        $long,
	$man,          $multiple,  $noHeader,       $outputFormat,
	$running,      $sortOrder, $terminated,     $userName,
	$verbose
);

my $mask = 0x8000;

my @States = qw /
        ELIG
        RUN
        SUSPENDED
        COMPLETE
        CANCELLED
        FAILED
        TIMEOUT
        NODE_FAIL
/;

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);

if ($sversion < 2.2) {
	printf("\n Hold/Release functionality not available in this release.\n\n");
	exit(1);
}

GetOptions(
	'A'          => \$all,
	'a'          => \$all,
	'b=s'        => \$bank,
	'c=s'        => \$constraints,
	'D'          => \$delayed,
	'f'          => \$full,
	'noheader|H' => \$noHeader,
	'help|h|?'   => \$help,
	'L'          => \$long,
	'm=s'        => \$constraints,
	'man'        => \$man,
	'M'          => \$multiple,
	'n=s'        => \$jobList,
	'o=s'        => \$outputFormat,
	'R'          => \$running,
	'T'          => \$terminated,
	's=s'        => \$sortOrder,
	'u=s'        => \$userName,
	'v'          => \$verbose,
) or pod2usage(2);


#
# Display usage
#
if ($help) {
	pod2usage(-verbose => 0, -exit => 'NOEXIT', -output => \*STDOUT);
	print "Report problems to LC Hotline.\n";
	exit(0);
}


#
# Display man page
#
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

#
# Use single argument as job id or fail if extra args
#
if (@ARGV) {
	if (!$jobList && @ARGV == 1) {
		$jobList = $ARGV[0];
	} else {
		pod2usage(2);
	}
}

#
# Fail on unsupported option combinations
#
pod2usage(-message => "The -o and -v options may not be used together", -verbose => 0) if $outputFormat && $verbose;
pod2usage(-message => "The -o and -f options may not be used together", -verbose => 0) if $outputFormat && $full;
pod2usage(-message => "The -v and -f options may not be used together", -verbose => 0) if $verbose && $full;


#
# Create the output spec array
#
my @outspec = ();
my @outspec_normal  = ('jid', 'name', 'user', 'bank',
		      'status', 'exehost', 'cl');
my @outspec_verbose = (@outspec_normal,
		       'aging_time', 'maxcputime', 'memsize',
		       'depend', 'used', 'earliest_start');
if ($verbose) {
	@outspec = (@outspec_verbose);
} elsif ($outputFormat || exists $ENV{PSTAT_CONFIG}) {
	$outputFormat = $ENV{PSTAT_CONFIG} unless $outputFormat;
	$outputFormat =~ s/^\s+//;    # Remove leading whitespace
	$outputFormat =~ s/\s+$//;    # Remove trailing whitespace
	foreach my $spec (split /\s*[, ]\s*/, $outputFormat) {
		$spec = lc $spec;
		if (my $full = abbreviation($spectable, $spec)) { $spec = $full; }
		logDie("Unrecognized outspec: $spec\n") unless exists $spectable->{$spec};
		if ($spec eq "default") {
			push @outspec, @outspec_normal;
		} elsif (exists $spectable->{$spec}->{'obsolete'}) {
			push @outspec, $spectable->{$spec}->{'obsolete'};
		} else {
			push @outspec, $spec;
		}
	}
} else {
	@outspec = (@outspec_normal);
}

#
# Create the sort order array
#
my @sort_order = ();
if (!defined $sortOrder) {
	push @sort_order, [1, "jid"];
} else {
	$sortOrder =~ s/^\s+//;    # Remove leading whitespace
	$sortOrder =~ s/\s+$//;    # Remove trailing whitespace
	foreach my $spec (split /\s*[, ]\s*/, $sortOrder) {
		$spec = lc $spec;
		my $ascending;
		if ($spec =~ /^-(.*)/) {
			$spec = $1;
			$ascending = 0;
		} else {
			$ascending = 1;
		}
	
		if (my $full = abbreviation($spectable, $spec)) { $spec = $full; }
		logDie("Unrecognized outspec: $spec\n") unless exists $spectable->{$spec};
		if (exists $spectable->{$spec}->{'obsolete'}) {
			$spec = $spectable->{$spec}->{'obsolete'}
		}
	
#
#		Priority sorts in the reverse order.
#
		if ($spec eq 'priority') {
			$ascending = $ascending ? 0 : 1;
		}

		push @sort_order, [$ascending, $spec];
	}
}

#
# Build command
#

my $Now = time();

my ($eval_reason, $eval_state);

my $tmp = `scontrol show config | grep ClusterName`;;
my ($host) = ($tmp =~ m/ = (\S+)/); 

#
# Use SLURM perl api to get job info.
#
my $jobs = Slurm->load_jobs();
unless($jobs) {
	my $errmsg = Slurm->strerror();
	print "Error loading jobs: $errmsg";
}

#
# Iterate over the jobs
#
my @all_jobs;
foreach my $job (@{$jobs->{job_array}}) {
	my $jobId           = $job->{job_id};
#	next if ($jobId > 1000000);
	my $user            = getpwuid($job->{user_id});
	my $jobName         = $job->{name} || 'N/A';
	my $dRMJID          = $job->{job_id};
	my $account         = $job->{account} || 'N/A';
	my $block           = 'N/A';
	my $reason          = Slurm->job_reason_string($job->{state_reason}) || "N/A";
	my $state           = Slurm->job_state_string($job->{job_state}) || "N/A";
	next if (($state eq "COMPLETED"  || 
		  $state eq "FAILED") || 
		  $state eq "CANCELLED" ||
		  $state eq "TIMEOUT" && !$terminated);
	my $hold            = 'N/A';
	my $allocPartition  = $host;
	my $reqPartition    = $host;
	my $qosReq          = 'N/A';
	my $depend          = 'N/A';
	my $startPriority   = $job->{priority} || 0;
	my $tpn             = 'N/A';
	my $reqNodes        = $job->{num_nodes};
	my $reqProcs        = $job->{num_cpus} || 0;
	my $class           = $job->{partition};
	my $variable        = 'N/A';
	my $submissionTime  = $job->{submit_time};
	my $systemQueueTime = $job->{submit_time} || 'N/A';
	my $startTime       = $job->{start_time};
	my $completionTime  = $job->{end_time};
	my $reqSMinTime     = $job->{start_time};
	my $reqNodeFeature  = $job->{features} || 'N/A';
	my $EffPAL          = 'N/A';
	my $reqNodeMem      = $job->{job_min_memory} || 'N/A';
	my $reqNodeDisk     = $job->{job_min_tmp_disk} || 'N/A';
	my $statPSDed       = 'N/A';
	my $reqAWDuration   = $job->{time_limit} * 60;
	my $aWDuration      = ($Now - $job->{start_time}) if ($state ne "PENDING");;
	my $cpuLimit        = $job->{time_limit};
	my $sid             = $job->{alloc_sid} || "???";
#
#	Prepare variables
#
	my $finalState = finalState($state, $reason);

#
#	If the earliest start time is defined, rewrite finalState.
#
#	if (exists $job->{'ReqSMinTime'} && $finalState !~ /RUN/) {
#		my $now = time();
#		if ($job->{'ReqSMinTime'} > $now) { $finalState = "*WAIT"; }
#	}

#
#	Remove quotes from jobName.
#
	$jobName =~ s/\"//g;

#
#	The job class. 'D' means delayed, 'E' means exempted,
#	'N' means normal, 'S' means standby, 'X' means expedited.
#
	my $status = "N";
	if (defined $qosReq) {
		$status = "D" if $qosReq eq "delayed";
		$status = "S" if $qosReq eq "standby";
		$status = "X" if $qosReq eq "expedite";
	}
	my $exeHost = "???";
	if ($finalState =~ /RUN/ && defined $allocPartition) {
		$exeHost = $allocPartition;
	} elsif (defined $reqPartition) {
		$exeHost = $reqPartition;
	}
	my $agingTime = $submissionTime;
	if ($finalState =~ /ELIG|IDLE/ && defined $systemQueueTime) {
		$agingTime = $systemQueueTime;
	} elsif ($finalState =~ /RUN/ && defined $startTime) {
		$agingTime = $startTime;
	} elsif ($finalState =~ /COMPLETE|REMOVED/ && defined $systemQueueTime) {
		$agingTime = $completionTime;
	}
#
#	New field from CRI to make determining a transient dependency.
#
	my $dependency = "none";
	if ($depend) {
		$dependency = $1 if ($depend =~ /jobcomplete:(\w+)/);
	}
	my @features = ();
	if ($reqNodeFeature) {
		@features = split(/[\[\]]+/, $reqNodeFeature);
		shift @features;
	}
    
#
#	we want to see all jobs eligable for a node also
#
	my @eff_features = @features;    
	if($EffPAL) {
		push(@eff_features, split(/[\[\]]+/, $EffPAL));
		shift @eff_features;
	}    

#
#	Filter jobs according to options and arguments
#
	if ($bank)     { next unless $account eq $bank; }
	if ($userName) { next unless $user    eq $userName; }
	if ($constraints) {
		my $matchConstraint = 0;
	        foreach my $constraint (split /,/, $constraints) {
			$matchConstraint = 1 if grep { $constraint eq $_ } @eff_features;
		}
		next unless $matchConstraint;
	}
	if ($delayed) { next unless $status eq "D"; }
	if ($running) { next unless $finalState =~ /RUN/; }
	if ($jobList) {
		next unless grep { $_ eq $jobId } split(/,/, $jobList);
	}
	if (!$all &&
	    !$bank &&
	    !$constraints &&
	    !$delayed &&
	    !$jobList &&
	    !$running &&
	    !$terminated &&
	    !$userName) {
		my $userId = (getpwuid($<))[0];
		next unless $user eq $userId;
	}

#
#	Rewrite finalState if job is dependent on another job.
#
	$finalState = "*DEPEND" if ($dependency ne "none");

#
#	Full output
#
	if ($full) {
#
#		Display header data including job id and owner
#
		print "-" x 79, "\n";
		printf("MOAB BATCH JOB ID %19.19s  user: %34.34s\n", $jobId, $user);
		print "-" x 79, "\n";

#
#		Display other job identification data: job name, batch id,
#		session id, bank and dependency
#
		printf("  job name:         %17.17s  bank:    %31.31s\n",
			$jobName, $account);
		printf("  batch ID:%26s\n", $dRMJID);
		printf("  session ID:%24s  dependency:         %20s\n\n",
		       $sid, $dependency ? $dependency : "none");

#
#		Display the job's execution host, status, class, priority and
#		suspension data
#
		if ($status eq "D") {
			printf( "  executing host:                 N/A  job status:                      DELAYED\n");
		} else {
			printf("  executing host: %19s  job status:           %18s\n",
				$exeHost, $finalState);
		}
		printf("  expedited:        %17s  standby:               %17s\n",
			$status eq "X" ? "yes" : "no",
			$status eq "S" ? "yes" : "no");
		printf("  priority:         %17s  suspended by:          %17s\n",
			$startPriority ? $startPriority : "N/A", "N/A");

#
#		Display the job's cpus per node, min and max cpus,
#		node specification, geometry and constraint
#
		printf("  cpn: %30s  min/max nodes:         %17s\n",
			$reqProcs ? $reqProcs : "N/A",
			$reqNodes ? $reqNodes : "N/A");
		printf("  node distribution: %16s  geometry:%31s\n",
			$reqNodes ? $reqNodes : "N/A", "N/A");
		printf("  constraint:%66s\n", (join(',', @features) || "N/A"));
		if (defined $qosReq && $qosReq =~ /^exempt(\w+)/) {
			printf("  exemptions:%66s\n", $1);
		}
		if ($class) {
			printf("  pool:%72s\n", $class);
		}
		if (defined $variable && $variable =~ /Project=([^;]+)/) {
			printf("  project:%69s\n", $1);
		}

#
#		Display the job's submission time, earliest start time, stop time,
#		estimated completion time (or actual termination time), run time
#		limit, time limit per cpu, elapsed run time number of cpus,
#		time used, time used per cpu and time charged
#
		printf("\n  submitted at:   %19s  earliest start time: %19s\n",
       			hRTime($submissionTime),
			$reqSMinTime ? hRTime($reqSMinTime) : "N/A");
		printf("  must stop at:   %19s", "N/A");
		if ($finalState eq "REMOVED" || $finalState eq "COMPLETE") {
			printf("  terminated at:       %19s\n", hRTime($completionTime));
		} else {
			printf("  estimated completion:%19s\n", "N/A");
		}
		printf("  resources used:   %17s  time charged:          %17s\n",
			$statPSDed, "???");
		printf("  elapsed run time limit: %11s  elapsed run time:      %17s\n",
			$reqAWDuration ? hhmm($reqAWDuration) : "default",
			$aWDuration    ? hhmm($aWDuration)    : "N/A");
		printf(
		"  time limit per cpu: %15s  time used per cpu:%22s\n",
		$cpuLimit  ? hhmm($cpuLimit)  : "default", "N/A");
		printf("  cpus:             %17d\n\n", $reqProcs);
		print "\n";
	}
#
#	Normal output
#
	else {
		my $value = {};
#
#		Fill values for this job in the spectable
#
		$value->{'aging_time'} = hRTime($agingTime);
		$value->{'bank'} = $account;
		$value->{'batchid'} = $jobId;
		$value->{'cl'} = $status;
		$value->{'cpn'} = $reqProcs if $reqProcs;
		$value->{'cpus'} = $reqProcs if $reqProcs;
		$value->{'depend'} = $dependency;
		$value->{'earliest_start'} = hRTime($reqSMinTime) if $reqSMinTime;
		$value->{'exehost'} = $exeHost;
		$value->{'jid'} = $jobId;
		$value->{'constraint'} = (join(',', @features) || "N/A");
		$value->{'maxcputime'} = $cpuLimit ? hhmm($cpuLimit) : "default";
		$value->{'maxruntime'} = $reqAWDuration ? hhmm($reqAWDuration) : "default";
		my $used = $completionTime - $startTime;
#		$value->{'maxruntime'} = $used ? hhmm($used) : "default";
		$value->{'name'} = $jobName;
		$value->{'nodes'} = $reqNodes if $reqNodes;
		$value->{'pool'} = $class if $class;
		$value->{'priority'} = $startPriority if $startPriority;
		$value->{'runtime'} = hhmm($aWDuration) if $aWDuration;
		$value->{'status'} = $finalState;
		$value->{'submitted'} = hRTime($submissionTime);
		if ($reqAWDuration && $aWDuration) {
			$value->{'timeleft'} =
#			   hhmm($reqAWDuration - $aWDuration);
			   hhmm($completionTime - $Now);
		}
		$value->{'used'} = hhmm($aWDuration) if $aWDuration;
		$value->{'user'} = $user;

		push @all_jobs, $value;
	}
}

if (!$full) {
#
#	first print the header
#
	if (!$noHeader) {
		my $format;
		my $line = '';
		foreach my $spec (@outspec) {
			$format = '';
			$format = $spectable->{$spec}->{'long'} if $long;
			$format = $spectable->{$spec}->{'short'} unless $format;
			$format = '%s' unless $format; # unsupported spec
			$line .= sprintf "$format", uc $spec;
			$line .= " ";
		}
		$line =~ s/\s+$//; # Remove trailing whitespace
		print "$line\n";
	}

#
#	Then sort the @all_jobs job info
#
	my $spec = 'jid';
	my @sorted = sort job_cmp @all_jobs;

#
#	Finally, print out the job lines
#
	foreach my $job (@sorted) {
		my $format;
		my $value;
		my $line = '';
		foreach my $spec (@outspec) {
			$format = '';
			$format = $spectable->{$spec}->{'long'} if $long;
			$format = $spectable->{$spec}->{'short'} unless $format;
			$format = ('%-' . (length $spec) . 's') unless $format;
			if (exists $job->{$spec} && $job->{$spec}) {
				$value = $job->{$spec}
			} else {
				$value = 'N/A';
			}
			$line .= sprintf "$format", $value;
			$line .= " ";
		}
		$line =~ s/\s+$//;
		print "$line\n";
	}
}

#
# Exit with status code
#
exit(0);

#
# $speckey = abbreviation($hashptr, $abbrev)
# Determines if a abbrev is a unique abbreviation of a key in the hashtable
#
sub abbreviation
{
	my ($hashptr, $abbrev) = @_;
	my $match = '';
	my $matches = 0;

	foreach my $key (sort keys %$hashptr) {
		if ($key =~ /^$abbrev/) {
			$match = $key;
			$matches++;
		}
	}

	Die("\"$abbrev\" is a non-unique abbreviation.\n") if ($matches > 1);

	return($match);
}

#
# $dateTime = hRTime($epochTime)
# Converts an epoch time to human readable time
#
sub hRTime
{
	my ($epochTime) = @_;

#
#	Convert epoch time to human readable time
#
	my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday, $isdst) =
		localtime($epochTime);
	my $hRTime = sprintf( "%02d/%02d/%02d %02d:%02d:%02d",
		$mon + 1, $mday, ($year + 1900) - 2000,
		$hour, $min, $sec);

	return($hRTime);
}

#
# $hhmmss = hhmm($hhmmss)
# Converts a duration in hh:mm:ss to hh:mm
#   OR in form XXXdays to hh:mm
#
sub hhmm
{
	my ($hhmmss) = @_;
	my ($dd, $hh, $mm, $ss);

#
#	Convert hhmmss to duration in minutes
#
	$hhmmss = 0 unless $hhmmss;
	if ($hhmmss =~ /(\d+)days/) {
        	($hh, $mm, $ss) = ($1 * 24, 0, 0);
	} else {
		my @ct = split(/:/, $hhmmss);
		if (@ct == 4) {
			$hhmmss =~ /(?:(\d+):)?(?:(\d+):)?(?:(\d+):)?([\d\.]+)/;
			($dd, $hh, $mm, $ss) = ($1 || 0, $2 || 0, $3 || 0, $4 || 0);
			$hh += $dd * 24;
		} else {
			$hhmmss =~ /(?:(\d+):)?(?:(\d+):)?([\d\.]+)/;
			($hh, $mm, $ss) = ($1 || 0, $2 || 0, $3 || 0);
		}
	}

	my $duration = int($ss / 60);
	$duration += ($mm * 1);
	$duration += ($hh * 60);

#
#	Convert duration in minutes to hhmm
#
	my $hours = int($duration / 60);
	$duration = $duration - $hours * 60;
	my $minutes = $duration;
	my $hhmm = sprintf('%02d:%02d', $hours, $minutes);

	return($hhmm);
}

#
# $finalState = finalState($state)
# Converts a SLURM state into an lcrm state.
#
sub finalState
{
	my ($state, $reason) = @_;

	my $finalState;
	if    ($state =~ /Cancelled|Removed|Vacated/) { $finalState = ' REMOVED'; }
	elsif ($state =~ /COMPLETED/)                 { $finalState = ' COMPLETE'; }
	elsif ($state =~ /RUNNING/)     	      { $finalState = ' RUN'; }
	elsif ($state =~ /PENDING/)  {
		if ($reason =~ /None/) {
		 	$finalState = '*ELIG'; 
		} else {
			$finalState = '*' . uc($reason); 
		}
	}
	elsif ($state =~ /SUSPENDED/)                 { $finalState = ' SUSPENDED'; }
	else { $finalState = $state; }

	$finalState = "*DEPEND"  if ($reason =~ /Depend/);
	$finalState = "*HELDu"   if ($reason =~ /JobHeldUser/);
	$finalState = "*HELDs"   if ($reason =~ /JobHeldAdmin/);
	$finalState = "*TooLong" if ($reason =~ /PartitionTimeLimit/);
	$finalState = "*Wait"    if ($reason =~ /BeginTime/);

	return($finalState);
}


#
# Compare two jobs based on the global @sort_order, and return less than
# equal to, or greater than zero, as suitable for the perl "sort" builtin.
#
sub job_cmp
{
	my $rc = 0;
	my $vala = '';
	my $valb = '';

	foreach my $tmp (@sort_order) {
		my $ascending = $tmp->[0];
		my $spec = $tmp->[1];

		if (exists $a->{$spec}) {
			$vala = $a->{$spec};
		}
		if (exists $b->{$spec}) {
			$valb = $b->{$spec};
		}

		if (($vala =~ /^-?(\d+)(\.\d+)?$/) && ($valb =~ /^-?(\d+)(\.\d+)?$/)) {
#
#			Do a numerical sort if they both look like numbers.
#
			if ($ascending) {
				$rc = $vala <=> $valb;
			} else {
				$rc = $valb <=> $vala;
			}
		} else {
#
#			Do normal string sort
#
			if ($ascending) {
				$rc = $vala cmp $valb;
			} else {
				$rc = $valb cmp $vala;
			}
		}

		last if ($rc != 0);
	}

	return($rc);
}


##############################################################################

__END__

=head1 NAME

B<pstat> - display job information in a familiar lcrm format

=head1 SYNOPSIS

B<pstat> [B<-b> I<bank>] [B<-u> I<user>] [B<-c> I<constraint_list> | B<-m> I<host>] [B<-D>] [B<-H>] [B<-L>] [B<-R>] [B<-T>] [B<-v> | B<-f> | B<-o> I<outspec>]]

B<pstat> [I<jobid> | B<-n> I<jobid_list>] [B<-H>] [B<-L>] [B<-T>] [B<-v> | B<-f> | B<-o> I<outspec>]

B<pstat> B<-A> [B<-D>] [B<-H>] [B<-L>] [B<-R>] [B<-T>] [B<-v> | B<-f> | B<-o> I<outspec>]]

B<pstat> {B<-h> | B<--help> | B<-?> | B<--man>}

=head1 DESCRIPTION

The B<pstat> command displays information about jobs.

=head1 OPTIONS

=over 4

=item B<-A>

Display all jobs in the system. 

=item B<-b> I<bank_name>

Diplay only jobs associated with the specified bank.

=item B<-c> I<constraint_list>

Diplay only jobs that are running, or have the potential to run, on hosts that match the given constraint list.

=item B<-D>

Diplay only delayed jobs.

=item B<-f>

Displays the full information for each selected job in a multi-line format.

=item B<-H>

Do not print a header on the output.

=item B<-h | --help | -?>

Display a brief help message

=item B<-L>

Prints long bank and user names without truncation.

=item B<-m> I<host_name>

Display only jobs that are running, or have the potential to run, on the specified machine.

=item B<--man>

Display full documentation

=item B<-n> I<job_list>

Displays only jobs listed in the specified job list where I<job_list> is a comma-separated list of job ids of the form I<job_id>[,I<job_id>...].

=item B<-o> I<outspec>

List only the fields specified by outspec.
This option may not be used in conjuction with
the -f or -v options. outspec may specify
multiple fields, comma separated. Valid field
specifications include: aging_time, bank,
batchid, cl, constraint, cpn, depend,
ecomptime, earliest_start, exehost, highwater,
jid, maxcputime, maxmem, maxphyss, maxrss,
maxruntime, memint, memsize, name, nodes,
preempted_by, priority, runtime, sid, status,
stoptime, submitted, tasks, timecharged,
timeleft, used, user, and vmemint. If no
output format specification is supplied (no
-f, -o, or -v options), the environment
variable PSTAT_CONFIG may specify output
format (e.g., "setenv PSTAT_CONFIG
name,user"). One additional possible value for
outspec is "default", which means to use the
pstat default output format. Field
specifications may be abbreviated. Fields
specified by the -o option or via the
PSTAT_CONFIG environment variable will be
listed in the order specified.


=item B<-R>

Display only running jobs.

=item B<-T>

Display only completed or removed jobs.

=item B<-u> I<user_name>

Display only jobs owned by the specified user.

=item B<-v>

In addition to default fields, aging time (AGING_TIME), maximum cpu time (MAXCPUTIME), the sum of the last measured sizes of all processes in the job (MEMSIZE), dependency (DEPEND), time used per task (USED) and the earliest start time (EARLIEST_START) are displayed.

=back

=head1 JOB STATUS

Supported status include RUN, ELIG, HELD, SUSPENDED, STAGING, DEFERRED, COMPLETE, and REMOVED.

=head1 JOB CLASS (CL)

Possible values for "CL" (class) are:

=over 4

=item B<D> Delayed

=item B<N> Normal

=item B<S> Standby

=item B<X> Expedited

=back

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut

