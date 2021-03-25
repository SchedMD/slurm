#! /usr/bin/perl -w
###############################################################################
#
#  qrerun - PBS wrapper to cancel and resubmit a job
#
###############################################################################
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
use Slurmdb ':all'; # needed for getting the correct cluster dims

# Parse Command Line Arguments
my (
	$help, $man,
	$err, $pid, $resp
);

GetOptions(
	'help|?'    => \$help,
	'--man'     => \$man,
	) or pod2usage(2);

pod2usage(2) if $help;
# Handle man page flag
if ($man)
{
	if ($< == 0)    # Cannot invoke perldoc as root
	{
		my $id = eval { getpwnam("nobody") };
		$id = eval { getpwnam("nouser") } unless defined $id;
		$id = -2			  unless defined $id;
		$<  = $id;
	}
	$> = $<;			 # Disengage setuid
	$ENV{PATH} = "/bin:/usr/bin";    # Untaint PATH
	delete @ENV{'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};
	if ($0 =~ /^([-\/\w\.]+)$/) { $0 = $1; }    # Untaint $0
	else { die "Illegal characters were found in \$0 ($0)\n"; }
	pod2usage(-exitstatus => 0, -verbose => 2);
}


# This makes the assumption JOBID will always be the last argument
my $job_id = $ARGV[$#ARGV];

if (@ARGV < 1) {
	pod2usage(-message=>"Invalid Argument", -verbose=>1);
	exit(1);
}

if (Slurm->requeue($job_id)) {
	$err = Slurm->get_errno();
	$resp = Slurm->strerror($err);
	pod2usage(-message=>"Job id $job_id rerun error: $resp", -verbose=>0);
	exit(1);
}
exit(0);

__END__

=head1 NAME

B<qrerun> -  To rerun a job is to terminate the job and return the job to the queued state in the execution queue in which the job currently resides.
If a job is marked as not rerunable then the rerun request will fail for that job.

See the option on the qsub and qalter commands.

It is aimed to be feature-compatible with PBS' qsub.

=head1 SYNOPSIS

B<qrerun> [-? | --help] [--man] [--verbose] <job_id>

=head1 DESCRIPTION

The B<qrerun> command directs that the specified job is to be rerun if possible.

=head1 OPTIONS

=over 4

=item B<-? | --help>

a brief help message

=item B<--man>

full documentation

=back

=head1 EXIT STATUS

On success, B<qrerun> will exit with a value of zero. On failure, B<qrerun> will exit with a value greater than zero.

=head1 SEE ALSO

qalter(1) qsub(1)
=cut
