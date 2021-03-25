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
#  Additions by Troy Baer <tbaer@utk.edu>
#
#  This file is part of Slurm, a resource management program.
#  For details, see <https://slurm.schedmd.com/>.
#  Please also read the included file: DISCLAIMER.
#
#  Slurm is free software; you can redistribute it and/or modify it under
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
#  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#
#  You should have received a copy of the GNU General Public License along
#  with Slurm; if not, write to the Free Software Foundation, Inc.,
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

Main:
{
    # Parse Command Line Arguments
    my ($all, $clear, $help, $man, $shortlist, $printnote, $offline, $reset, $setnote);
    GetOptions(
        'all|a'       => \$all,
	'clear|c'     => \$clear,
        'help|?'      => \$help,
        'list|l'      => \$shortlist,
        'man'         => \$man,
        'note|n'      => \$printnote,
	'offline|o'   => \$offline,
        'reset|r'     => \$reset,
        'setnote|N=s' => \$setnote
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
    my $slurm = Slurm::new();
    if (!$slurm) {
        die "Problem loading slurm.\n";
    }

    # handle all of the node update operations
    if ( defined $clear || defined $offline || defined $reset ) {
           my $oprc = 0;
           foreach my $node (@nodeIds) {
	           my $nodestate;
		   if ( defined $clear || defined $reset ) {
                           $nodestate = {node_names=>$node,node_state=>NODE_RESUME};
		   } elsif ( defined $offline ) {
                           $nodestate = {node_names=>$node,node_state=>NODE_STATE_DRAIN};
		   }
		   if ( defined $setnote ) {
		           $nodestate->{'reason'} = $setnote;
		   }
                   my $rc = $slurm->update_node($nodestate);
                   if ( $rc!=0 ) {
                           $oprc += $rc;
                   }
           }
           exit($oprc);
    }

    # if we've gotten to this point, we're doing some kind of list operation
    my $resp = $slurm->load_node(0, SHOW_ALL);
    if(!$resp) {
	    die "Problem loading node.\n";
    }

    my $update = $resp->{last_update};
    foreach my $node (@{$resp->{node_array}}) {
            #print STDERR join(",",keys($node))."\n";
	    next unless (defined $node);
	    next unless (keys %{$node});
	    my $nodeId    = $node->{'name'};
	    my $rCProc    = $node->{'cpus'};
	    my $rBoards   = $node->{'boards'};
	    my $rSockets  = $node->{'sockets'};
            my $rCores    = $node->{'cores'};
            my $rThreads  = $node->{'threads'};
	    my $features  = $node->{'features'};
	    my $rAMem     = $node->{'real_memory'};
	    my $rAProc    = ($node->{'cpus'} - $node->{'alloc_cpus'});
	    my $state     = lc(Slurm->node_state_string($node->{'node_state'}));
            my $reason    = $node->{'reason'};
	    my $gres      = $node->{'gres'};
	    if ( !defined $node->{'os'} ) {
		$node->{'os'} = "unknown";
	    }
	    my $os        = lc($node->{'os'});
	    my $arch      = $node->{'arch'};
	    my $disksize  = $node->{'tmp_disk'};

	    # deal w/ specific types of gres
	    my $gpus = 0;
	    my $mics = 0;
	    if ( defined $gres ) {
                  my @gres = split(/,/,$gres);
                  foreach my $grestype ( @gres ) {
			  $grestype =~ s/\([^)]*\)//g;
                          my @elt = split(/:/,$grestype);
			  if ( $#elt>0 && $elt[0] eq "gpu" ) {
				if ( $elt[1] =~ /^[0-9]+$/ ) {
					$gpus += int($elt[1]);
			        } else {
					$gpus += int($elt[2]);
				}
			  }
			  if ( $#elt>0 && $elt[0] eq "mic" ) {
				if ( $elt[1] =~ /^[0-9]+$/ ) {
					$mics += int($elt[1]);
			        } else {
					$mics += int($elt[2]);
				}

			  }
		  }
	    }

            # find job(s) on node
	    my $jobs;
	    if ( $state eq "allocated" ) {
                    # how to get list of jobs on node efficiently?
            }

            # this isn't really defined in Slurm, so I am not sure how to get it
	    my $load;

	    # mangle Slurm states into PBS equivs
	    my $pbsstate = $state;
	    $pbsstate =~ s/drained/offline/g;
	    $pbsstate =~ s/idle/free/g;
            $pbsstate =~ s/\*//g;
	    if ( $state eq "allocated" ) {
	            if ( $rAProc>0 ) {
                            $pbsstate = "busy";
		    } else {
                            $pbsstate = "job-exclusive";
		    }
	    }

	    # Filter nodes according to options and arguments
	    if (@nodeIds) {
		    next unless grep /^$nodeId$/, @nodeIds;
	    }

            if ( !defined($shortlist) ) {
	            # Prepare variables
	            my @status = ();
		    push @status, "rectime=$update" if defined $update;
		    push @status, "jobs=$jobs" if defined $jobs;
		    push @status, "state=$pbsstate" if defined $pbsstate;
	            push @status, "slurmstate=$state" if defined $state;
		    push @status, "size=".(int($disksize)*1024)."kb:".(int($disksize)*1024)."kb" if defined $disksize;
		    push @status, "gres=$gres" if defined $gres;
		    push @status, "message=\"$reason\"" if defined $reason;
	            push @status, "loadave=" . sprintf("%.2f", $load) if defined $load;
	            push @status, "ncpus=${rCProc}" if defined $rCProc;
		    push @status, "boards=${rBoards}" if defined $rBoards;
		    push @status, "sockets=${rSockets}" if defined $rSockets;
	            push @status, "cores=${rCores}" if defined $rCores;
		    push @status, "threads=${rThreads}" if defined $rThreads;
	            push @status, "availmem=${rAMem}mb" if defined $rAMem;
         	    push @status, "opsys=$os" if defined $os;
	            push @status, "arch=$arch" if defined $arch;

         	    # Print the node attributes
    	            printf "%s\n",             $nodeId;
	            printf "    state = %s\n", $pbsstate;
	            printf "    np = %s\n", $rCProc if $rCProc;
	            printf "    properties = %s\n", join(' ', split(/:/, $features))
		      if $features;
                    printf "    ntype = cluster\n";
	            printf "    status = %s\n", join(',', @status) if @status;
		    printf "    note = %s\n", $reason if defined $reason;
		    printf "    gpus = %d\n", $gpus if $gpus>0;
		    printf "    mics = %d\n", $mics if $mics>0;
	            print "\n";
	     } else {
	            if ( $state =~ /drained|down/i ) {
                            printf "%s\t\t%s",$nodeId,$pbsstate;
                            printf "\t\t%s",$reason if ( defined $printnote && defined $reason );
                            print "\n";
                    }
             }
    }

    # Exit with status code
    exit 0;
}


##############################################################################

__END__

=head1 NAME

B<pbsnodes> - display and manipulate host information in a PBS-like format

=head1 SYNOPSIS

B<pbsnodes> [B<-a>] [I<node_id>...]

B<pbsnodes> B<-l> [B<-n>]

B<pbsnodes> B<-{c|r|o}> [I<node_id>...] [ B<-N> "note/reason string"]

=head1 DESCRIPTION

The B<pbsnodes> command displays and manipulates information about
nodes.

=head1 OPTIONS

=over 4

=item B<-a>

Display information for all nodes. This is the default if no node name
is specified.

=item B<-c>

Clear OFFLINE from listed nodes.

=item B<-l>

List node names and their state for nodes that are DOWN, OFFLINE, or
UNKNOWN.

=item B<-N>

Specify a "note/reason" attribute.  Use "" to clear field.

=item B<-n>

Show the "note/reason" attribute for nodes that are DOWN, OFFLINE, or
UNKNOWN.  This option requires B<-l>.

=item B<-r>

Reset the listed nodes by clearing OFFLINE.  Functionally equivalent
to B<-c>.

=item B<-? | --help>

brief help message

=item B<--man>

full documentation

=back

=cut

