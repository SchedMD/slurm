#!/usr/bin/perl -T
use Test::More tests => 9;
use Slurm qw(:constant);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my $resp;
$resp = $slurm->load_partitions();
ok(ref($resp) eq "HASH", "load partitions");


# 3
SKIP: {
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_partition_info_msg($fh, $resp);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^Partition data as of/;
    }
    close($fh);
    ok($print_ok, "print partition info msg");
}


# 4
SKIP: {
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_partition_info($fh, $resp->{partition_array}->[0]);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^PartitionName=\w+/;
    }
    close($fh);
    ok($print_ok, "print partition info");
}


# 5
my $str = $slurm->sprint_partition_info($resp->{partition_array}->[0]);
ok(defined $str && $str =~ /^PartitionName=\w+/, "sprint partition info") or diag("sprint_partition_info: $str");


# 6
$resp = $slurm->load_node();
ok(ref($resp) eq "HASH", "load node");


my $node_name = $resp->{node_array}->[0]->{name};
my $part_name = "perlapi_test";
my $rc;


# 7
SKIP: {
    skip "not super user", 1 if $>;
    $rc = $slurm->create_partition({name => $part_name, nodes => $node_name});
    ok($rc == SLURM_SUCCESS, "create partition") || diag("create partition: " . $slurm->strerror());
}


# 8
SKIP: {
    skip "not super user", 1 if $>;
    $rc = $slurm->update_partition({name => $part_name, flags => PART_FLAG_ROOT_ONLY});
    ok($rc == SLURM_SUCCESS, "update partition") || diag("update partition: " . $slurm->strerror());
}


# 9
SKIP: {
    skip "not super user", 1 if $>;
    $rc = $slurm->delete_partition({name => $part_name});
    ok($rc == SLURM_SUCCESS, "delete partition") || diag("delete partition: " . $slurm->strerror());
}
