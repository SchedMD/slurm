#!/usr/bin/perl -T
use Test::More tests => 4;
use Slurm qw(:constant);


# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my $resp = $slurm->load_topo();
ok(ref($resp) eq "HASH", "load topo");
my $rec_cnt = @{$resp->{topo_array}};

# 3
SKIP: {
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    if ($rec_cnt) {
	$slurm->print_topo_info_msg($fh, $resp);
	seek($fh, 0, 0);
	while(<$fh>) {
	    $print_ok = 1 if /^SwitchName=/;
	}
    }
    close($fh);
    ok($print_ok || $rec_cnt == 0, "print topo info msg");
}


# 4
SKIP: {
    my ($fh, $print_ok);
    skip "no topo record available", 1 unless $rec_cnt;
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_topo_record($fh, $resp->{topo_array}->[0], 1);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^SwitchName=/;
    }
    close($fh);
    ok($print_ok, "print topo record");
}

