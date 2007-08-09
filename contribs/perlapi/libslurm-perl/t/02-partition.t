#!/usr/bin/perl -T
use Test::More tests => 4;
use Slurm ':all';

my $resp;
my $partname = "libslurm-perl-test";

$resp = Slurm->load_partitions();
ok(ref($resp) eq "HASH", "loading partitions");

$resp = Slurm->load_partitions(1);
ok(ref($resp) eq "HASH", "loading all partitions");


SKIP: {
	skip "You have to be superuser to update partitions", 2 if $>;
	# at least there is one node in system
	$resp = Slurm->load_node(1);
	my $nodename = $resp->{node_array}->[0]->{name};
	my $rc;
	my $err_msg;
	$rc = Slurm->update_partition({name => $partname, nodes => $nodename, state_up => 1, root_only => 1, hidden => 1});
	$err_msg = Slurm->strerror() unless $rc == SLURM_SUCCESS;
	ok($rc == SLURM_SUCCESS, "creating partition") || diag("update_partition failed: $err_msg");
	#delete it
	$rc = Slurm->delete_partition($partname);
	$err_msg = Slurm->strerror() unless $rc == SLURM_SUCCESS;
	ok($rc == SLURM_SUCCESS, "deleting partition") || diag("delete_partition failed: $err_msg");
}
