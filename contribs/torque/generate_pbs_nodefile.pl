#!/usr/bin/perl -w

# Copyright 2013 Brigham Young University
# Written by Ryan Cox <ryan_cox@byu.edu>
#
# Licensed under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option) any later version.
#
# Uses SLURM environment variables to produce a $PBS_NODEFILE -style
#   output file.  The output goes in a temporary file and the name of
#   the file is printed on stdout.  Intended usage is:
#   export PBS_NODEFILE=`generate_pbs_nodefile`

use strict;
use File::Temp qw( tempfile );
use FindBin;
use lib "${FindBin::Bin}/../lib/perl";
use Slurm ':all';

my ($fh, $filename) = tempfile(UNLINK => 0);
die "No SLURM_NODELIST given, run generate_pbs_nodefile inside a "
	. "Slurm allocation or batch script.\n" if (!$ENV{'SLURM_NODELIST'});

my $hl = Slurm::Hostlist::create($ENV{'SLURM_NODELIST'});
my $tasks = $ENV{SLURM_TASKS_PER_NODE};
my @counts = split(",", $tasks);

foreach my $count(@counts) {
	my $ppn;
	my $nodes;
	$count =~ /^(\d+)(\(x(\d+)\))?$/;
	$ppn = $1;
	if ($3) {
		$nodes = $3;
	} else {
		$nodes = 1;
	}
	for (my $j = 0; $j < $nodes; $j++) {
		my $node = Slurm::Hostlist::shift($hl);
		foreach (my $i = 0; $i < $ppn; $i++) {
			print $fh "$node\n";
		}
	}
}
close($fh);

print "$filename\n";
