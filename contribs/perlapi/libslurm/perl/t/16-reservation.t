#!/usr/bin/perl -T
use Test::More tests => 8;
use Slurm qw(:constant);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my $resv;
SKIP: {
    skip "not super user", 1 if $>;
    $resv = $slurm->create_reservation( { start_time => NO_VAL,
					     duration => 100,
					     flags => RESERVE_FLAG_OVERLAP | RESERVE_FLAG_IGN_JOBS,
					     users => 'root',
					     node_cnt => 1
					   } );
    ok(defined $resv, "create reservation") || diag ("create_reservation: " . $slurm->strerror());
}


# 3
SKIP: {
    skip "not super user", 1 if $>;
    my $rc = $slurm->update_reservation( { name => $resv, duration => 20 });
    ok($rc == SLURM_SUCCESS, "update reservation") || diag ("update_rerservation: " . $slurm->strerror());
}

# 4
my $resp = $slurm->load_reservations();
ok(ref($resp) eq "HASH", "load reservations");


# 5
SKIP: {
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_reservation_info_msg($fh, $resp, 1);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^Reservation data as of/;
    }
    close($fh);
    ok($print_ok, "print reservation info msg");
}


# 6
SKIP: {
    my ($fh, $print_ok);
    skip "no reservation in system", 1 unless @{$resp->{reservation_array}};
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_reservation_info($fh, $resp->{reservation_array}->[0], 1);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^ReservationName=\w+/;
    }
    close($fh);
    ok($print_ok, "print reservation info");
}


# 7
SKIP: {
    skip "no reservation in system", 1 unless @{$resp->{reservation_array}};
    my $str = $slurm->sprint_reservation_info($resp->{reservation_array}->[0], 1);
    ok(defined $str && $str =~ /^ReservationName=\w+/, "sprint reservation info") or diag("sprint_reservation_info: $str");
}


# 8
SKIP: {
    skip "not super user", 1 if $>;
    $rc = $slurm->delete_reservation({name => $resv});
    # XXX: if accounting_storage/slurmdbd is configured and slurmdbd fails, delete reservation will fail.
    ok($rc == SLURM_SUCCESS, "delete reservation") || diag("delete_reservation" . $slurm->strerror());
}




