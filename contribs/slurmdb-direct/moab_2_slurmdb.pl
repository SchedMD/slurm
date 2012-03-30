#! /usr/bin/perl -w
###############################################################################
#
# slurmdbd_direct - write directly into the slurmdbd.
#
# based off the index in
# http://www.clusterresources.com/products/mwm/docs/16.3.3workloadtrace.shtml
#
#
###############################################################################
#  Copyright (C) 2009 Lawrence Livermore National Security.
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Danny Auble <da@llnl.gov>
#  CODE-OCEC-09-009. All rights reserved.
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
###############################################################################

use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case require_order);
#use lib "${FindBin::Bin}/../lib/perl";
use lib qw(/home/da/slurm/1.3/snowflake/lib/perl/5.8.8);
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Switch;
use DBI;
BEGIN { require "config.slurmdb.pl"; }
our ($logLevel, $db_conn_line, $db_job_table, $db_user, $db_passwd);

my $set = 0;

my $job_common_columns =
	"(id_job, id_assoc, id_wckey, track_steps, priority, " .
	"id_user, id_group, account, partition, wckey, job_name, " .
	"state, cpus_req, time_submit";

my %submit_sql;

my %migrate_sql;

my %start_sql;

my %end_sql;

foreach my $line (<STDIN>) {
	chomp $line;
	# the below list is based off the index in
#  http://www.clusterresources.com/products/mwm/docs/16.3.3workloadtrace.shtml
	my ($hr_time,
	    $timestamp,
	    $type,
	    $id,
	    $event,
	    $req_nodes,
	    $req_tasks,
	    $user,
	    $group,
	    $wall_limit,
	    $state,
	    $partition,
	    $eligible_time,
	    $dispatch_time,
	    $start_time,
	    $end_time,
	    $network,
	    $arch,
	    $op,
	    $node_mem_comp,
	    $node_mem,
	    $node_disk_comp,
	    $node_disk,
	    $node_features,
	    $submit_time,
	    $alloc_tasks,
	    $tasks_per_node,
	    $qos,
	    $flags,
	    $account,
	    $executable,
	    $rm_ext,
	    $bypass_cnt,
	    $cpu_secs,
	    $cluster,
	    $procs_per_task,
	    $mem_per_cpu,
	    $disk_per_task,
	    $swap_per_task,
	    $other_time,
	    $timeout,
	    $alloc_hostlist,
	    $rm_name,
	    $req_hostlist,
	    $resv,
	    $app_sim_data,
	    $desc,
	    $message,
	    $cost,
	    $history,
	    $util,
	    $estimate,
	    $comp_code,
	    $ext_mem,
	    $ext_cpu,
	    @extra) = split /\s+/, $line;
	next if !$type;
	next if $type ne "job";

	my $uid = getpwnam($user);
	my $gid = getgrnam($group);
	$uid = -2 if !$uid;
	$gid = -2 if !$gid;

	# figure out the wckey
	my $wckey = "";
	if ($rm_ext =~ /wckey:(\w*)/) {
		$wckey = $1;
	}

	if($partition =~ /\[(\w*)/) {
		$partition = $1;
	}

	# Only pick out a number at the beginning if it is something else
	# we should skip it and make comp_code 0.  If we want something
	# else just change it to whatever you would think would be best.
	# Dispite the Moab documentation the comp code could contain
	# characters like ,SID= afterwards,  without knowing what that means
	# we just skip it.  We haven't seen a case where comp_code isn't an
	# int at the first so the 0 "should" never happen.
	if($comp_code =~ /^(\d+)/) {
		$comp_code = $1;
	} else {
		$comp_code = 0;
	}

	#figure out the cluster
	if($cluster eq "ALL") {
		if ($node_features =~ /\[(\w*)\]/) {
			$cluster = $1;
		} elsif ($rm_ext =~ /partition:(\w*)/) {
			$cluster = $1;
		} elsif ($rm_ext =~ /feature:(\w*)/) {
			$cluster = $1;
		} elsif ($rm_ext =~ /PARTITION=(\w*)/) {
			$cluster = $1;
		} else {
			print "ERROR: No cluster found for\n$line\n";
			next;
		}
	}

	if($message =~ /job\\20exceeded\\20wallclock\\20limit/) {
		$event = "JOBTIMEOUT";
	}

	my $alloc_hl = Slurm::Hostlist::create($alloc_hostlist);
	if($alloc_hl) {
		Slurm::Hostlist::uniq($alloc_hl);
		$alloc_hl = Slurm::Hostlist::ranged_string($alloc_hl);
	}

	if($event eq "JOBSUBMIT") {
		if(!$submit_sql{$cluster}) {
			$submit_sql{$cluster} =
				"INSERT INTO $cluster" . "_$db_job_table " .
				"$job_common_columns) VALUES ";
		} else {
			$submit_sql{$cluster} .= ", "
		}
		$submit_sql{$cluster} .= "($id, 0, 0, 0, 0, $uid, $gid, " .
			"\"$account\", \"$partition\", \"$wckey\", " .
			"\"$executable\", 0, $req_tasks, $submit_time)";
		$set = 1;
	} elsif ($event eq "JOBMIGRATE") {
		if(!$migrate_sql{$cluster}) {
			$migrate_sql{$cluster} =
				"INSERT INTO $cluster" . "_$db_job_table " .
				"$job_common_columns, time_eligible) VALUES ";
		} else {
			$migrate_sql{$cluster} .= ", ";
		}
		# here for some reason the eligible time is really the
		# submit time, so we use the end time which appears
		# to be the best guess.
		$migrate_sql{$cluster} .= "($id, 0, 0, 0, 0, $uid, $gid, " .
			"\"$account\", \"$partition\", \"$wckey\", " .
			"\"$executable\", 0, $req_tasks, $submit_time, " .
			"$end_time)";
		$set = 1;
	} elsif ($event eq "JOBSTART") {
		if(!$start_sql{$cluster}) {
			$start_sql{$cluster} =
				"INSERT INTO $cluster" . "_$db_job_table " .
				"$job_common_columns, time_eligible, " .
				"time_start, nodelist, cpus_alloc) VALUES ";
		} else {
			$start_sql{$cluster} .= ", ";
		}

		# req_tasks is used for alloc_tasks on purpose.
		# alloc_tasks isn't always correct.
		$start_sql{$cluster} .= "($id, 0, 0, 0, 0, $uid, $gid, " .
			"\"$account\", \"$partition\", \"$wckey\", " .
			"\"$executable\", 1, $req_tasks, $submit_time, " .
			"$eligible_time, $start_time, \"$alloc_hl\", " .
			"$req_tasks)";
		$set = 1;
	} elsif (($event eq "JOBEND") || ($event eq "JOBCANCEL")
		|| ($event eq "JOBFAILURE") || ($event eq "JOBTIMEOUT"))  {
		if($event eq "JOBEND") {
			$state = 3;
		} elsif($event eq "JOBCANCEL") {
			$state = 4;
		} elsif($event eq "JOBFAILURE") {
			$state = 5;
		} else {
			$state = 6;
		}

		if(!$end_sql{$cluster}) {
			$end_sql{$cluster} =
				"INSERT INTO $cluster" . "_$db_job_table " .
				"$job_common_columns, time_eligible, " .
				"time_start, nodelist, cpus_alloc, " .
				"time_end, exit_code) VALUES ";
		} else {
			$end_sql{$cluster} .= ", ";
		}
		$end_sql{$cluster} .= "($id, 0, 0, 0, 0, $uid, $gid, " .
			"\"$account\", \"$partition\", \"$wckey\", " .
			"\"$executable\", $state, $req_tasks, $submit_time, " .
			"$eligible_time, $start_time, \"$alloc_hl\", " .
			"$req_tasks, $end_time, $comp_code)";
		$set = 1;
	} else {
		print "ERROR: unknown event of $event\n";
		next;
	}
}

exit 0 if !$set;

$db_user = (getpwuid($<))[0] if !$db_user;
my $dbhandle = DBI->connect($db_conn_line, $db_user, $db_passwd,
			    {AutoCommit => 1, RaiseError => 1});

while (my ($key, $line) = each(%submit_sql)) {
	$line .= " on duplicate key update id_job=VALUES(id_job)";
	#print "submit\n$line\n\n";
	$dbhandle->do($line);
}

while (my ($key, $line) = each(%migrate_sql)) {
	$line .= " on duplicate key update " .
		"time_eligible=VALUES(time_eligible)";
	#print "migrate\n$line\n\n";
	$dbhandle->do($line);
}

while (my ($key, $line) = each(%start_sql)) {
	$line .= " on duplicate key update nodelist=VALUES(nodelist), " .
		"account=VALUES(account), partition=VALUES(partition), " .
		"wckey=values(wckey), time_start=VALUES(time_start), " .
		"job_name=VALUES(job_name), state=values(state), " .
		"cpus_alloc=values(cpus_alloc)";
	#print "start\n$line\n\n";
	$dbhandle->do($line);
}

while (my ($key, $line) = each(%end_sql)) {
	$line .= " on duplicate key update time_end=VALUES(time_end), " .
		"state=VALUES(state), exit_code=VALUES(exit_code)";
	#print "end\n$line\n\n";
	$dbhandle->do($line);
}

exit 0;
