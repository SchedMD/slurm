#!/usr/bin/perl -T
use Test::More tests => 8;
use Slurm qw(:constant);


# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my $resp = $slurm->load_ctl_conf();
ok(ref($resp) eq "HASH", "load ctl conf") or diag("load_ctl_conf failed: " . $slurm->strerror());


# 3
my $bi_msg;
SKIP: {
    skip "system not supported", 1 unless $resp->{select_type} eq "select/bluegene";
    $bi_msg = $slurm->load_block_info();
    ok(ref($bi_msg) eq "HASH", "load block info") or diag("load_block_info error: " . $slurm->strerror());
}

# 3
SKIP: {
    skip "no block info msg", 1 unless $bi_msg;
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_block_info_msg($fh, $bi_msg);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^Bluegene Block data as of/;
    }
    close($fh);
    ok($print_ok, "print block info msg");
}


# 4
SKIP: {
    skip "no block info msg", 1 unless $bi_msg;
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_block_info($fh, $bi_msg->{block_array}->[0], 1);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^BlockName=\w+/;
    }
    close($fh);
    ok($print_ok, "print block info");
}


# 5
SKIP: {
    skip "no block info msg", 1 unless $bi_msg;
    my $str;
    $str = $slurm->sprint_block_info($bi_msg->{block_array}->[0]);
    ok($str =~ /^BlockName=\w+/, "sprint block info");
}


# 6 - 7
SKIP: {
	# TODO
	skip "don't know how to test", 2;
        skip "no block info msg", 2 unless $bi_msg;
	skip "not super user", 2 if $>;
	my $block = $bi_msg->{block_array}->[0];

	$rc = $slurm->update_block({});
	$err_msg = $slurm->strerror() unless $rc == SLURM_SUCCESS;
	ok($rc == SLURM_SUCCESS, "update block") || diag("update_block failed: $err_msg");

	$rc = $slurm->update_block({});
	$err_msg = $slurm->strerror() unless $rc == SLURM_SUCCESS;
	ok($rc == SLURM_SUCCESS, "update block") || diag("update_block failed: $err_msg");
}
