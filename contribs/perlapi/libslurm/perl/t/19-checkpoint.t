#!/usr/bin/perl -T
use Test::More tests => 1;
use Slurm qw(:constant);

my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# TODO: do not know how to test



