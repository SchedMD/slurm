#! /usr/bin/perl -w
###############################################################################
#
# bjobs - displays and filters information about jobs in familar
#         openlava format.
#
#
###############################################################################
#  Copyright (C) 2015 SchedMD LLC.
#  Written by Danny Auble <da@schedmd.com>.
#
#  This file is part of SLURM, a resource management program.
#  For details, see <http://slurm.schedmd.com/>.
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
###############################################################################

#use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';

sub print_job_full
{
	my ($job) = @_;
	# Print the job attributes
	printf("Job Id:\t%s\n", $job->{'job_id'});
	printf("\tJob_Name = %s\n", $job->{'name'}) if $job->{'name'};
	printf "\tinteractive = True\n" if !$job->{'batch_flag'};

	printf("\tqueue = %s\n", $job->{'partition'});


	printf("\tAccount_Name = %s\n", $job->{'account'}) if $job->{'account'};

	printf("\tPriority = %u\n", $job->{'priority'});

	# can't run getgrgid inside printf it appears the input gets set to
	# x if ran there.
	my $user_group = getgrgid($job->{'group_id'});
	printf("\tegroup = %s(%d)\n", $user_group, $job->{'group_id'});

	printf("\tResource_List.nodect = %d\n", $job->{'num_nodes'})
		if $job->{'num_nodes'};
	printf("\tResource_List.ncpus = %s\n", $job->{'num_cpus'})
		if $job->{'num_cpus'};

	if ($job->{'reqNodes'}) {
		my $nodeExpr = $job->{'reqNodes'};
		$nodeExpr .= ":ppn=" . $job->{'ntasks_per_node'}
		        if $job->{'ntasks_per_node'};

		printf("\tResource_List.nodes = %s\n", $nodeExpr);
	}

	print "\n";
}

# Parse Command Line Arguments
my ($help,
    $man);

GetOptions(
        'h' => \$help,
        'man'    => \$man,
	) or pod2usage(2);


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
}

my $job_flags = SHOW_ALL | SHOW_DETAIL;
my $resp = Slurm->load_jobs(0, $job_flags);
if(!$resp) {
	die "Problem loading jobs.\n";
}

my $line = 0;
foreach my $job (@{$resp->{job_array}}) {
	my $state = $job->{'job_state'};

	$job->{'aWDuration'} = $job->{'statPSUtl'};

	$job->{'allocNodeList'} = $job->{'nodes'} || "--";
	$job->{'name'} = "Allocation" if !$job->{'name'};

	# Filter jobs according to options and arguments
	if (@job_ids) {
		next unless grep /^$job->{'job_id'}/, @job_ids;
	}

	print_job_full($job);
}

exit 0;

##############################################################################

__END__

=head1 NAME

B<bjobs> - displays and filters information about jobs in familar openlava format.

=head1 SYNOPSIS

B<bjobs> I<job_id>...

=head1 DESCRIPTION

The B<bjobs> displays and filters information about jobs in familar openlava format.

=head1 OPTIONS

=over 4

=item B<-h | --help>

brief help message

=item B<--man>

full documentation

=back

=head1 EXIT STATUS

On success, B<bjobs> will exit with a value of zero. On failure, B<bkill> will exit with a value greater than zero.

=cut

