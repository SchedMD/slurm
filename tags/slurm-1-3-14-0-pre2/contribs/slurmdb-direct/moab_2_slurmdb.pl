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
use lib "${FindBin::Bin}/../lib/perl";
use lib qw(/home/da/slurm/1.3/snowflake/lib/perl/5.8.8);
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Switch;
use DBI;
BEGIN { require "config.slurmdb.pl"; }
our ($logLevel, $db_conn_line, $db_job_table, $db_user, $db_passwd);

my $set = 0;

my $sql = "INSERT INTO $db_job_table " .
	"(jobid, associd, wckeyid, uid, gid, nodelist, " .
	"cluster, account, partition, wckey, eligible, " .
	"submit, start, name, track_steps, state, priority, " .
	"req_cpus, alloc_cpus) VALUES ";

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
	    $class,
	    $sub_time,
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
	    $queue_time,
	    $alloc_tasks,
	    $tasks_per_node,
	    $qos,
	    $flags,
	    $account,
	    $executable,
	    $rm_ext,
	    $bypass_cnt,
	    $cpu_secs,
	    $partition,
	    $procs_per_task,
	    $mem_per_task,
	    $disk_per_task,
	    $swap_per_task,
	    $eligible_time,
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
	next if $event eq "JOBMIGRATE";

	my $uid = getpwnam($user);
	my $gid = getgrnam($group);
	$uid = -2 if !$uid;
	$gid = -2 if !$gid;

	my $alloc_hl = Slurm::Hostlist::create($alloc_hostlist);
	if($alloc_hl) {
		Slurm::Hostlist::uniq($alloc_hl);
		$alloc_hl = Slurm::Hostlist::ranged_string($alloc_hl);
	}

	$sql .= ", " if $set;
	$sql .= "($id, 0, 0, $uid, $gid, '$alloc_hl', )";
	$set = 1;
}

exit 0 if !$set;
$sql .= " on duplicate key update nodelist=VALUES(nodelist), account=VALUES(account), partition=VALUES(partition), wckey=VALUES(wckey), start=VALUES(start), alloc_cpus=VALUES(alloc_cpus)";
print "$sql\n";

exit 0;
$db_user = (getpwuid($<))[0] if !$db_user;
my $dbhandle = DBI->connect($db_conn_line, $db_user, $db_passwd,
			    {AutoCommit => 1, RaiseError => 1});
$dbhandle->do($sql);

exit 0;
