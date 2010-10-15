#! /usr/bin/perl -w
###############################################################################
#
# qhold - places a hold on slurm jobs in familar pbs format.
#
#                 Copyright (c) 2006 Cluster Resources, Inc.
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
    my ($help, $hold, $man);
    GetOptions(
        'help|?' => \$help,
        'h=s'      => \$hold,
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

    my $reason = "user held (qhold)";

    if($hold) {
	    switch($hold) {
		    case ['u'] { $reason = "user held (qhold)" }
		    case ['s'] { $reason = "system held (qhold)" }
		    case ['o'] { $reason = "batch held (qhold)" }
		    case ['n'] { $reason = "" }
	    }
    }

    my $rc = 0;
    foreach my $jobid (@jobIds) {
	    my $err = 0;
	    my $resp = 0;
	    my %update = ();

	    $update{job_id} = $jobid;
	    if($reason) {
		    $update{priority} = 0;
		    $update{comment} = $reason; #doesn't do anything in 1.2
	    } else {
		    $update{priority} = -1;
		    $update{comment} = "None"; #doesn't do anything in 1.2
	    }

	    if(Slurm->update_job(\%update)) {
		    $err = Slurm->get_errno();
		    $rc++;
		    printf("qhold: Error on job id %d: %s\n",
			   $jobid, Slurm->strerror($err));
	    }
    }
    exit $rc;
}

##############################################################################

__END__

=head1 NAME

B<qhold> - places a hold on jobs in a familiar pbs format

=head1 SYNOPSIS

B<qhold> [B<-h> B<u>|B<o>|B<s>|B<n>] I<job_id>...

=head1 DESCRIPTION

The B<qhold> command requests that a hold be placed on a job.  A job that is on hold is not eligible for execution.  There are three supported holds: USER, OTHER (also known as operator or batch), and SYSTEM.

If the B<-h> option is not specified, the USER hold will be applied to the specified jobs.

=head1 OPTIONS

=over 4

=item B<-h> I<hold_type>

Specifieds the types of holds to be placed on the job.

The I<hold_type> argument is a one of the characters "u", "o", "s" or "n".  The hold type associated with each letter is:

    B<u> - USER

    B<o> - OTHER

    B<s> - SYSTEM

    B<n> - None

=item B<-? | --help>

brief help message

=item B<--man>

full documentation

=back

=head1 EXIT STATUS

On success, B<qhold> will exit with a value of zero. On failure, B<qhold> will exit with a value greater than zero.

=cut

