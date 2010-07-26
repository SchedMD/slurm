#! /usr/bin/perl
#
# Convert showstart commands into slurm commands.
#
# Author:Phil Eckert
# Date: 07/11/2010
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
my ($help, $resid, $man, $verbose);

#
# Get input from user.
#
GetOpts();


my $line = "scontrol show res";
$line .= " $resid" if ($resid);

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
		'man'		=> \$man,
		'r'		=> \$resid,
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
# Simple check to see if number is an integer,
# retrun 0 if it is not, else return 1.
#
sub isnumber
{
	my ($var) = $_;

	if ($var !~ /\D+/) {
		return(1); #if it is just a number.
	} else {
		return(0); #if it is not just a number.
	}
}


__END__

=head1 NAME

 B<showres> - This command shows reservation information.

=head1 SYNOPSIS

 showres  [-r reservationid] [-h] [--man]

=head1 DESCRIPTION

 B<showres> - This command shows reservation information.

=head1 OPTIONS

=over 4

=item B<--help>

Show help.

=item B<--man>

Display man page for this utility.

=item B<-r> I<reservationid>

The reservation id name.

=back

=head1 REPORTING BUGS

Report problems to LC Hotline.


=cut

