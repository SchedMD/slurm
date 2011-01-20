#!/usr/bin/perl -T
use Test::More tests => 9;
use Slurm qw(:constant);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my $resp = $slurm->load_ctl_conf();
ok(ref($resp) eq "HASH", "load ctl conf");


# 3
my $rc = $slurm->ping();
ok($rc == SLURM_SUCCESS, "ping primary controller");


# 4
SKIP: {
    skip "no backup control machine configured", 1 unless $resp->{backup_controller};
    $rc = $slurm->ping(2);
    ok($rc == SLURM_SUCCESS, "ping backup control machine") || diag ("ping backup controller: " . $slurm->strerror());
}


# 5
SKIP: {
    skip "better not testing this", 1;
    #$rc = $slurm->shutdown();
    ok($rc == SLURM_SUCCESS, "shutdown");
}


# 6
SKIP: {
    skip "better not testing this", 1;
    skip "no backup control machine configured", 1 unless $resp->{backup_controller};
    #$rc = $slurm->takeover();
    ok($rc == SLURM_SUCCESS, "takeover");
}


# 7
SKIP: {
    skip "not super user", 1 if $>;
    $rc = $slurm->set_debug_level(3);
    ok($rc == SLURM_SUCCESS, "set debug level") or diag("set_debug_level: " . $slurm->strerror());
}


# 8
SKIP: {
    skip "not super user", 1 if $>;
    $rc = $slurm->set_schedlog_level(1);
    ok($rc == SLURM_SUCCESS, "set sched log level") || diag("set_sched_log_level" . $slurm->strerror());
}


# 9
SKIP: {
    skip "not super user", 1 if $>;
    $rc = $slurm->reconfigure();
    ok($rc == SLURM_SUCCESS, "reconfigure") || diag("reconfigure: " . $slurm->strerror());
}

