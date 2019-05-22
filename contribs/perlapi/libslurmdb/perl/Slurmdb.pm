package Slurmdb;

use 5.008;
use strict;
use warnings;
use Carp;

require Exporter;
use AutoLoader;

our @ISA = qw(Exporter);

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.

# This allows declaration	use Slurmdb ':all';
# If you do not need this, moving things directly into @EXPORT or @EXPORT_OK
# will save memory.
our %EXPORT_TAGS = ( 'all' => [ qw(
	SLURMDB_ADD_ASSOC
	SLURMDB_ADD_COORD
	SLURMDB_ADD_QOS
	SLURMDB_ADD_USER
	SLURMDB_ADD_WCKEY
	SLURMDB_ADMIN_NONE
	SLURMDB_ADMIN_NOTSET
	SLURMDB_ADMIN_OPERATOR
	SLURMDB_ADMIN_SUPER_USER
	SLURMDB_CLASSIFIED_FLAG
	SLURMDB_CLASS_BASE
	SLURMDB_CLASS_CAPABILITY
	SLURMDB_CLASS_CAPACITY
	SLURMDB_CLASS_CAPAPACITY
	SLURMDB_CLASS_NONE
	SLURMDB_EVENT_ALL
	SLURMDB_EVENT_CLUSTER
	SLURMDB_EVENT_NODE
	SLURMDB_MODIFY_ASSOC
	SLURMDB_MODIFY_QOS
	SLURMDB_MODIFY_USER
	SLURMDB_MODIFY_WCKEY
	SLURMDB_PROBLEM_ACCT_NO_ASSOC
	SLURMDB_PROBLEM_ACCT_NO_USERS
	SLURMDB_PROBLEM_NOT_SET
	SLURMDB_PROBLEM_USER_NO_ASSOC
	SLURMDB_PROBLEM_USER_NO_UID
	SLURMDB_PURGE_ARCHIVE
	SLURMDB_PURGE_BASE
	SLURMDB_PURGE_DAYS
	SLURMDB_PURGE_FLAGS
	SLURMDB_PURGE_HOURS
	SLURMDB_PURGE_MONTHS
	SLURMDB_REMOVE_ASSOC
	SLURMDB_REMOVE_COORD
	SLURMDB_REMOVE_QOS
	SLURMDB_REMOVE_USER
	SLURMDB_REMOVE_WCKEY
	SLURMDB_REPORT_SORT_NAME
	SLURMDB_REPORT_SORT_TIME
	SLURMDB_REPORT_TIME_HOURS
	SLURMDB_REPORT_TIME_HOURS_PER
	SLURMDB_REPORT_TIME_MINS
	SLURMDB_REPORT_TIME_MINS_PER
	SLURMDB_REPORT_TIME_PERCENT
	SLURMDB_REPORT_TIME_SECS
	SLURMDB_REPORT_TIME_SECS_PER
	SLURMDB_UPDATE_NOTSET
) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw();

our $VERSION = '0.01';

sub AUTOLOAD {
    # This AUTOLOAD is used to 'autoload' constants from the constant()
    # XS function.

    my $constname;
    our $AUTOLOAD;
    ($constname = $AUTOLOAD) =~ s/.*:://;
    croak "&Slurmdb::constant not defined" if $constname eq 'constant';
    my ($error, $val) = constant($constname);
    if ($error) { croak $error; }
    {
	no strict 'refs';
	# Fixed between 5.005_53 and 5.005_61
#XXX	if ($] >= 5.00561) {
#XXX	    *$AUTOLOAD = sub () { $val };
#XXX	}
#XXX	else {
	    *$AUTOLOAD = sub { $val };
#XXX	}
    }
    goto &$AUTOLOAD;
}

#require XSLoader;
#XSLoader::load('Slurmdb', $VERSION);

# XSLoader will not work for Slurm because it does not honour dl_load_flags.
require DynaLoader;
push @ISA, 'DynaLoader';
bootstrap Slurmdb $VERSION;

sub dl_load_flags { if($^O eq 'aix') { 0x00 } else { 0x01 }}

# Preloaded methods go here.

# Autoload methods go after =cut, and are processed by the autosplit program.

1;
__END__

=head1 NAME

Slurmdb - Perl extension for slurmdb library

=head1 SYNOPSIS

  use Slurmdb;

=head1 DESCRIPTION

A traditional Perl module that contains XSUBs of the Slurm Database API.

=head2 EXPORT

None by default.

=head2 Exportable constants


=head1 SEE ALSO

https://slurm.schedmd.com/accounting.html

=head1 AUTHOR

Don Lipari, <lt>lipari@llnl.gov<gt>

=head1 COPYRIGHT AND LICENSE

 Copyright (C) 2010 Lawrence Livermore National Security.
 Written by Don Lipari
 CODE-OCEC-09-009. All rights reserved.

 This file is part of Slurm, a resource management program.  For
 details, see <https://slurm.schedmd.com/>.  Please also
 read the included file: DISCLAIMER.

 Slurm is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the
 Free Software Foundation; either version 2 of the License, or (at
 your option) any later version.

 In addition, as a special exception, the copyright holders give
 permission to link the code of portions of this program with the
 OpenSSL library under certain conditions as described in each
 individual source file, and distribute linked combinations including
 the two. You must obey the GNU General Public License in all respects
 for all of the code used other than OpenSSL. If you modify file(s)
 with this exception, you may extend this exception to your version of
 the file(s), but you are not obligated to do so. If you do not wish
 to do so, delete this exception statement from your version.  If you
 delete this exception statement from all source files in the program,
 then also delete it here.

 Slurm is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 for more details.

 You should have received a copy of the GNU General Public License
 along with Slurm; if not, write to the Free Software Foundation,
 Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

=cut
