#!/usr/bin/perl -T
use Test::More tests => 4;

# 1
BEGIN { use_ok(Slurm, qw(:constant)); }


# 2
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 3
ok(defined SLURM_ERROR, "export constant");


# 4
cmp_ok(SLURM_ERROR, "==", -1, "constant value");
