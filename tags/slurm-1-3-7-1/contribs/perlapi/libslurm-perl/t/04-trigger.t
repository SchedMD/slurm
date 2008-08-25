#!/usr/bin/perl -T
use Test::More tests => 3;
use Slurm ':all';

my $resp;

$resp = Slurm->get_triggers();
ok(ref($resp) eq "HASH", "getting triggers");

# set a test trigger
diag("These 2 tests are not finished yet.");
TODO: {
	todo_skip "Not implemented yet", 2;
my $rc;
my $err_msg;
#$rc = Slurm->set_trigger({});
#$err_msg = Slurm->strerror() unless $rc == SLURM_SUCCESS;
ok($rc == SLURM_SUCCESS, "setting trigger") || diag("set_trigger failed: $err_msg");
#clear the trigger
#$rc = Slurm->set_trigger({});
#$err_msg = Slurm->strerror() unless $rc == SLURM_SUCCESS;
ok($rc == SLURM_SUCCESS, "setting trigger") || diag("set_trigger failed: $err_msg");
}

