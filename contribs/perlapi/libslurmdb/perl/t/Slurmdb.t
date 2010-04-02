# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl Slurmdb.t'

#########################

# change 'tests => 4' to 'tests => last_test_to_print';

use Test::More tests => 1;
BEGIN { use_ok('Slurmdb') };

#########################

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

my $db_conn = Slurmdb::connection_get();

my %hv = ();

my $results = Slurmdb::clusters_get($db_conn, \%hv);

my $rc = Slurmdb::connection_close($db_conn);

print "return code: $rc\n";

print "classification $results->[0]{'classification'}\n";
print "control_host   $results->[0]{'control_host'}\n";
print "control_port   $results->[0]{'control_port'}\n";
print "cpu_count      $results->[0]{'cpu_count'}\n";
print "name           $results->[0]{'name'}\n";
print "nodes          $results->[0]{'nodes'}\n";
print "rpc_version    $results->[0]{'rpc_version'}\n";
