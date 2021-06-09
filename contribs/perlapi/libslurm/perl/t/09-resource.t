#!/usr/bin/perl -T
use Test::More tests => 5;
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
    stdout => "/dev/null",
    stderr => "/dev/null",
    environment => \%env,
};
$resp = $slurm->submit_batch_job($job_desc);
ok($resp, "submit batch job") or diag ("submit_batch_job: " . $slurm->strerror());


# 3
$resp = $slurm->load_jobs(0, SHOW_DETAIL);
ok(ref($resp) eq "HASH", "load jobs") or diag("load_jobs: " . $slurm->strerror());


my ($job, $resrcs);
foreach (@{$resp->{job_array}}) {
    if ($_->{job_resrcs}) {
	$resrcs = $_->{job_resrcs};
	$job = $_;
	last;
    }
}

# 4, 5
SKIP: {
    skip "no job resources", 2 unless $resrcs;

    my $cnt = $slurm->job_cpus_allocated_on_node_id($resrcs, 0);
    ok($cnt, "job cpus allocated on node id") or diag("job_cpus_allocated_on_node_id: $cnt");

    my $hl = Slurm::Hostlist::create($job->{nodes});
    my $node = $hl->shift;
    $cnt = $slurm->job_cpus_allocated_on_node($resrcs, $node);
    ok($cnt, "job cpus allocated on node") or diag("job_cpus_allocated_on_node: $cnt");
}

