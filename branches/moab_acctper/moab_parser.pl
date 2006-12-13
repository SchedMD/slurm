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
use POSIX("strftime");



#define variables for this info to be sent to
$db_host = "ramon";
$db_dir = "/lcrm_root/data/db_data";
$db_user = "lrm";

$conhost = `hostname -s`;
$conhost =~ s/\n//g;

$time_period = 5; #period you want to grab (i.e. last so many minutes)

#get seconds to go from in the last period minus 1 so we don't have overlap
$diff = ($time_period * 60) - 1; 
$tm = localtime; # get the localtime;
$min = $tm->min;
#get the last time period
while(($min%$time_period)) {
	$min--;
}

#get the endtime and the starttime of the period
$endtime = timelocal(0, $min, $tm->hour, $tm->mday, $tm->mon, $tm->year);
$starttime = $endtime - ($diff);

# $time_in_mins = sprintf("%.4d.%.2d.%.2d.%.2d.%.2d.",
# 			($tm->year + 1900) , ($tm->mon + 1) , $tm->mday, 
# 			$tm->hour , $tm->min);

$file_time = strftime("%Y.%m.%d.%H.%M.%Z",
		      0, $min, $tm->hour,
		      $tm->mday, $tm->mon, $tm->year,
		      $tm->wday, $tm->yday, $tm->isdst);

$file = "moab_" . $conhost . "_" . $file_time . ".dat";

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
		$timestamp = $job->{AWDuration} + $job->{SuspendDuration}
		+ $start;		
		
		$end = $timestamp;
		if($start > $endtime || $end < $starttime) {
			next;
		}	
		
		$info = "$job->{JobID} $start $starttime";
		# get just this hours info
		if($start < $starttime) {
			$start = $starttime;
		}
		
		$info .= " $end $endtime ";
		if($end > $endtime) {
			$end = $endtime;
		}
		$running_time = ($end - $start) * $job->{ReqProcs};
		$info .= "$end $start $running_time ";
		
		if($job->{PAL}) {
			print "got $end - $start * $job->{ReqProcs} = $running_time $job->{AWDuration} extra to subtract for job $job->{JobID}\n";
			$parts{$job->{PAL}} -= $running_time;
		}


		if(($timestamp > $endtime) || ($timestamp < $starttime)) {
			next;
		}
		
		#$info = "";
		#$info .= "$job->{JobID}\t"; #JOBID
		if($job->{PAL}) {
			$info .= "$job->{PAL}\t"; #host
			$info .= "$job->{PAL}_part\t"; #partition
		} else {
			$info .= "-\t-\t"; #host partition
		}
		
		if($job->{Class}) {
			$info .= "$job->{Class}\t"; #pool
		} else {
			$info .= "-\t"; #pool
		}
		
		$info .= "$timestamp\t"; #timestamp
		
		if($job->{User}) {
			$info .= "$job->{User}\t"; #user
		} else {
			$info .= "-\t"; #user
		}
		
		if($job->{Account}) {
			$info .= "$job->{Account}\t"; #bank
		} else {
			$info .= "-\t"; #bank
		}
		
		$info .= "MOAB\t"; #type
		
		$ucpus = $job->{AWDuration} * $job->{ReqProcs};
		$info .= "$ucpus\t"; #ucpu
		
		$info .= "0\t0\t0\n"; #icpu memint vmemint
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
	$running_time = ($end - $start) * $job->{ReqProcs};; 
	print "$end - $start = $running_time\n";
	if($job->{PAL}) {
		$parts{$job->{PAL}} -= $running_time;
		 print "subtracting " . $running_time . " for running job $job->{JobID}\n";
	}
}

while (($part, $icpu) = each(%parts)) {
	$info = "$part\t"; #host
	$info = "$part\t"; #partition
	$info .= "no_pool\t"; #pool
	$info .= "$endtime\t"; #timestamp
	$info .= "(NULL)\t"; #user
	$info .= "noreport\t"; #bank
	$info .= "IDLE\t"; #type
	$info .= "$icpu\t"; #ucpu
	$info .= "0\t0\t0\n"; #icpu memint vmemint
	print FILE $info;
}
close FILE;
system("cat $send_file");
print "scp $send_file $db_user\@$db_host:$db_file 2>/dev/null\n";
#system("scp $send_file $db_user@$db_host:$db_file 2>/dev/null");

system("rm -f $send_file");
