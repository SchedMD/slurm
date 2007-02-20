#!/usr/bin/env perl

use slurm;

$tmp = slurm::slurm_get_job_steps(0,0,0,0);

for ($i = 0; $i < $tmp->{job_step_count}; ++$i) {
  $step = $tmp->get_step($i);
  print $step->{job_id}, ".", $step->{step_id}, " ",
    $step->{name}, " ", $step->{partition},"\n";
}
