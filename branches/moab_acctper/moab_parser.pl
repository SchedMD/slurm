#!/usr/bin/perl -w

############################################################################
# Name: moab_parser.pl
# Purpose: To convert MOAB statistics into LCRM statics
#
# Usage: On most systems just run moab_parser.pl which will make a file 
#        that will be scped over to the host specified in var $db_host, 
#        the directory on that host var $db_dir, and the user on the host 
#        $db_user.  
#        The program 'mdiag', 'showq' and 'scp' need to be in the path for 
#        this to run correctly.
#
# Note: This will not handle suspended time correctly as of yet
#       (Only for idle time calculations).   
#
############################################################################
# Copyright (C) 1993-2006 The Regents of the University of California.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Danny Auble <da@llnl.gov>
# UCRL-CODE-155981.
# 
# This file is part of LCRM (Livermore Computing Resource Management).
# For details, see <http://www.llnl.gov/lcrm>.
#  
# LCRM is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
# 
# LCRM is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
# 
# You should have received a copy of the GNU General Public License along
# with LCRM; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
############################################################################


use XML::Simple;
use Time::Local;
use Time::localtime;
use Data::Dumper;

#define variables for this info to be sent to
$db_host = "norman";
$db_dir = "/tmp";
$db_user = "lcrm";

$conhost = `hostname -s`;
$conhost =~ s/\n//g;

$diff = 3600;
$tm = localtime;
$endtime = timelocal(0, 0, $tm->hour, $tm->mday, $tm->mon, $tm->year);
$starttime = $endtime - ($diff-1);

$time_in_mins = sprintf("%.4d.%.2d.%.2d.%.2d.%.2d",
			($tm->year + 1900) , ($tm->mon + 1) , $tm->mday, 
			$tm->hour , $tm->min);

$file = "lcrm_" . $conhost . "_" . $time_in_mins;

$db_file = "$db_dir/$file";
$send_file = "/tmp/$file";

open FILE , ">$send_file";

# $tm = localtime($starttime);
# printf("Start: %02d:%02d:%02d-%04d/%02d/%02d\n",
#     $tm->hour, $tm->min, $tm->sec, $tm->year+1900,
#     $tm->mon+1, $tm->mday);
# $tm = localtime($endtime);
# printf("End: %02d:%02d:%02d-%04d/%02d/%02d\n",
#     $tm->hour, $tm->min, $tm->sec, $tm->year+1900,
#     $tm->mon+1, $tm->mday);

#Get the partitions and there proc count
$output = `mdiag -t --format=xml`;
$xs1 = XML::Simple->new();
$doc = $xs1->XMLin($output);

%parts = ();
foreach $par (@{$doc->{par}}) {
	# we don't want the full part here ("ALL")
	if(!$par->{'PROC.cfg'} || ($par->{ID} eq "ALL")) {
		next;
	}
	$parts{$par->{ID}} = $par->{'PROC.cfg'} * $diff;
}

# Get the completed jobs and report them
$output = `showq -c --format=xml`;
$xs1 = XML::Simple->new();
$doc = $xs1->XMLin($output);
#print Dumper($doc);

foreach $queue (@{$doc->{queue}}) {
	if(!$queue->{count}) {
		next;
	}
	for($i=0; $i < $queue->{count}; $i++) {
		if($queue->{count} == 1) {
			$job = $queue->{job};
		} else {
			$job = $queue->{job}->[$i];
		}

		if(!$job->{AWDuration}) {
			next;
		}
		$start = $job->{StartTime};

		if($start > $endtime) {
			next;
		}	
		
		# get just this hours info
		if($start < $starttime) {
			$start = $starttime;
		}
		
		$timestamp = $job->{AWDuration} + $job->{SuspendDuration}
		+ $start;		
		
		$end = $timestamp;
		if($end > $endtime) {
			$end = $endtime;
		}
		$running_time = ($end - $start);
		
		if($job->{PAL}) {
			#print "got $end - $start = $running_time $job->{AWDuration} extra to subtract for job $job->{JobID}\n";
			$parts{$job->{PAL}} -= $running_time;
		}


		if(($timestamp > $endtime) || ($timestamp < $starttime)) {
			next;
		}
		
		$info = "";
		#$info .= "$job->{JobID} "; #JOBID
		if($job->{PAL}) {
			$info .= "$job->{PAL} "; #host
			$info .= "$job->{PAL}_part "; #partition
			# get just this hours info
			# if($job->{StartTime} < $starttime) {
# 				$extra = $job->{AWDuration} 
# 				- ($starttime - $job->{StartTime});
# 			} else {
# 				$extra = $job->{AWDuration};
# 			}
# 			$parts{$job->{PAL}} -= $extra;
			

		} else {
			$info .= "- - "; #host partition
		}
		
		if($job->{Class}) {
			$info .= "$job->{Class} "; #pool
		} else {
			$info .= "- "; #pool
		}
		
		$info .= "$timestamp "; #timestamp
		
		if($job->{User}) {
			$info .= "$job->{User} "; #user
		} else {
			$info .= "- "; #user
		}
		
		if($job->{Account}) {
			$info .= "$job->{Account} "; #bank
		} else {
			$info .= "- "; #bank
		}
		
		$info .= "MOAB "; #type
		
		$ucpus = $job->{AWDuration} * $job->{ReqProcs};
		$info .= "$ucpus "; #ucpu
		
		$info .= "0 0 0\n"; #icpu memint vmemint
		print FILE $info;
	}
}

# Get the running jobs and subtract the used time for the idle time
$output = `showq -r --format=xml`;
$xs1 = XML::Simple->new();
$doc = $xs1->XMLin($output);
#print Dumper($doc);
$query_time = $doc->{cluster}->{time};

for($i=0; $i < $doc->{queue}->{count}; $i++) {
	if($doc->{queue}->{count} == 1) {
		$job = $doc->{queue}->{job};
	} else {
		$job = $doc->{queue}->{job}->[$i];
	}
	#print "here with job $i " . $job->{JobID} . "\n";
	
	# make it so we only get this hours info
	$start = $job->{StartTime};
	#print "$start $endtime\n";
	
	if($start > $endtime) {
		next;
	}
	if($start < $starttime) {
		$start = $starttime;
	}
	$end = $query_time;
	if($end > $endtime) {
		$end = $endtime;
	}
	
	$running_time = ($end - $start); 
	if($job->{PAL}) {
		$parts{$job->{PAL}} -= $running_time;
	}
}

while (($part, $icpu) = each(%parts)) {
	$info = "$part "; #host
	$info = "$part "; #partition
	$info .= "no_pool "; #pool
	$info .= "$endtime "; #timestamp
	$info .= "(NULL) "; #user
	$info .= "noreport "; #bank
	$info .= "IDLE "; #type
	$info .= "$icpu "; #ucpu
	$info .= "0 0 0\n"; #icpu memint vmemint
	print FILE $info;
}
close FILE;
system("more $send_file");

#system("scp $send_file $db_user@$db_host:$db_file 2>/dev/null");

system("rm -f $send_file");
