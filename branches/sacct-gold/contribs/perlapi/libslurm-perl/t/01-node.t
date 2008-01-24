#!/usr/bin/perl -T
use Test::More tests => 4;
use Slurm ':all';

my $resp;

$resp = Slurm->load_node();
ok(ref($resp) eq "HASH", "loading nodes");

$resp = Slurm->load_node(1);
ok(ref($resp) eq "HASH", "loading all nodes");

# at least there is one node in system
my $node = $resp->{node_array}->[0];

SKIP: {
	skip "You have to be superuser to update nodes", 2 if $>;
	my $rc;
	my $err_msg;
	$rc = Slurm->update_node({node_names => $node->{name}, state => NODE_STATE_DRAIN, reason => 'test'});
	$err_msg = Slurm->strerror() unless $rc == SLURM_SUCCESS;
	ok($rc == SLURM_SUCCESS, "drainning nodes") || diag("update_node failed: $err_msg");

	$rc = Slurm->update_node({node_names => $node->{name}, state => NODE_RESUME, features => 'test'});
	$err_msg = Slurm->strerror() unless $rc == SLURM_SUCCESS;
	ok($rc == SLURM_SUCCESS, "resuming nodes") || diag("update_node failed: $err_msg");
}
