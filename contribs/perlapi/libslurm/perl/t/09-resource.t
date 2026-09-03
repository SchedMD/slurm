#!/usr/bin/perl -T
use Test::More tests => 4;
use Slurm qw(:constant);

# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my %env = ('PATH' => $ENV{'PATH'});
my $job_desc = {
    min_nodes => 1,
    num_tasks => 1,
    user_id => $>,
    script => "#!/bin/sh\nsleep 1000\n",
    name => "perlapi_test",
    std_out => "/dev/null",
    std_err => "/dev/null",
    work_dir => "/tmp",
    environment => \%env,
};
$resp = $slurm->submit_batch_job($job_desc);
ok($resp, "submit batch job") or diag ("submit_batch_job: " . $slurm->strerror());


# 3
$resp = $slurm->load_jobs(0, SHOW_DETAIL);
ok(ref($resp) eq "HASH", "load jobs") or diag("load_jobs: " . $slurm->strerror());


#
# Find a job with allocated resources. The job submitted above needs a moment
# to be scheduled, so reload until one shows up rather than racing it.
#
my ($job, $node_resrcs);
foreach my $try (1 .. 10) {
    foreach (@{$resp->{job_array}}) {
	    if ($_->{node_resrcs}) {
	        $node_resrcs = $_->{node_resrcs};
	        $job = $_;
	        last;
	    }
    }
    last if $node_resrcs;
    sleep 1;
    $resp = $slurm->load_jobs(0, SHOW_DETAIL);
}

# 4
SKIP: {
    skip "no node resources", 1 unless $node_resrcs && @$node_resrcs;

    my $cnt = $node_resrcs->[0]->{cpus};
    ok($cnt, "job cpus allocated on node id") or diag("node_resrcs[0]{cpus}: " . (defined $cnt ? $cnt : "undef"));
}
