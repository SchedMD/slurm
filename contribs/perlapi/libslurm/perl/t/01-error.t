#!/usr/bin/perl -T
use Test::More tests => 4;
use Slurm qw(:constant);


# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my $errno = $slurm->get_errno();
ok(defined $errno, "get error number");


# 3
my $msg = $slurm->strerror();
ok(defined $msg, "get default error string");


# 4
my $errmsg = $slurm->strerror(SLURM_NO_CHANGE_IN_DATA);
ok($errmsg eq "Data has not changed since time specified", "get specified error string");
