#!/usr/bin/perl -T
use Test::More tests => 9;
use Slurm ':constant';


# 1
my $slurm = Slurm::new();
ok(defined $slurm, "create slurm object with default configuration");


# 2
my ($major, $minor, $micro) = $slurm->api_version();
ok(defined $micro, "api version");


# 3
my $resp = $slurm->load_ctl_conf();
ok(ref($resp) eq "HASH", "load ctl conf");


# 4
SKIP: {
    skip("TODO: This functionality doesn't work in perlAPI now", 1);
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_ctl_conf($fh, $resp);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^SlurmctldPort/;
    }
    close($fh);
    ok($print_ok, "print ctl conf");
}


# 5
SKIP:  {
	skip("TODO: This functionality doesn't work in perlAPI now", 1);
	my $list = $slurm->ctl_conf_2_key_pairs($resp);
	ok(ref($list) eq "Slurm::List", "ctl conf 2 key pairs");
}


# 6
$resp = $slurm->load_slurmd_status();
ok((defined $resp || $slurm->strerror() eq "Connection refused"), "load slurmd status");


# 7
SKIP: {
    skip("TODO: This functionality doesn't work in perlAPI now", 1);
    my ($fh, $print_ok);
    skip "this is not a compute node", 1 unless defined $resp;
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_slurmd_status($fh, $resp);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^Slurmd PID\s+=\s+\d+$/;
    }
    close($fh);
    ok($print_ok, "print slurmd status");
}


# 8
SKIP: {
    my ($fh, $print_ok);
    local $TODO = "do not know how to test";
    skip($TODO, 1);
    ok($print_ok, "print key pairs");
}


# 9
SKIP: {
    my $update_ok;
    local $TODO = "do not know how to test";
    skip($TODO, 1);
    ok($update_ok, "update step");
}
