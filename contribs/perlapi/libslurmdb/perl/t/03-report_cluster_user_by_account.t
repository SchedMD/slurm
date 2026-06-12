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

my %assoc_cond = ();
$assoc_cond{'usage_start'} = '1270000000';
$assoc_cond{'usage_end'}   = '1273000000';

my $clusters = Slurmdb::report_cluster_user_by_account($db_conn, \%assoc_cond);

for (my $i = 0; $i < @$clusters; $i++) {
    print "name       $clusters->[$i]{'name'}\n"
	if exists $clusters->[$i]{'name'};
    print "cpu_count  $clusters->[$i]{'cpu_count'}\n"
	if exists $clusters->[$i]{'cpu_count'};
    print "cpu_secs   $clusters->[$i]{'cpu_secs'}\n"
	if exists $clusters->[$i]{'cpu_secs'};

    for (my $j = 0; $j < @{$clusters->[$i]{'assoc_list'}}; $j++) {
	print "$j assoc_list acct $clusters->[$i]{'assoc_list'}[$j]{'acct'}\n"
	    if exists $clusters->[$i]{'assoc_list'}[$j]{'acct'};
	print "$j assoc_list cluster $clusters->[$i]{'assoc_list'}[$j]{'cluster'}\n"
	    if exists $clusters->[$i]{'assoc_list'}[$j]{'cluster'};
	print "$j assoc_list cpu_secs $clusters->[$i]{'assoc_list'}[$j]{'cpu_secs'}\n"
	    if exists $clusters->[$i]{'assoc_list'}[$j]{'cpu_secs'};
	print "$j assoc_list parent_acct $clusters->[$i]{'assoc_list'}[$j]{'parent_acct'}\n"
	    if exists $clusters->[$i]{'assoc_list'}[$j]{'parent_acct'};
	print "$j assoc_list user $clusters->[$i]{'assoc_list'}[$j]{'user'}\n"
	    if exists $clusters->[$i]{'assoc_list'}[$j]{'user'};
    }

    for (my $j = 0; $j < @{$clusters->[$i]{'user_list'}}; $j++) {
	print "$j user_list acct $clusters->[$i]{'user_list'}->[$j]{'acct'}\n"
	    if exists $clusters->[$i]{'user_list'}->[$j]{'acct'};
	for (my $k = 0; $k < @{$clusters->[$i]{'user_list'}->[$j]{'acct_list'}}; $k++) {
	    print "$j $k user_list acct_list $clusters->[$i]{'user_list'}->[$j]{'acct_list'}->[$k]\n";
	}
	for (my $k = 0; $k < @{$clusters->[$i]{'user_list'}->[$j]{'assoc_list'}}; $k++) {
	    print "$j $k user_list assoc_list acct $clusters->[$i]{'user_list'}->[$j]{'assoc_list'}->[$k]{'acct'}\n";
	    print "$j $k user_list assoc_list cluster $clusters->[$i]{'user_list'}->[$j]{'assoc_list'}->[$k]{'cluster'}\n";
	    print "$j $k user_list assoc_list cpu_secs $clusters->[$i]{'user_list'}->[$j]{'assoc_list'}->[$k]{'cpu_secs'}\n";
	    print "$j $k user_list assoc_list parent_acct $clusters->[$i]{'user_list'}->[$j]{'assoc_list'}->[$k]{'parent_acct'}\n";
	    print "$j $k user_list assoc_list user $clusters->[$i]{'user_list'}->[$j]{'assoc_list'}->[$k]{'user'}\n";
	}
	print "$j user_list cpu_secs $clusters->[$i]{'user_list'}->[$j]{'cpu_secs'}\n";
	print "$j user_list name $clusters->[$i]{'user_list'}->[$j]{'name'}\n";
	print "$j user_list uid $clusters->[$i]{'user_list'}->[$j]{'uid'}\n";
    }
    print "\n";
}

my $rc = Slurmdb::connection_close(\$db_conn);
ok( $rc == 0, 'connection_close' );
