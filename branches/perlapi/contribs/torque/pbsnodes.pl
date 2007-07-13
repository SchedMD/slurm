#! /usr/bin/perl -w
################################################################################
#
# pbsnodes - queries moab nodes in familar pbs format.
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
our ($logLevel, $mdiag);

Main:
{
    logPrint("Command line arguments: @ARGV\n") if $logLevel;

    # Parse Command Line Arguments
    my ($all, $help, $man);
    GetOptions(
        'a'      => \$all,
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

    # Use sole remaining argument as nodeIds
    my @nodeIds = @ARGV;

    # Build command
    my $cmd = "$mdiag -n --format=xml";
    logPrint("Invoking subcommand: $cmd\n") if $logLevel >= 2;

    my $output = `$cmd 2>&1`;
    my $rc     = $?;

    if ($rc)
    {
        logDie("Unable to query moab nodes ($cmd) [rc=$rc]: $output\n");
    }

    logPrint("Subcommand output:>\n$output") if $logLevel >= 3;

    # Parse the response
    $output =~ s/^\s+//;    # Remove leading whitespace
    $output =~ s/\s+$//;    # Remove trailing whitespace
    my $parser = new XML::LibXML();
    my $doc    = $parser->parse_string($output);
    my $root   = $doc->getDocumentElement();

    my @nodes     = ();
    my @nodeNodes = $root->getChildrenByTagName("node");
    foreach my $nodeNode (@nodeNodes)
    {
        my %nodeAttr  = ();
        my @nodeAttrs = $nodeNode->attributes();
        foreach my $attr (@nodeAttrs)
        {
            my $name  = $attr->nodeName;
            my $value = $attr->nodeValue;
            $nodeAttr{$name} = $value;
        }

        my @xloads     = ();
        my @xloadNodes = $nodeNode->getChildrenByTagName("XLOAD");
        foreach my $xloadNode (@xloadNodes)
        {
            my %xloadAttr  = ();
            my @xloadAttrs = $xloadNode->attributes();
            foreach my $attr (@xloadAttrs)
            {
                my $name  = $attr->nodeName;
                my $value = $attr->nodeValue;
                $xloadAttr{$name} = $value;
            }
            push @xloads, \%xloadAttr;
        }
        $nodeAttr{'xloads'} = \@xloads;
        push @nodes, \%nodeAttr;
    }

    # Display output
    exit 0 unless @nodes;

    foreach my $node (@nodes)
    {
        my $nodeId    = $node->{'NODEID'};
        my $rCProc    = $node->{'RCPROC'};
        my $nodeState = $node->{'NODESTATE'};
        my $features  = $node->{'FEATURES'};
        my $os        = $node->{'OS'};
        my $load      = $node->{'LOAD'};
        my $arch      = $node->{'ARCH'};
        my $rAMem     = $node->{'RAMEM'};
        my $rAProc    = $node->{'RAPROC'};

        # Filter nodes according to options and arguments
        if (@nodeIds)
        {
            next unless grep /^$nodeId/, @nodeIds;
        }

        # Prepare variables
        my $state = "unknown";
        if    ($nodeState =~ /Down/i)              { $state = "down"; }
        elsif ($nodeState =~ /Busy|Idle|Running/i) { $state = "free"; }
        elsif ($nodeState =~ /Draining/i)          { $state = "offline"; }
        elsif ($nodeState =~ /Unknown/i)           { $state = "unknown"; }
        my @status = ();
        push @status, "opsys=$os" if $os;
        push @status, "loadave=" . sprintf("%.2f", $load) if defined $load;
        push @status, "state=$state";

        # Print the node attributes
        printf "%s\n",             $nodeId;
        printf "    state = %s\n", $state;
        printf "    pcpus = %s\n", $rCProc if $rCProc;
        printf "    properties = %s\n", join(' ', split(/:/, $features))
          if $features;
        printf "    status = %s\n", join(',', @status) if @status;
        printf "    resources_available.arch = %s\n", $arch if $arch;
        printf "    resources_available.mem = %smb\n", $rAMem if $rAMem;
        printf "    resources_available.ncpus = %s\n", $rAProc if $rAProc;
        print "\n";
    }

    # Exit with status code
    exit 0;
}

##############################################################################

__END__

=head1 NAME

B<pbsnodes> - display host information in a familiar pbs format

=head1 SYNOPSIS

B<pbsnodes> [B<-a>] [I<node_id>...]

=head1 DESCRIPTION

The B<pbsnodes> command displays information about nodes.

=head1 OPTIONS

=over 4

=item B<-a>

Display information for all nodes. This is the default if no node name is specified.

=item B<-? | --help>

brief help message

=item B<--man>

full documentation

=back

=cut

