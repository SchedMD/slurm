#! /usr/bin/perl -w
################################################################################
#
# qdel - deletes moab jobs in familar pbs format.
#
#                 Copyright (c) 2006 Cluster Resources, Inc.
#
################################################################################

use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib "${FindBin::Bin}/../tools";
use Moab::Tools;    # Required before including config to set $homeDir
use autouse 'Pod::Usage' => qw(pod2usage);
use XML::LibXML;
BEGIN { require "config.moab.pl"; }
our ($logLevel, $mjobctl);

Main:
{
    logPrint("Command line arguments: @ARGV\n") if $logLevel;

    # Parse Command Line Arguments
    my ($help, $man);
    GetOptions(
        'help|?' => \$help,
        'man'    => \$man,
      )
      or pod2usage(2);

    # Display usage if necessary
    pod2usage(0) if $help;
    if ($man)
    {
        if ($< == 0)    # Cannot invoke perldoc as root
        {
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

    # Use sole remaining argument as jobIds
    my @jobIds;
    if (@ARGV)
    {
        @jobIds = @ARGV;
    }
    else
    {
        pod2usage(2);
    }

    # Build command
    my $cmd = "$mjobctl -c " . join ' ', @jobIds;
    logPrint("Invoking subcommand: $cmd\n") if $logLevel >= 2;

    my $output = `$cmd 2>&1`;
    my $rc     = $?;

    # Display and log error output
    if ($output =~ /invalid job specified \(([^\(\)]+)\)/)
    {
        logWarn("qdel: Unknown Job Id $1\n");
    }
    elsif ($rc)
    {
        logWarn($output);
    }
    else
    {
        logPrint("Subcommand output:>\n$output") if $logLevel >= 3;
    }

    exit $rc;
}

##############################################################################

__END__

=head1 NAME

B<qdel> - deletes jobs in a familiar pbs format

=head1 SYNOPSIS

B<qdel> I<job_id>...

=head1 DESCRIPTION

The B<qdel> command cancels the specified jobs.

=head1 OPTIONS

=over 4

=item B<-? | --help>

brief help message

=item B<--man>

full documentation

=back

=head1 EXIT STATUS

On success, B<qdel> will exit with a value of zero. On failure, B<qdel> will exit with a value greater than zero.

=cut

