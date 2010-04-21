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

my $accounts = Slurmdb::report_cluster_account_by_user($db_conn, \%assoc_cond);

for (my $i = 0; $i < @$accounts; $i++) {
    for (my $j = 0; $j < @{$accounts->[$i]{'assoc_list'}}; $j++) {
	print "$j assoc_list acct $accounts->[$i]{'assoc_list'}[$j]{'acct'}\n"
	    if exists $accounts->[$i]{'assoc_list'}[$j]{'acct'};
	print "$j assoc_list cluster $accounts->[$i]{'assoc_list'}[$j]{'cluster'}\n"
	    if exists $accounts->[$i]{'assoc_list'}[$j]{'cluster'};
	print "$j assoc_list cpu_secs $accounts->[$i]{'assoc_list'}[$j]{'cpu_secs'}\n"
	    if exists $accounts->[$i]{'assoc_list'}[$j]{'cpu_secs'};
	print "$j assoc_list parent_acct $accounts->[$i]{'assoc_list'}[$j]{'parent_acct'}\n"
	    if exists $accounts->[$i]{'assoc_list'}[$j]{'parent_acct'};
	print "$j assoc_list user $accounts->[$i]{'assoc_list'}[$j]{'user'}\n"
	    if exists $accounts->[$i]{'assoc_list'}[$j]{'user'};
    }

    print "cpu_count  $accounts->[$i]{'cpu_count'}\n"
	if exists $accounts->[$i]{'cpu_count'};
    print "cpu_secs   $accounts->[$i]{'cpu_secs'}\n"
	if exists $accounts->[$i]{'cpu_secs'};
    print "name       $accounts->[$i]{'name'}\n"
	if exists $accounts->[$i]{'name'};

    for (my $j = 0; $j < @{$accounts->[$i]{'user_list'}}; $j++) {
	print "user_list acct $accounts->[$i]{'user_list'}->{'acct'}\n";
	#print "user_list acct_list $accounts->[$i]{'user_list'}->[0]{'acct_list'}\n";
	#print "user_list assoc_list $accounts->[$i]{'user_list'}->[0]{'assoc_list'}\n";
	print "user_list cpu_secs $accounts->[$i]{'user_list'}->[0]{'cpu_secs'}\n";
	print "user_list name $accounts->[$i]{'user_list'}->[0]{'name'}\n";
	print "user_list uid $accounts->[$i]{'user_list'}->[0]{'uid'}\n";
    }
}

my $rc = Slurmdb::connection_close($db_conn);
ok( $rc == 0, 'connection_close' );
