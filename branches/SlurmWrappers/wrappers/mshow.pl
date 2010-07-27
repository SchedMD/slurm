#! /usr/bin/perl
#
# Convert mshow commands into slurm commands.
#
# Last Update: 2010-07-27
#
# Copyright (C) 2010 Lawrence Livermore National Security.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Philip D. Eckert <eckert2@llnl.gov>
# CODE-OCEC-09-009. All rights reserved.
#

#
# For debugging.
#
my $debug=1;

use Getopt::Long 2.24 qw(:config no_ignore_case);
use strict;


#
# Define the options variables.
#
my ($help, $showq, $showbf, $unused);

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);

#
# Get input from user.
#
GetOpts();

my $line;
if ($debug == 1) {
	$line = "showq.pl" if (defined $showq);
	$line = "showbf.pl" if (defined $showbf);
} else {
	$line = "showq" if (defined $showq);
	$line = "showbf" if (defined $showbf);
}

my $result = `$line 2>&1`;
my $status = $?;

printf("\n$result\n");

exit($status);


#
# Use arrays, when appropriate, for the options, since somemany
# will have two or more elements.
# 
sub GetOpts
{
	$showq = 1if ($#ARGV == -1);

	GetOptions (
		'h|help'	=> \$help,
		'a'		=> \$showbf,
		'r'		=> \$showbf,
		'i'		=> \$showq,
		'o'		=> \$showq,
		'p:s'		=> \$showq,
		'q:s'		=> \$showq,
		'T'		=> \$showq,
		'w:s'		=> \$unused,
		'x'		=> \$showq,
	) or help();

#
#	Show help.
#
	help() if ($help);

	return;
}


sub help
{

	printf("\
Usage: mshow [FLAGS] 
  --help

	The options below return output as close
	as possible to mshow under Moab give the
	vast difference between the two. However,
	be advised that showq and showbf have
	replaced this utility and are probably
	a better choice for your use.

	-a - Available Resources
	-i - Intersect Available Resource Query
	-o - No Aggregration Of Available Resource Query
	-p - Available Resource QuerY
	-q - diag | timeline ] - Queue
	-r - Reservation
	-T - Timelock Available Resources
	-x - Exclusive Resources

	\n");

	exit(0);
}

