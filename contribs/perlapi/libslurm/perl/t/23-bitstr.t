#!/usr/bin/perl -T
use Test::More tests => 37;
use Slurm qw(:constant);


my ($bm, $bm2, $rc, $cnt, $pos, $sum, $size, $ia, $str);

# 1
$bm = Slurm::Bitstr::alloc(32);
ok(ref($bm) eq "Slurm::Bitstr", "bit alloc");


# 2
#$bm->realloc(32);
#ok($bm->size() == 32, "bit realloc");


# 3
$bm2 = $bm->copy();
ok(ref($bm2) eq "Slurm::Bitstr", "bit copy");


# 4
$rc = $bm->test(13);
ok (!$rc, "bit test");


# 5
$bm->set(13);
$rc = $bm->test(13);
ok ($rc, "bit set");


# 6
$bm->clear(13);
$rc = $bm->test(13);
ok (!$rc, "bit clear");


# 7
$bm->nset(13, 28);
$rc = $bm->test(16);
ok($rc, "bit nset");


#8
$bm->nclear(22, 30);
$rc = $bm->test(26);
ok(!$rc, "bit nset");


# $bm fmt: "13-21"
# $bm2 fmt: ""

# 9
$pos = $bm->ffc();
ok($pos == 0, "bit ffc") or diag("ffc: $pos");


# 10
$pos = $bm->ffs();
ok($pos == 13, "bit ffs") or diag("ffs: $pos");


# 11
$pos = $bm->fls();
ok($pos == 21, "bit fls") or diag("fls: $pos");


# 12
$pos = $bm->nffc(3);
ok($pos == 0, "bit nffc") or diag("nffc: $pos");


# 13
$pos = $bm->nffs(20);
ok($pos == -1, "bit nffs") or diag("nffs: $pos");


# 14
$pos = $bm->noc(5, 16);
ok($pos == 22, "bit noc") or diag("noc: $pos");


# 15
$size = $bm->size();
ok($size == 32, "bit size") or diag("size: $size");


# 16
$bm->and($bm2);
$cnt = $bm->set_count();
ok($cnt == 0, "bit and") or diag("and: $cnt");


# 17
$bm->not();
$cnt = $bm->set_count();
ok($cnt == 32, "bit not") or diag("not: $cnt");


# 18
$bm->nclear(16, 31);
$bm2->nset(16, 23);
$bm->or($bm2);
$cnt = $bm->set_count();
ok($cnt == 24, "bit or") or diag("or: $cnt");

# $bm2 fmt: "16-23"

# 19
$bm->copybits($bm2);
$cnt = $bm->set_count();
ok($cnt == 8, "bit copybits") or diag("copybits: $cnt");


# 20
$cnt = $bm->set_count();
ok($cnt == 8, "bit set count") or diag("set_count: $cnt");


# 21
$cnt = $bm->clear_count();
ok($cnt == 24, "bit clear count") or diag("clear_count: $cnt");


# 22
$cnt = $bm->nset_max_count();
ok($cnt == 8, "bit nset max count") or diag("nset_max_count: $cnt");

# $bm fmt: "16-23"

# 24
$bm2 = $bm->rotate_copy(16, 40);
$size = $bm2->size();
$pos = $bm2->ffs();
ok($size == 40 && $pos == 32, "bit rotate copy") or diag("rotate_copy: $size, $pos");


# 25
$bm->rotate(-8);
$pos = $bm->ffs();
ok($pos == 8, "bit rotate") or diag("rotate: $pos");

# $bm fmt: "8-15"

# 26
$str = $bm->fmt();
ok ($str eq "8-15", "bit fmt") or diag("fmt: $str");


# 27
$bm->unfmt("16-23");
$rc = $bm->test(13);
ok (!$rc, "bit unfmt");

# $bm fmt: "16-23"

# 28
$ia = Slurm::Bitstr::fmt2int($str);
$size = @$ia;
ok($size == 2 && $ia->[0] == 8 && $ia->[1] == 15, "bit fmt2int") or diag("fmt2int: $size, $ia->[0], $ia->[1]");


# 29
$str = $bm->fmt_hexmask();
ok($str eq "0x00FF0000", "bit fmt hexmask") or diag("fmt_hexmask: $str");


# 30
$rc = $bm->unfmt_hexmask("0x000000F0");
$cnt = $bm->set_count();
ok($rc == 0 && $cnt == 4, "bit unfmt hexmask") or diag("unfmt_hexmask: $rc, $cnt");

# $bm fmt: "4-7"

# 31
$str = $bm->fmt_binmask();
ok($str eq "00000000000000000000000011110000", "bit fmt binmask") or diag("fmt_binmask: $str");


# 32
$bm->unfmt_binmask("0000000111111110000000011110001");
$cnt = $bm->set_count();
ok($cnt == 13, "bit unfmt binmask") or diag("unfmt_binmask: $cnt");


# $bm fmt: "0-0,4-7,16-23"

# 33
$bm->fill_gaps();
$cnt = $bm->set_count();
ok($cnt == 24, "bit fill gaps"), or diag("fill_gaps: $cnt");


# $bm fmt: "0-23"

# 34
$bm2 = $bm->rotate_copy(16, 32);
$rc = $bm->super_set($bm2);
ok (!$rc, "bit super set") or diag("super_set: $rc");

# $bm fmt: "0-23"
# $bm2 fmt: "0-7,16-31"

# 35
$cnt = $bm->overlap($bm2);
ok($cnt == 16, "bit overlap") or diag("overlap: $cnt");


# 36
$rc = $bm->equal($bm2);
ok(!$rc, "bit equal") or diag("equal: $rc");


# 37
$bm2 = $bm->pick_cnt(8);
ok($bm2 && $bm2->set_count() == 8, "pick cnt") or diag("pick_cnt: $cnt");


# 38
$bm->unfmt("3-5,12-23");
$pos = $bm->get_bit_num(8);
ok($pos = 17, "bit get bit num") or diag("get_bit_num: $pos");

# 39
$bm->unfmt("3-5,12-23");
$cnt = $bm->get_pos_num(12);
ok($cnt == 3, "bit get pos num") or diag("get_pos_num: $cnt");

