#!/usr/bin/perl -T
use Test::More tests => 2;
use Slurm ':all';

my $resp;

$resp = Slurm->load_ctl_conf();
ok(ref($resp) eq "HASH", "loading controller configuration");

my ($major, $minor, $micro) = Slurm->api_version();
#diag("slurm api version: $major.$minor.$micro");
ok(defined $major && defined $minor && defined $micro, "getting api version");

