#! /usr/bin/perl
#
# Convert releasehold commands into slurm commands.
#
# Author:Phil Eckert
# Date: 2010-07-20
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

my $line = "scontrol release $jobid";
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
Usage: releasehold [FLAGS] <JOB_ID>
  --help

          [ -a ] - All hold types (disabled)
          [ -b ] - Batch hold (disabled)
          [ -s ] - System hold (disabled)
          [ -u ] - User hold (disabled)
		\n");

	exit(0);
}

