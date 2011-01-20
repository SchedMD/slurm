#!/usr/bin/perl -T
use Test::More tests => 1;
use Slurm qw(:constant);


my $slurm = Slurm::new();
ok(ref $slurm eq "Slurm", "create slurm object with default configuration");


# TODO: do not know how to test
