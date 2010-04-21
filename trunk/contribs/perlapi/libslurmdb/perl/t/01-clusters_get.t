#!/usr/bin/perl -T
# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl Slurmdb.t'
use strict;
use warnings;

#########################

use Test::More tests => 3;
BEGIN { use_ok('Slurmdb') };

#########################

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

my $db_conn = Slurmdb::connection_get();

my %hv = ();

my $clusters = Slurmdb::clusters_get($db_conn, \%hv);
ok( $clusters != 0, 'clusters_get' );

for (my $i = 0; $i < @$clusters; $i++) {

      print "classification $clusters->[$i]{'classification'}\n";
      print "control_host   $clusters->[$i]{'control_host'}\n";
      print "control_port   $clusters->[$i]{'control_port'}\n";
      print "cpu_count      $clusters->[$i]{'cpu_count'}\n";
      print "name           $clusters->[$i]{'name'}\n";
      print "nodes          $clusters->[$i]{'nodes'}\n";
      print "rpc_version    $clusters->[$i]{'rpc_version'}\n";
}

my $rc = Slurmdb::connection_close($db_conn);
ok( $rc == 0, 'connection_close' );
