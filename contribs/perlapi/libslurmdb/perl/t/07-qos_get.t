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

my %qos_cond = ();
#$qos_cond{description_list} = ["general","other"];
#$qos_cond{id_list}          = ["1","2","14"];
#$qos_cond{name_list}        = ["normal","special"];
#$qos_cond{with_deleted}     = "1";

my $qoss = Slurmdb::qos_get($db_conn, \%qos_cond);
print Dumper($qoss);

my $rc = Slurmdb::connection_close(\$db_conn);
ok( $rc == 0, 'connection_close' );
