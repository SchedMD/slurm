#! /usr/bin/perl -w
###############################################################################
#
# pbsnodes - queries slurm nodes in familar pbs format.
#
#
###############################################################################
#  Copyright (C) 2007 The Regents of the University of California.
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Danny Auble <auble1@llnl.gov>.
#  CODE-OCEC-09-009. All rights reserved.
#  
#  This file is part of SLURM, a resource management program.
#  For details, see <https://computing.llnl.gov/linux/slurm/>.
#  Please also read the included file: DISCLAIMER.
#  
#  SLURM is free software; you can redistribute it and/or modify it under
#  the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 2 of the License, or (at your option)
#  any later version.
#
#  In addition, as a special exception, the copyright holders give permission 
#  to link the code of portions of this program with the OpenSSL library under
#  certain conditions as described in each individual source file, and 
#  distribute linked combinations including the two. You must obey the GNU 
#  General Public License in all respects for all of the code used other than 
#  OpenSSL. If you modify file(s) with this exception, you may extend this 
#  exception to your version of the file(s), but you are not obligated to do 
#  so. If you do not wish to do so, delete this exception statement from your
#  version.  If you delete this exception statement from all source files in 
#  the program, then also delete it here.
#  
#  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#  
#  You should have received a copy of the GNU General Public License along
#  with SLURM; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
#  
#  Based off code with permission copyright 2006, 2007 Cluster Resources, Inc.
###############################################################################


use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Switch;

Main:
{
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


    my $resp = Slurm->load_node(1);
    if(!$resp) {
	    die "Problem loading jobs.\n";
    }

     
    foreach my $node (@{$resp->{node_array}}) {
	    my $nodeId    = $node->{'name'};
	    my $rCProc    = $node->{'cpus'};
	    my $features  = $node->{'features'};
	    my $rAMem     = $node->{'real_memory'};
	    my $rAProc    = ($node->{'cpus'}) - ($node->{'used_cpus'});
	    my $state = lc(Slurm::node_state_string($node->{'node_state'}));

#these aren't really defined in slurm, so I am not sure what to get them from
	    my $os        = $node->{'OS'};
	    my $load      = $node->{'LOAD'};
	    my $arch      = $node->{'ARCH'};
#
	    
	    # Filter nodes according to options and arguments
	    if (@nodeIds) {
		    next unless grep /^$nodeId/, @nodeIds;
	    }
	    
	    # Prepare variables
	    
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
	    printf "    resources_available.mem = %smb\n", $rAMem if defined $rAMem;
	    printf "    resources_available.ncpus = %s\n", $rAProc if defined $rAProc;
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

