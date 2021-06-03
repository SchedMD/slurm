#!/usr/bin/perl -T
use Test::More tests => 8;
use Slurm qw(:constant);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my $resp = $slurm->get_job_steps();
ok(ref($resp) eq "HASH", "get job steps");


# 3
SKIP: {
    my ($fh, $print_ok);
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_job_step_info_msg($fh, $resp, 1);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^Job step data as/;
    }
    close($fh);
    ok($print_ok, "print job step info msg");
}


# 4
SKIP: {
    my ($fh, $print_ok);
    skip "no steps in system", 1 unless @{$resp->{job_steps}};
    skip "failed to open temporary file", 1 unless open($fh, '+>', undef);
    $slurm->print_job_step_info($fh, $resp->{job_steps}->[0], 1);
    seek($fh, 0, 0);
    while(<$fh>) {
	$print_ok = 1 if /^StepId=\d+/;
    }
    close($fh);
    ok($print_ok, "print job step info");
}


# 5
SKIP: {
    my ($fh, $print_ok);
    skip "no steps in system", 1 unless @{$resp->{job_steps}};
    my $str = $slurm->sprint_job_step_info($resp->{job_steps}->[0], 1);
    $print_ok = 1 if $str =~ /^StepId=\d+/;
    ok($print_ok, "print job step info");
}


# 6
SKIP: {
    skip "no steps in system", 1 unless @{$resp->{job_steps}};
    my $layout = $slurm->job_step_layout_get($resp->{job_steps}->[0]->{step_id}->{job_id},
					     $resp->{job_steps}->[0]->{step_id}->{step_id});
    ok(ref($layout) eq "HASH" || $slurm->get_errno() == ESLURM_INVALID_JOB_ID, "job step layout get")
	or diag("job_step_layout_get: " . $slurm->strerror());
}


# 7
SKIP: {
    skip "no steps in system", 1 unless @{$resp->{job_steps}};
    my $layout = $slurm->job_step_stat($resp->{job_steps}->[0]->{step_id}->{job_id},
				       $resp->{job_steps}->[0]->{step_id}->{step_id});
    ok(ref($layout) eq "HASH" || $slurm->get_errno() == ESLURM_INVALID_JOB_ID, "job step stat")
	or diag("job_step_stat: " . $slurm->strerror());
}


# 8
SKIP: {
    skip "no steps in system", 1 unless @{$resp->{job_steps}};
    my $layout = $slurm->job_step_get_pids($resp->{job_steps}->[0]->{step_id}->{job_id},
					   $resp->{job_steps}->[0]->{step_id}->{step_id});
    ok(ref($layout) eq "HASH" || $slurm->get_errno() == ESLURM_INVALID_JOB_ID, "job step get pids")
	or diag("job_step_get_pids: " . $slurm->strerror());
}


