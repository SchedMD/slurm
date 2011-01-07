#! /usr/bin/perl -w
#
# Convert mshare commands into slurm commands.
#
# Last Update: 2010-12-14
#
# Copyright (C) 2010 Lawrence Livermore National Security.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Philip D. Eckert <eckert2@llnl.gov>
# CODE-OCEC-09-009. All rights reserved.
#

use Getopt::Long 2.24 qw(:config no_ignore_case);
use autouse 'Pod::Usage' => qw(pod2usage);
use strict;

BEGIN {
#
#	Just dump the man page in *roff format and exit if --roff specified.
#
	foreach my $arg (@ARGV) {
		last if ($arg eq "--");
		if ($arg eq "--roff") {
			use Pod::Man;
			my $parser = Pod::Man->new (section => 1);
			$parser->parse_from_file($0, \*STDOUT);
			exit 0;
		}
	}
}

#
# Define the options variables.
#
my ($help, $account, $user, $man, $verbose);

#
# Check SLURM status.
#
isslurmup();

#
# Get input from user.
#
GetOpts();

my $line;
$line =  "sshare -u $user"    if ($user);
$line =  "sshare -a $account" if ($account);

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

#
#	If no arguments, then do info for calling user.
#
	if ($#ARGV < 0) {	
		$user = getpwuid($>);
		return;
	}

	GetOptions (
		'h|help'	=> \$help,
		'man'		=> \$man,
		'u'		=> \$user,
		'a'		=> \$account,
		't'		=> \$account,
		'T'		=> \$account,
		'r'		=> \$account,
	) or pod2usage(2);



#
#	Display man page if requested.
#
	if ($man) {
		if ($< == 0) {   # Cannot invoke perldoc as root
			my $id = eval { getpwnam("nobody") };
			$id = eval { getpwnam("nouser") } unless defined $id;
			$id = -2                          unless defined $id;
			$<  = $id;
		}
		$> = $<;                         # Disengage setuid
		$ENV{PATH} = "/bin:/usr/bin";    # Untaint PATH
		delete @ENV{'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};
		if ($0 =~ /^([-\/\w\.]+)$/) { $0 = $1; }    # Untaint $0
		else { die "Illegal characters were found in \$0 ($0)\n"; }
		pod2usage(-exitstatus => 0, -verbose => 2);
	}

#
#	Show help.
#

	if ($help) {
		pod2usage(-verbose => 0, -exit => 'NOEXIT', -output => \*STDOUT);
		print "Report problems to LC Hotline.\n\n";
		exit(0);
	}

	return;
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


__END__

=head1 NAME

 B<showstart> - This command shows the earliest start time for a queued job.

=head1 SYNOPSIS

 showstart jobid [-h] [--man]

=head1 DESCRIPTION

 B<showstart> - This command shows the earliest start time for a queued job.

=head1 OPTIONS

=over 4

=item B<--help>

Show help.

=item B<--man>

Display man page for this utility.

=back

=head1 REPORTING BUGS

Report problems to LC Hotline.


=cut

