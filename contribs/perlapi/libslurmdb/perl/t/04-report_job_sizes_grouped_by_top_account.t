#!/usr/bin/perl -T
# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl Slurmdb.t'
use strict;
use warnings;

#########################

use Test::More tests => 2;
BEGIN { use_ok('Slurmdb') };

#########################

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

my $db_conn = Slurmdb::connection_get();

my %job_cond = ();
$job_cond{'usage_start'} = '1270000000';
$job_cond{'usage_end'}   = '1273000000';
my @grouping = qw( 50 250 500 1000 );
my $flat_view = 0;

my $clusters = Slurmdb::report_job_sizes_grouped_by_top_account($db_conn, \%job_cond, \@grouping, $flat_view);

for (my $i = 0; $i < @$clusters; $i++) {
    print "cluster   $clusters->[$i]{'cluster'}\n";
    print "cpu_secs  $clusters->[$i]{'cpu_secs'}\n";
    for (my $j = 0; $j < @{$clusters->[$i]{'acct_list'}}; $j++) {
	print "$j acct_list acct $clusters->[$i]{'acct_list'}[$j]{'acct'}\n"
	    if exists $clusters->[$i]{'acct_list'}[$j]{'acct'};
	print "$j acct_list cpu_secs $clusters->[$i]{'acct_list'}[$j]{'cpu_secs'}\n"
	    if exists $clusters->[$i]{'acct_list'}[$j]{'cpu_secs'};
	print "$j acct_list lft $clusters->[$i]{'acct_list'}[$j]{'lft'}\n"
	    if exists $clusters->[$i]{'acct_list'}[$j]{'lft'};
	print "$j acct_list rgt $clusters->[$i]{'acct_list'}[$j]{'rgt'}\n"
	    if exists $clusters->[$i]{'acct_list'}[$j]{'rgt'};
	for (my $k = 0; $k < @{$clusters->[$i]{'acct_list'}->[$j]{'groups'}}; $k++) {
	    print "$j $k acct_list groups min_size $clusters->[$i]{'acct_list'}->[$j]{'groups'}->[$k]{'min_size'}\n";
	    print "$j $k acct_list groups max_size $clusters->[$i]{'acct_list'}->[$j]{'groups'}->[$k]{'max_size'}\n";
	    print "$j $k acct_list groups jobcount $clusters->[$i]{'acct_list'}->[$j]{'groups'}->[$k]{'count'}\n";
	    print "$j $k acct_list groups cpu_secs $clusters->[$i]{'acct_list'}->[$j]{'groups'}->[$k]{'cpu_secs'}\n";
	}
    }
    print "\n";
}

my $rc = Slurmdb::connection_close(\$db_conn);
ok( $rc == 0, 'connection_close' );
