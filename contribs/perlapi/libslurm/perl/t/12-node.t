#!/usr/bin/perl -T
use Test::More tests => 7;
use Slurm qw(:constant);


# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my $resp = $slurm->load_node();
ok(ref($resp) eq "HASH", "load node");


# 3
SKIP: {
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_node_info_msg($fh, $resp);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^NodeName=\w+/;
    }
    close($fh);
    ok($print_ok, "print node info msg");
}


# 4
SKIP: {
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_node_table($fh, $resp->{node_array}->[0], 1);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^NodeName=\w+/;
    }
    close($fh);
    ok($print_ok, "print node table");
}


# 5
my $str;
$str = $slurm->sprint_node_table($resp->{node_array}->[0]);
ok($str =~ /^NodeName=\w+/, "sprint node table");



# 6 - 7
SKIP: {
	skip "You are not super user", 2 if $>;
	my $node = $resp->{node_array}->[0];

	$rc = $slurm->update_node({node_names => $node->{name}, state => NODE_STATE_DRAIN, reason => 'perlapi test'});
	$err_msg = $slurm->strerror() unless $rc == SLURM_SUCCESS;
	ok($rc == SLURM_SUCCESS, "update node") || diag("update_node failed: $err_msg");


	$rc = $slurm->update_node({node_names => $node->{name}, state => NODE_RESUME, features => 'test', fetures_act => ''});
	$err_msg = $slurm->strerror() unless $rc == SLURM_SUCCESS;
	ok($rc == SLURM_SUCCESS, "update node") || diag("update_node failed: $err_msg");
}
