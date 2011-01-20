#!/usr/bin/perl -T
use Test::More tests => 8;
use Slurm qw(:constant);


my $hostnames="node0,node3,node4,node8,linux,linux2,linux5,node4";


# 1
my $hl = Slurm::Hostlist::create($hostnames);
ok(ref($hl) eq "Slurm::Hostlist", "hostlist create");


# 2
my $cnt = $hl->count();
ok ($cnt == 8, "hostlist count");


# 3
my $pos = $hl->find("linux");
ok ($pos == 4, "hostlist find");


# 4
$cnt = $hl->push("node12,node15,linux8");
ok ($cnt == 3, "hostlist push");


# 5
$cnt = $hl->push_host("linux23");
ok ($cnt == 1, "hostlist push host");


# 6
my $str = $hl->ranged_string();
ok($str eq "node[0,3-4,8],linux,linux[2,5],node[4,12,15],linux[8,23]", "hostlist ranged string") or diag("ranged_string: $str");


#7
my $hn = $hl->shift();
ok($hn eq "node0", "hostlist shift");


# 8
$hl->uniq();
$cnt = $hl->count();
# total 12, one duplicate, one shifted
ok($cnt == 10, "hostlist uniq") or diag("count after uniq: $cnt");
