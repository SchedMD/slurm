#! /usr/bin/perl -w
###############################################################################
#
# bkill - deletes slurm jobs in familar openlava format.
#
#
###############################################################################
#  Copyright (C) 2015 SchedMD LLC.
#  Written by Danny Auble <da@schedmd.com>.
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
###############################################################################

#use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use POSIX qw(:signal_h);

# Parse Command Line Arguments
my ($help,
    $man);

GetOptions(
        'h' => \$help,
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

# Use sole remaining argument as job_ids
my @job_ids;
if (@ARGV) {
        @job_ids = @ARGV;
} else {
        pod2usage(2);
}

# OpenLava goes through the list beforehand verifying before it processes
# and exits on first error.
foreach my $jobid (@job_ids) {
	if ($jobid !~ /^\d+$/) {
		printf("%s: Illegal job ID.\n", $jobid);
		exit 1;
	}
}

my $rc = 0;
foreach my $jobid (@job_ids) {
	my $err = 0;
	my $resp = 0;

	for (my $i=0; $i<3; $i++) {
		$resp = Slurm->kill_job($jobid, SIGKILL);
		$err = Slurm->get_errno();
		if($resp == SLURM_SUCCESS
		   || ($err ne ESLURM_TRANSITION_STATE_NO_UPDATE
		       && $err ne ESLURM_JOB_PENDING)) {
			last;
		}
	}
	if ($resp == SLURM_ERROR) {
		$rc++;
		printf("Job <%s>: %s\n", $jobid, Slurm->strerror($err));
	} else {
		printf("Job <%s> is being terminated\n", $jobid);
	}
}
exit $rc;

##############################################################################

__END__

=head1 NAME

B<bkill> - deletes jobs in a familiar openlava format

=head1 SYNOPSIS

B<bkill> I<job_id>...

=head1 DESCRIPTION

The B<bkill> command cancels the specified jobs.

=head1 OPTIONS

=over 4

=item B<-h | --help>

brief help message

=item B<--man>

full documentation

=back

=head1 EXIT STATUS

On success, B<bkill> will exit with a value of zero. On failure, B<bkill> will exit with a value greater than zero.

=cut

