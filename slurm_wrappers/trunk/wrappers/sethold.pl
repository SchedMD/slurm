#! /usr/bin/perl -w
#
# Convert sethold commands into slurm commands.
#
# Last Update: 2010-12-14
#
# Copyright (C) 2010 Lawrence Livermore National Security.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Philip D. Eckert <eckert2@llnl.gov>
# CODE-OCEC-09-009. All rights reserved.
#

#
# For debugging.
#
my $debug=0;

use Getopt::Long 2.24 qw(:config no_ignore_case);
use strict;

#
# Define the options variables.
#
my ($help, $jobid, $all, $batch, $system, $user);

#
# Check SLURM status.
#
isslurmup();

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);
if ($sversion < 2.2) {
	printf("\n Hold/Release functionality not available in this release.\n\n");
	exit(1);
}

#
# Get input from user.
#
GetOpts();

my $line = "scontrol hold $jobid";
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

	GetOptions (
		'h|help'	=> \$help,
		'a'		=> \$all,
		'b'		=> \$batch,
		's'		=> \$system,
		'u'		=> \$user,
	) or help();
#
#	Show help.
#

	help() if ($help);

#
#	Make sure we have a job id.
#
	if (!($jobid = shift(@ARGV))) {
		printf("\n Job Id needed.\n\n"); 
		pod2usage(2);
	}

	return;
}

sub help
{

	printf("\
Usage: sethold [FLAGS] <JOB_ID>
  --help

          [ -a ] - All hold types (disabled)
          [ -b ] - Batch hold (disabled)
          [ -s ] - System hold (disabled)
          [ -u ] - User hold (disabled)
	\n");

	exit(0);
}


#
# Determine if SLURM is available.
#
sub isslurmup
{
	my $out = `scontrol show part 2>&1`;
	if ($?) {
		printf("\n SLURM is not communicating.\n\n");
		exit(1);
	}

	return;
}
