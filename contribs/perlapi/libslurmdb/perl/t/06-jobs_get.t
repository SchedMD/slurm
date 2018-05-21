#!/usr/bin/perl -T
# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl Slurmdb.t'
use strict;
use warnings;

#########################

use Test::More tests => 2;
BEGIN { use_ok('Slurmdb') };
use Data::Dumper;

#########################

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

my $db_conn = Slurmdb::connection_get();

my %job_cond = ();
#$job_cond{'usage_start'} = 0;
#$job_cond{'usage_end'}   = 0;
#$job_cond{acct_list}     = ["blah"];
#$job_cond{userid_list}   = [1003];
#$job_cond{groupid_list}  = [500];
#$job_cond{jobname_list}  = ["hostname","pwd"];
#my @states = ("CA", "CD", "FAILED");
#my @state_nums = map {$slurm->job_state_num($_)} @states;
#$job_cond{state_list} = \@state_nums;
#$job_cond{step_list} = "2547,2549,2550.1";
$job_cond{'flags'} = JOBCOND_FLAG_NO_TRUNC;

my $jobs = Slurmdb::jobs_get($db_conn, \%job_cond);
print Dumper($jobs);

my $rc = Slurmdb::connection_close(\$db_conn);
ok( $rc == 0, 'connection_close' );
