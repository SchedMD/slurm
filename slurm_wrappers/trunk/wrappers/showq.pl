#! /usr/bin/perl -w
#
# A utility to expand the showq information.
#
# The coding may be awkward in palces, as I have attempted
# to retain the same information as the showq utility, in
# addition to all the other information that I provide.
# This makes it almost essential to parse and disply showq
# output one line at a time.
#
#
# A future rewrite will undoubtably handle this differently.
#
# Last Update: 2010-07-27
#
# Copyright (C) 2010 Lawrence Livermore National Security.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Philip D. Eckert <eckert2@llnl.gov>
# CODE-OCEC-09-009. All rights reserved.
#

#
# For debugging.
#
#use lib "/var/opt/slurm_dawn/lib64/perl5/site_perl/5.8.8/x86_64-linux-thread-multi";
use Slurm ':all';
use Slurmdb ':all'; # needed for getting the correct cluster dims





#
# For generating man pages.
#
BEGIN {
    # Just dump the man page in *roff format and exit if --roff specified.
    foreach my $arg (@ARGV) {
	if ($arg eq "--") {
	    last;
	} elsif ($arg eq "--roff") {
	    use Pod::Man;
	    my $parser = Pod::Man->new (section => 1);
	    $parser->parse_from_file($0, \*STDOUT);
	    exit 0;
	}
    }
}

use File::Basename;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use autouse 'Pod::Usage' => qw(pod2usage);
use Time::Local;
use Term::ANSIColor;
use Storable qw/dclone/;
use strict;

#
# Defining  arguments for GetOpt.
#
my (
	$account_arg,	$active_arg,	$blocked_arg,	$class_arg,
	$completed_arg, $eligible_arg,	$help,		$group_arg,
	$hp_arg,	$job_arg,	$man,		$master_arg,	
	$outputFormat,	$partition_arg,	$proc_type_arg,	$qos_arg,
	$status_arg,	$user_arg,	$version_arg, 	$head_arg
);

#
# Slurm output will be placed into these arrays for parsing.
#
my (@ACTIVE, @ELIG, @BLOCKED, @COMP);

#
# Not guaranteed to work on all systems.
#
my $rev_on  = color("reverse");
my $rev_off = color("clear");

#
# Other global variables.
#
my ($total, $opt) = (0,0);

my $bglflag = 1  if (`scontrol show config | grep -i bluegene`);

#
# Each entry in this hash table must have a subhash.  The possible keys are:
#   short - short format, falls back to length of key name
#   sameas - SPECIAL CASE, when first parsed, this spec will be
#              replaced by the spec specified in the 'sameas' subkey
#
# (borrowed from the pstat wrapper, I believe credit goes to Chris Morrone
#  for this one.)
#
my $spectable = {
     'account'       =>	{'short'  => '%-10.10s' },
     'aging'         =>	{'short'  => '%12.12s'  },
     'ccode'         =>	{'short'  => '%6.6s'    },
     'class'         =>	{'short'  => '%-10.10s' },
     'completedtime' =>	{'sameas' => 'starttime'},
     'dependency'    =>	{'short'  => '%10.10s'  },
     'exehost'       =>	{'short'  => '%-10.10s' },
     'group'         =>	{'short'  => '%-8.8s'   },
     'jobid'	     =>	{'short'  => '%-8.8s'   },
     'jobname'       =>	{'short'  => '%-15.15s' },
     'nodes'         =>	{'short'  => '%6s'      },
     'priority'      =>	{'short'  => '%10.10s'  },
     'procs'         =>	{'short'  => '%6s'      },
     'qos'           =>	{'short'  => '%-9.9s'   },
     'queuetime'     =>	{'sameas' => 'starttime'},
     'username'      =>	{'short'  => '%-10.10s' },
     'remaining'     =>	{'short'  => '%12.12s'  },
     'starttime'     =>	{'short'  => '%20.20s'  },
     'state'         =>	{'short'  => '%-11.11s' },
     'walltime'      =>	{'sameas' => 'remaining'},
     'wclimit'       =>	{'sameas' => 'remaining'},
};

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);

#
# Get options.
#
GetOpts();

#
# If asking for the version, show it and exit.
#
if ($version_arg) {
	system("sinfo -V");
	exit(0);
}

#
# Clean up output format string and check to see if the
# environment option is setup, otherwise set the Default
# options.
#
if ($outputFormat || exists $ENV{SHOWQ_CONFIG}) {
	$outputFormat = $ENV{SHOWQ_CONFIG} unless $outputFormat;
	$outputFormat =~ s/^\s+//;    # Remove leading whitespace
	$outputFormat =~ s/\s+$//;    # Remove trailing whitespace
	if ($proc_type_arg) { $master_arg = 1 };
} else {
	if ($completed_arg) {
		if ($proc_type_arg) {
			$master_arg = 1;
			$outputFormat ="JOBID USERNAME ACCOUNT QOS CLASS EXEHOST STATE CCODE PROCS WCLIMIT QUEUETIME";
		} else {
			$outputFormat ="JOBID USERNAME ACCOUNT QOS CLASS EXEHOST STATE CCODE NODES WCLIMIT QUEUETIME";
		}
	} elsif ($proc_type_arg) {
		$master_arg = 1;
		$outputFormat ="JOBID USERNAME ACCOUNT QOS CLASS EXEHOST STATE PROC WCLIMIT QUEUETIME";
	} else {
		$outputFormat ="JOBID USERNAME ACCOUNT QOS CLASS EXEHOST STATE NODE WCLIMIT QUEUETIME";
	}
}

#
# Validate format options (if any).
#
my @outspec = ();
foreach my $spec (split /\s*[, ]\s*/, $outputFormat) {
	$spec = lc $spec;
	if (my $full = abbreviation($spectable, $spec)) { $spec = $full; }
	die("Unrecognized outspec: $spec\n") unless exists $spectable->{$spec};
	if (exists $spectable->{$spec}->{'sameas'}) {
		push @outspec, $spectable->{$spec}->{'sameas'};
	} else {
		push @outspec, $spec;
	}
}

#
# Get showq output.
#

if ($completed_arg) {
	getcompletedjobs();
} else {
	getslurmdata();
}


my @queue = ( "active", "eligible", "blocked", "completed" );

@ACTIVE = sort { $a->{remaining} <=> $b->{remaining} } @ACTIVE;
@ELIG = sort { $b->{priority} <=> $a->{priority} } @ELIG;
#
# Loop through the queues (active, eligible and blocked, or completed).
#
my @Ars =  ( \@ACTIVE, \@ELIG, \@BLOCKED, \@COMP);


#
# Depending on the option, we wil either want to look through
# just the first three arrays, or just the last.
#
my ($min, $max);
if ($completed_arg) {
	$min = 3;
	$max = 3;
} else {
	$min = 0;
	$max = 2;
}

my $Now = time();
foreach my $ct ($min .. $max) {
	my $status    = $queue[$ct];
	my $ar        = $Ars[$ct];

#
#	Depending on the option, we may want to skip some queues.
#
	next if ((($opt & 4) != 4) && ($status eq "blocked"));
	next if ((($opt & 2) != 2) && ($status eq "eligible"));
	next if ((($opt & 1) != 1) && ($status eq "active"));

	my $plural = "s";
	my $purgeinfo = "";
	my $jobcount = 0;
	my $info1= "";
	my $info2= "";

#
#	Set the headings, depending on the status.
#
	my $time = "  WCLIMIT";
	my $timeheading = "QUEUETIME";
	if ($status eq "active") {
		$time = "REMAINING";
		$timeheading = "STARTTIME";
		($info1,$info2) = getnodeinfo();
	} elsif ($status eq "completed") {
		$time = "WALLTIME";
		$timeheading = "COMPLETIONTIME";
	}

#
#	Heading.
#
#	(Special case "remaining" and "startime", this heading value will
#	 vary.)
#
	my $format = '';
	my $line = '';
	foreach  my $spec (@outspec) {
		$format = '';
		$format = $spectable->{$spec}->{'short'} unless $format;
		$format = '%s' unless $format; # unsupported spec
		if (lc $spec eq "remaining") {
			$line .= sprintf("%12.12s ",$time);
		} elsif (lc $spec eq "starttime") {
			$line .= sprintf("%20.20s ",$timeheading);
		} else {
			$line .= sprintf "$format ", uc $spec;
		}
	}
	printf("\n\n$status jobs------------------------\n") if (!$head_arg);
	printf("$line\n\n") if (!$head_arg);

#
#	Loop through all jobs.
#
	foreach my $job (@{$ar}) {
		my $value = {};
		my $jid         = $job->{jobid};
		my $user        = $job->{user};
		my $group       = $job->{group};
		my $jobname     = $job->{jobname};
		my $account     = $job->{account};
		my $reason      = $job->{reason};
		my $state       = $job->{state};
		my $exehost     = $job->{host};
		my $qos         = $job->{qos};
		my $compcode    = $job->{ccode};
		my $depend      = $job->{depend};
		my $startpri    = $job->{priority};
		my $nodes       = $job->{nodes};
		if ($bglflag) {
			if (($nodes > 1024) && ($nodes % 1024 == 0)) {
				($nodes /= 1024) .= "k";
			}
		}
		my $procs       = $job->{procs};
		my $tpn         = $job->{tpn};
		my $class       = $job->{class};
		my $eligtime    = $job->{etime};
		my $subtime     = $job->{subtime};
		my $starttime   = $job->{starttime};
		my $comptime    = $job->{comptime};
		my $masternode  = $job->{master} || "N/A";
		my $duration    = $job->{duration};
		my $remaining   = $job->{remaining};
		my $status      = $job->{status};
#
#		For options that require matching.
#
		next if (($account_arg)   && ($account_arg   ne $account));
		next if (($class_arg)     && ($class_arg     ne $class));
		next if (($group_arg)     && ($group_arg     ne $group));
		next if (($job_arg)       && ($job_arg       ne $jid));
		next if (($qos_arg)       && ($qos_arg       ne $qos));
		next if (($user_arg)      && ($user_arg      ne $user));

#
#		Master node needs to pruned from all node data for job.
#
		$masternode =~ s/\[//; 
		$masternode =~ s/[,|\-| ].*//g; 


		$value->{'jobid'}         = $jid;
		$value->{'username'}      = $user;
		$value->{'account'}       = $account;
		$value->{'aging'}         = hhmmss($Now-$subtime);
		$value->{'group'}         = $group;
		$value->{'qos'}           = $qos;
		$value->{'nodes'}         = $nodes;
		$value->{'procs'}         = $procs;
		$value->{'priority'}      = $startpri;
		$value->{'class'}         = $class           if (defined $class);
		$value->{'ccode'}         = $compcode        if (defined $compcode);
		$value->{'jobname'}       = $jobname         if (defined $jobname);
		$value->{'exehost'}       = $exehost         if (defined $exehost);
		$value->{'exehost'}       = $masternode      if ($master_arg && defined $masternode && ($masternode ne "N/A"));

		if ($status eq "active" || $status eq "eligible") {
			my  $qtime = $starttime - $subtime;
			$qtime = $Now - $subtime if ($status eq "eligible");
			$qtime = 1 if ($qtime < 1);
		}
#
#		Total of all jobs queried.
#
		$total += 1;
		$jobcount += 1;

#
#		Some time value checks are based on whether active or completed.
#
		if ($status eq "blocked" || $status eq "eligible") {
			$remaining = $duration;
		} elsif ($status eq "active") {
			$subtime = $starttime;
		} else {
			$remaining = $duration;
			$subtime = $comptime;
		}
		$value->{'remaining'} = hhmmss($remaining);
		if ($completed_arg) {
			$value->{'starttime'} = $subtime;
		} else {
			$value->{'starttime'} = toTime($subtime);
		}

#
#		Remap some of the states to be more recognizable to users.
#
		$state = "Running" if ($state =~ /RUN/);
		if ($state eq "PENDING" && $status eq "eligible") {
			if ($reason eq "Resource") {
				$state = "wcpu";
			} else {
				$state = "Wprio";
			}
		}

		if ($status eq "blocked") {
			$state = $reason;
			$state = "TooLong"	if ($reason =~ /PartitionTimeLimit/);
			$state = "Depend"	if ($reason =~ /Depend/);
			$state = "UserHold"	if ($reason =~ /JobHeldUser/);
			$state = "SystemHold"	if ($reason =~ /JobHeldAdmin/);
	        	$state = "Wait"		if ($reason =~ /BeginTime/);
		}
		$value->{'state'} 	  = $state;
			

		next if (($status_arg) && (lc $status_arg ne lc $state));

		my $format;
		my $val;
		my $line = '';
		foreach my $spec (@outspec) {
			$format = '';
			$format = $spectable->{$spec}->{'short'} unless $format;
			$format = ('%-' . (length $spec) . 's') unless $format;
			if (exists $value->{$spec}) {
				$val = $value->{$spec}
			} else {
				$val = 'N/A';
			}
			$line .= sprintf "$format ", $val;
		}
		$line =~ s/\s+$//;      # Remove trailing whitespace

#
#		Highlight (inverse video) whatever pattern chosen.
#
		if (defined $hp_arg) {
			my $temp = $rev_on.$hp_arg.$rev_off;
			$line =~ s/$hp_arg/$temp/g;
		}
		printf("$line\n");
	}

#
#	Print out jobcount for each category.
#
	$plural = "" if ($jobcount == 1);
	my $part1 = sprintf("$jobcount $status job%s",$plural);

	if ($status eq "completed") {
		printf("\n$jobcount $status job%s   $purgeinfo\n",$plural) if (!$head_arg);
	} else {
		printf("\n%-20.20s %s\n", $part1, $info1) if (!$head_arg);
	}

	printf("                     %s\n",$info2) if (defined $info2 && !$head_arg);
}

#
# Print totals and any additonal info.
#
printf("\nTotal jobs:   %d\n\n",$total) if (!$head_arg);

exit;

#
# Use sinfo to get proc/node usage.
#
sub getnodeinfo
{
	my @Info = split(/[\/| ]/, `sinfo -o "%F %C" -h`);
	my @Info2 = @Info;

	foreach my $i (0 .. 3) {
		if ($Info[$i] =~ /K/) {
			$Info[$i] =~s/K//;
			$Info[$i] *= 1024;
		}
	}

	chomp @Info2;
	my $average = ($Info[0] / $Info[3]) * 100.0;
	my $procinfo = sprintf("%8s of %-s processors in use by local jobs (%2.2f%%)",
		$Info2[4], $Info2[7], $average);
	my $nodeinfo = sprintf("%8s of %-s nodes active (%2.2f%%)\n",
		$Info2[0], $Info2[3], $average);

	return ($procinfo, $nodeinfo);
}

#
# Get the needed data from the slurm job api.
#
sub getslurmdata
{
	use Slurm ':all';

	my ($eval_reason, $eval_state);

	my $tmp = `scontrol show config  | grep ClusterName`;;
	my ($host) = ($tmp =~ m/ = (\S+)/);


#
#	Use SLURM perl api to get job info.
#
	my $jobs = Slurm->load_jobs();
	unless($jobs) {
		my $errmsg = Slurm->strerror();
		print "Error loading jobs: $errmsg";
	}

	my $now = time();
	my $jdat;
#
#	Iterate over the jobs
#
	foreach my $job (@{$jobs->{job_array}}) {
#		next if ($job->{job_id} > 1000000); # don't process interactive jobs.
		$jdat->{jobid}       = $job->{job_id};
		$jdat->{user}        = getpwuid($job->{user_id});
		$jdat->{group}       = getpgrp($job->{group_id});
		$jdat->{jobname}     = $job->{name} || 'N/A';
		$jdat->{account}     = $job->{account} || '';

#
#		Have to use eval to avoid structure differences in SLURM perl api.
#
if ($sversion =~ /2.2/) {
		$eval_reason = 'Slurm->job_reason_string($job->{state_reason})';
		$eval_state  = 'Slurm->job_state_string($job->{job_state})';
} else {
		$eval_reason = 'Slurm::job_reason_string($job->{state_reason})';
		$eval_state  = 'Slurm::job_state_string($job->{job_state})';
}
		$jdat->{reason} = eval $eval_reason || "N/A";
		$jdat->{state}  = eval $eval_state || "N/A";

		$jdat->{host}        = $host;
		$jdat->{qos}         = $job->{qos} || 'N/A';
		$jdat->{ccode}       = $job->{exit_code} >> 8;
		$jdat->{depend}      = $job->{dependency} || 'N/A';
		$jdat->{priority}    = $job->{priority} || 0;
		$jdat->{master}      = $job->{nodes};
		$jdat->{nodes}       = $job->{num_nodes};
		$jdat->{procs}       = $job->{num_cpus};
		$jdat->{tpn}         = $job->{ntasks_per_node} || 'N/A';
		$jdat->{class}       = $job->{partition};
		$jdat->{eligtime}    = $job->{eligible_time};
		$jdat->{subtime}     = $job->{submit_time};
		$jdat->{starttime}   = $job->{start_time};
		$jdat->{comptime}    = $job->{end_time};
		$jdat->{features}    = $job->{features} || 'N/A';
		$jdat->{duration}    = $job->{time_limit} * 60;	# slurm states in minutes, Moab seconds.

		my $jdat2 = dclone($jdat);

#
#		In order to avoid too many changes in how showq.pl handles the slurm data
#		compared to moab, I am arraning the output into separate arrays similar to
# 		showq xml data is arranges....there is a method to my madness.
#	
		if ($jdat->{state} =~ /COMP/ ||
		    $jdat->{state} =~ /FAIL/ ||
		    $jdat->{state} =~ /TIMEOUT/ ||
		    $jdat->{state} =~/CANC/) {
			$jdat2->{status} = "completed";
			push @COMP, $jdat2;
		} elsif ($jdat2->{state} eq "RUNNING") {
			$jdat2->{remaining} = $jdat->{duration} - ($now - $jdat->{starttime});
			$jdat2->{status} = "active";
			push @ACTIVE, $jdat2;
		} elsif ($jdat->{state} eq "PENDING" &&
				($jdat->{reason} eq "None" ||
				 $jdat->{reason} eq "Resource" ||
				 $jdat->{reason} eq "Priority")) {
			$jdat2->{status} = "eligible";
			push @ELIG, $jdat2;
		} else {
			$jdat2->{status} = "blocked";
			push @BLOCKED, $jdat2;
		}
	}

	return;
}


#
# Get the needed data from the slurm job api.
#
sub getcompletedjobs
{
	use Slurm ':all';

	my $now = time();
	my $jdat;
#
#	Use SLURM perl api to get job info.
#
	my $cmd = "sacct -X -p ";
	my $fmt = "-o 'JobID,User,Group,JobName,Account,State,Cluster,QOS,ExitCode,Priority,NodeList,NNodes,NTasks,Partition,Start,End,Elapsed' ";
	$cmd .= $fmt;

	$cmd .= " -A $account_arg" if ($account_arg);
	$cmd .= " -r $class_arg"   if ($class_arg);
	$cmd .= " -g $group_arg"   if ($group_arg);
	$cmd .= " -j $job_arg"     if ($job_arg);
	$cmd .= " -u $user_arg"    if ($user_arg);
	$cmd .= " -s $status_arg"  if ($status_arg);
	$cmd .= " -a"              if (!$user_arg);
	$cmd .= " -S 06/21/10";

	my @out = `$cmd`;
	
#
#	Iterate over the jobs
#
	foreach my $line (@out) {
		my @job =  split (/\|/, $line);
		$jdat->{jobid}       = $job[0];
		next if ($jdat->{jobid} > 1000000); # don't process interactive jobs.
		$jdat->{user}        = $job[1];
		$jdat->{group}       = $job[2];
		$jdat->{jobname}     = $job[3] || 'N/A';
		$jdat->{account}     = $job[4];
		$jdat->{state}       = $job[5];
		$jdat->{state}       =~ s/ b.*//;
		$jdat->{host}        = $job[6];
		$jdat->{qos}         = $job[7] || 'N/A';
		$jdat->{ccode}       = $job[8];
		$jdat->{priority}    = $job[9];
		$jdat->{master}      = $job[10];
		$jdat->{nodes}       = $job[11];
		$jdat->{procs}       = $job[12];
		$jdat->{class}       = $job[13];
		$jdat->{starttime}   = $job[14];
		$jdat->{comptime}    = $job[15];
		$jdat->{duration}    = $job[16];	# slurm states in minutes, Moab seconds.
		$jdat->{status}      = "completed";

		my $jdat2 = dclone($jdat);

#
#		In order to avoid too many changes in how showq.pl handles the slurm data
#		compared to moab, I am arraning the output into separate arrays similar to
# 		showq xml data is arranges....there is a method to my madness.
#	
		push @COMP, $jdat2;
	}

	return;
}


#
# slurm states and reaosn - for reference.
#
#sub job_state_string {
#        my $st = shift;
#        return "COMPLETING" if $st & JOB_COMPLETING;
#        return "PENDING" if $st == JOB_PENDING;
#        return "RUNNING"  if $st == JOB_RUNNING;
#        return "SUSPENDED"  if $st == JOB_SUSPENDED;
#        return "COMPLETED" if $st == JOB_COMPLETE;
#        return "CANCELLED" if $st == JOB_CANCELLED;
#        return "FAILED"  if $st == JOB_FAILED;
#        return "TIMEOUT" if $st == JOB_TIMEOUT;
#        return "NODE_FAIL" if$st == JOB_NODE_FAIL;
#        return "?";
#}

#sub job_reason_string {
#        my $r = shift;
#        return "None" if $r == WAIT_NO_REASON;
#        return "Priority" if $r == WAIT_PRIORITY;
#        return "Dependency" if $r == WAIT_DEPENDENCY;
#        return "Resource" if $r == WAIT_RESOURCES;
#        return "PartitionNodeLimit" if $r == WAIT_PART_NODE_LIMIT;
#        return "PartitionTimeLimit" if $r == WAIT_PART_TIME_LIMIT;
#        return "PartitionDown" if $r == WAIT_PART_STATE;
#        return "JobHeld" if $r == WAIT_HELD;
#        return "BeginTime" if $r == WAIT_TIME;
#        return "PartitionDown" if $r == FAIL_DOWN_PARTITION;
#        return "NodeDown" if $r == FAIL_DOWN_NODE;
#        return "BadConstraints" if $r == FAIL_BAD_CONSTRAINTS;
#        return "SystemFailure" if $r == FAIL_SYSTEM;
#        return "JobLaunchFailure" if $r == FAIL_LAUNCH;
#        return "NonZeroExitCode" if $r == FAIL_EXIT_CODE;
#        return "TimeLimit" if $r == FAIL_TIMEOUT;
#        return "InactiveLimit" if $r == FAIL_INACTIVE_LIMIT;
#        return "?";
#}


#
# Convert epochtime to readable format.
#
sub toTime
{
	my ($epochTime) = @_;

	my @months   = qw(Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec);
	my @weekdays = qw(Sun Mon Tue Wed Thu Fri Sat);
	my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday, $isdst) =
		localtime($epochTime);

	my $toTime = sprintf( "%3s %3s %2d %02d:%02d:%02d",
        $weekdays[$wday], $months[$mon], $mday, $hour, $min, $sec);

	return $toTime;
}

#
# Convert seconds to dd:hh:mm:ss format.
#
sub hhmmss
{
	my ($hhmmss) = @_;

#
#	For right now, any time longer than 99 day,
#	just indicate that.
#
	my $year = 300 * 24 * 3600;
	return "UNLIMITED" if ($hhmmss >= $year);

#
#	Convert hhmmss to duration in seconds
#
	$hhmmss = 0 unless $hhmmss;
	$hhmmss =~ /(?:(\d+):)?(?:(\d+):)?([\d\.]+)/;
	my ($hh, $mm, $ss) = ($1 || 0, $2 || 0, $3 || 0);

	my $duration = int($ss);
	$duration += $mm * 60;
	$duration += $hh * 3600;

#
#	Convert duration in seconds to ddhhmmss
#
	my $days = int($duration / 86400);
	$duration = $duration - $days * 86400;
	my $hours = int($duration / 3600);
	$duration = $duration - $hours * 3600;
	my $minutes = int($duration / 60);
	$duration = $duration - $minutes * 60;
	my $seconds = $duration;
	my $hhmmss2 = 0;
	if ($days != 0) {
		$hhmmss2 = sprintf('%d:%02d:%02d:%02d', $days, $hours, $minutes, $seconds);
	} else {
		$hhmmss2 = sprintf('%2d:%02d:%02d', $hours, $minutes, $seconds);
		if ($hours == 0) { $hhmmss2 |= "00:00:00"; }
	}

	return $hhmmss2;
}

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
	die("\"$abbrev\" is a non-unique abbreviation.\n") if ($matches > 1);

	return($match);
}


sub GetOpts
{
	GetOptions(
		'help|h|?'   => \$help,
		'C=s'        => \$class_arg,
		'H'          => \$head_arg,
		'M'          => \$master_arg,
		'P'          => \$proc_type_arg,
		'a=s'        => \$account_arg,
		'b'          => \$blocked_arg,
		'g=s'        => \$group_arg,
		'o=s'        => \$outputFormat,
		'cc'         => sub { printf("$rev_off"); exit;},
		'c'          => \$completed_arg,
		'hp=s'       => \$hp_arg,
		'i'          => \$eligible_arg,
		'man'        => \$man,
		'j=s'        => \$job_arg,
		'p=s'        => \$partition_arg,
		'q=s'        => \$qos_arg,
		'r'          => \$active_arg,
		's=s'        => \$status_arg,
		'u=s'        => \$user_arg,
		'v'          => \$help,
		'version'    => \$version_arg,
	) or usage();

#
#	Display man page if requested.
#
	show_man() if ($man);

#
#	If help, exit afterwards.
#
	usage() if ($help);

        $opt |= 4 if ($blocked_arg);
        $opt |= 2 if ($eligible_arg);
        $opt |= 1 if ($active_arg);
#
#	Completed is handled differently.
#
        if ($completed_arg && $opt) {
			printf("\n  These options may not be used together.\n\n");
			exit(-1);
	} elsif (!$opt) { $opt = 7; } 

	return;
}

sub show_man
{

	if ($< == 0) {    # Cannot invoke perldoc as root
		my $id = eval { getpwnam("nobody") };
		$id = eval { getpwnam("nouser") } unless defined $id;
		$id = -2                          unless defined $id;
		$<  = $id;
		printf("\n You can not do this as root!\n\n");
		exit;
	}
	$> = $<;                         # Disengage setuid
	$ENV{PATH} = "/bin:/usr/bin";    # Untaint PATH
	delete @ENV{'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};
	if ($0 =~ /^([-\/\w\.]+)$/) { $0 = $1; }    # Untaint $0
	else { die "Illegal characters were found in \$0 ($0)\n"; }
	pod2usage(-exitstatus => 0, -verbose => 2);

}

#
# Report the usage in a manner similar to that of
# showq, including the additional options.
#
sub usage
{
	my $base = basename($0);

	printf("\
Usage: $base [FLAGS]


Options.

  -P                 // PROCESSOR BASED, AND USE MASTER NODE NAME WHEN POSSIBLE
  -H                 // NO COLUMN HEADINGS
  -M                 // SHOW MASTER NODE NAME WHEN POSSIBLE
  -C <class name>    // CLASS
  -a <account name>  // ACCOUNT
  -b                 // BLOCKED JOBS
  -c                 // COMPLETED QUEUE
  -h                 // HELP
  -hp <pattern>      // HIGHLIGHT PATTERN (if supported)
  -i                 // IDLE QUEUE (ELIGIBLE)
  -j <job id>        // JOB
  -r                 // ACTIVE QUEUE
  -s <status>        // SHOW JOBS WITH THE NAMED STATUS
  -man               // MAN PAGES
  -o '...'           // FORMATED OUTPUT
  -p <partition name>// PARTITION
  -q <qos name>      // OQS
  -u <user name>     // USER

Jobs under the active heading are sorted by remaining time.

Jobs under the eligible heading are sorted in priority order and
if there is an asterisk after the job id, it has a pre-reservation.

Jobs under the blocked headed are sorted by queue time.

Jobs with a time limit > 300 days will show UNLIMITED.

 (Note: a '-' following the user name indicates failure to get 
  the user's environment at job submission time.)
\n");

	exit;
}


__END__

=head1 NAME

B<showq> - an expanded version of showq.

=head1 SYNOPSIS

B<showq>    [B<-C> I<class>] [B<-H>] [B<-P>] [B<-M>] [B<-a> I<account>] [B<-b>] [B<-c>]
B<>         [B<-hp> I<pattern>] [B<-i>] [B<-j> I<jobid>] [B<-m>] [B<-o> I<outspec>] [B<-p> I<partition>]
B<>         [B<-q> I<qos>] [B<-r>] [B<-s> I<status>] [B<-u> I<username>] [B<-version>]
B<>         [B<-h>] [B<-man>]

=head1 DESCRIPTION

The B<showq> command displays information about Moab jobs.

=head1 OPTIONS

=over 4

=item B<-C> I<class_name>

Display only jobs requesting the class. 

=item B<-H>

Only show jobs without any other fields.

=item B<-P> 

Output is process based, and when applicable, diplay the masternode of the job,
rather than the hostname.

=item B<-M> 

When applicable, diplay the masternode of the job, rather than the hostname.

=item B<-a> I<account_name>

Display only jobs requesting the account. 

=item B<-b>

Diplay only jobs that are blocked.

=item B<-c>

Diplay only completed jobs.

=item B<-h>

Display a brief help message

=item B<-hp> I<pattern>

If supported by your terminal, will display the specified pattern in inverse
video. (Note: if your terminal should get hung up in inverse video mode,
using "showq -cc" should clear it.)

=item B<-i>

Display only eligible (idle) jobs.

=item B<-j> I<job_id>

Display this job.

=item B<-m>

Display only non-running migrated jobs.

=item B<-man>

Display man pages for this utility.

=item B<-o> I<outspec>

List only the fields specified by outspec.
outspec may specify multiple fields, comma separated. Valid field
specifications include:

   account, aging, ccode, class, dependency, effic, exehost, group, jobid, jobname, nodes,
   priority, procs, qos, rsvstarttime, state, username, xfactor, remaining/wclimit/walltime,
   completiontime/starttime/queuetime

Field specifications may be abbreviated. Fields specified by the -o option will be listed in
the order specified.

Selecting any one of the following fields generates the correct
heading and field depending on job category, so they are
equivalent:

   remaining walltime wclimit

As also are:

   starttime completedtime queuetime

Default is:
     -o 'jobid username account qos class exehost state nodes remaining starttime'

The environment variable SHOWQ_CONFIG may specify output format (e.g., "setenv SHOWQ_CONFIG jobid,username").

=item B<-p> I<partition_name>

Displays only jobs listed in the specified partition.

=item B<-q> I<qos_name>

Displays only jobs listed in the specified partition.

=item B<-r>

Display only active (running) jobs.

=item B<-s> I<status>

Display only jobs that have this status.

=item B<-u> I<user_name>

Display only jobs owned by the specified user.

=item B<-version>

Display the version number.

=back

=head1 FIELDS

The fields are as follows:

   ACCOUNT		Account usage is charged to.

   CCODE		Completion code for batch job.

   CLASS		Class (pool, ie. pbatch) job has selected.

   DEPENDENCY		The job that this one is waiting on.

   EFFIC		CPU efficiency of job.

   EXEHOST		Host of exectution.

   GROUP		Group associated with user.

   JOBID		Job identifier.

   JOBNAME		Job name.

   NODES		Number of nodes selected.

   PRIORITY		Moab priority of job.

   PROCS		Number of proccessors selected.

   QOS			Quality of Service (defined below.)

   RSVSTARTTIME		Highest priority job(s) projected startime.

   STATE		Status of job (possibilities listed below.)

   USERNAME		User owning job.

   XFACTOR		Current expansion factor of job, where
			XFactor = (QueueTime + WallClockLimit) / WallClockLimit

   REMAINING/WCLIMIT/WALLTIME
			For active jobs, the time the job has until it has
			reached its wall clock limit or for idle/blocked/
			completed jobs, the amount of time requested by the job.
			Time specified in [DD:]HH:MM:SS notation.

   COMPLETIONTIME/STARTTIME/ QUEUETIME
			Time job started running.

=head1 POSSIBLE JOB STATES

Supported status include:

BatchHold
Canceleing
Completed
Deferred
Depend
MaxJobs
Migrated
Multiple
NoAcct
Nodes>Max
Procs>Max
Removed
Running
Starting
SystemHold
TooLong
UserHold
Wait
Wcpu
Wprio

=head1 QOS DEFINITIONS

Current possible values for "QOS"are:

=over 4

Normal     - default

Standby    - preemptable

Expedited  - high priority, exempted from limits

=back


=head1 ADDITIONAL NOTES

Jobs under the active heading are sorted by remaining time.

Jobs under the eligible heading are sorted in priority order and
if there is an asterisk after the job id, it has a pre-reservation.

Jobs under the blocked headed are sorted by queue time.

Jobs with a time limit > 300 days will show UNLIMITED.

a '-' following the user name indicates failure to get 
the user's environment at job submission time.)

If a job shows a "Multiple" status, use 'checkjob -v jobid'
to determine its issues.

=head1 AUTHOR NOTES

I have taken some liberties with the status, trying to reflect a status that is more
familiar to our users. In addition, this program also executes mdiag to try and get a
little more status information than is provided by showq alone. There are requests
in to CRI to provide more information than is currently available, and as that is
provided this utility will be updated.

=head1 REPORTING BUGS

Report problems to Phil Eckert
eckert2@llnl.gov.

=cut
