#!/usr/bin/perl -w
###############################################################################
#
#  chart_stats.cgi - display job usage statistics and cluster utilization
#  $Id: chart_stats.cgi lipari $
#
# chart_stats creates bar charts of batch job usage and machine utilization
# It initially presents a page that allows the user to specify the chart
# s/he wants to see.  It then invokes sreport to retrieve the data from the
# SLURM database and creates a page containing a chart of this data.
#
#############################################################################
#  Copyright (C) 2010 Lawrence Livermore National Security.
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Don Lipari <lipari1@llnl.gov>.
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
#############################################################################

use strict;
use warnings;

use CGI qw(:standard escapeHTML);
#use CGI::Carp qw(fatalsToBrowser);
use GD;
use Chart::StackedBars;
use Expect;
use Time::Local;


my $obj = Chart::StackedBars->new(600, 400);
my ($time_begin, $time_end);
my ($yb, $mb, $db, $ye, $me, $de);


if (param) {
    if (valid_input()) {
	plot_results();
    }
} else {
    print_form();
}

#
# When the chart cannot be constructed, tell the user what went wrong.
#
sub print_msg {
    my ($msg) = @_;

    print header,
    start_html('No Results');
    print $msg;
    print end_html;
}

#
# Ask the user to specify the chart s/he wants to see
#
sub print_form {

    print header,
    start_html('HPC Stats');

    my @values = qw(
		    top_user top_acct job_size size_acct utilizatn
		    );

    my %labels = (
		  user_usage => 'Usage - Single User: ',
		  acct_usage => 'Usage - Single Account: ',
		  top_user   => 'Usage - Top Users',
		  top_acct   => 'Usage - Top Accounts',
		  job_size   => 'Job Size',
		  size_acct  => 'Job Size by Accounts',
		  utilizatn  => 'Cluster Utilization',
		  );

    print start_form;

    print h3("Cluster"),
    popup_menu(-name=>'cluster',
	       -values=>cluster_list());

    print h3("Report");

    print radio_group(-name      => 'report',
		      -values    => 'user_usage',
		      -labels    => \%labels),
    textfield(-name => 'user', -default   => remote_user()), br;

    print radio_group(-name      => 'report',
		      -values    => 'acct_usage',
		      -labels    => \%labels),
    textfield(-name => 'account'), br;

    print radio_group(-name      => 'report',
		      -values    => \@values,
		      -linebreak => 'true',
		      -labels    => \%labels);

    print h3("From");
    print b("Month: "),
    textfield(-name=>'month_begin',
	      -value=>'mm',
	      -size=>2,
	      -maxlength=>2),
    b(" Day: "),
    textfield(-name=>'day_begin',
	      -value=>'dd',
	      -size=>2,
	      -maxlength=>2),
    b(" Year: "),
    textfield(-name=>'year_begin',
	      -value=>'10',
	      -size=>2,
	      -maxlength=>2),
    br;

    print h3("Up To But Not Including");
    print b("Month: "),
    textfield(-name=>'month_end',
	      -value=>'mm',
	      -size=>2,
	      -maxlength=>2),
    b(" Day: "),
    textfield(-name=>'day_end',
	      -value=>'dd',
	      -size=>2,
	      -maxlength=>2),
    b(" Year: "),
    textfield(-name=>'year_end',
	      -value=>'10',
	      -size=>2,
	      -maxlength=>2),
    br;

    print h3("X Axis"),
    radio_group(-name=>'x_axis',
		-values=>['Days','Weeks','Months',
			  'Quarters','Years','aggregate'],
		-default=>'Days',
		-linebreak=>'true');

    print h3("Y Axis"),
    radio_group(-name=>'y_axis',
		-values=>['percent','hours'],
		-default=>'percent',
		-labels=>{'percent'=>'percent',
                          'hours'=>'processor-hours'},
		-linebreak=>'true'),
    br;

    print submit, p, reset;

    print end_form;
    print end_html;
}

#
# Manually validate the calendar fields
#
sub valid_input {
    $yb = param('year_begin');  $yb =~ s/^0*//;
    $mb = param('month_begin'); $mb =~ s/^0*//;
    $db = param('day_begin');   $db =~ s/^0*//;
    $ye = param('year_end');    $ye =~ s/^0*//;
    $me = param('month_end');   $me =~ s/^0*//;
    $de = param('day_end');     $de =~ s/^0*//;
    my (undef,undef,undef,$mday,$mon,$year,undef,undef,undef) = localtime();

    if ($ye > ($year - 100)) {
	($ye, $me, $de) = ($year - 100, $mon + 1, $mday);
    }
    elsif ($ye == ($year - 100)) {
	if ($me > ($mon + 1)) {
	    ($me, $de) = ($mon + 1, $mday);
	}
	elsif (($me == ($mon + 1)) && ($de > $mday)) {
	    $de = $mday;
	}
    }

    if ($yb < 0 || $yb > $ye) {
	print_msg("Invalid From Year: " . param('year_begin'));
	return 0;
    }
    elsif ($ye < 0) {
	print_msg("Invalid To Year: " . param('year_end'));
	return 0;
    }
    elsif ($mb < 1 || $mb > 12) {
	print_msg("Invalid From Month: " . param('month_begin'));
	return 0;
    }
    elsif ($me < 1 || $me > 12) {
	print_msg("Invalid To Month: " . param('month_end'));
	return 0;
    }
    elsif ($db < 1 || $db > 31) {
	print_msg("Invalid From Day: " . param('day_begin'));
	return 0;
    }
    elsif ($de < 1 || $de > 31) {
	print_msg("Invalid To Day: " . param('day_end'));
	return 0;
    }
    elsif ((param('report') =~ /user_usage/) && !param('user')) {
	print_msg("User not specified");
	return 0;
    }
    elsif ((param('report') =~ /acct_usage/) && !param('account')) {
	print_msg("Account not specified");
	return 0;
    }

    $time_begin = sprintf "%.2d/%.2d/%.2d", $mb, $db, $yb;
    $time_end   = sprintf "%.2d/%.2d/%.2d", $me, $de, $ye;

    return 1;
}

#
# Construct the end time for the next query
#
sub next_time {
    my ($yn, $mn, $dn) = ($yb, $mb, $db);
    my $new_time;
    my $end_time = timelocal(0, 0, 0, $de, $me - 1, $ye + 100);

    if ($_ = param('x_axis')) {
	/Years/ && do {
	    $yn++;
	    $new_time = timelocal(0, 0, 0, $dn, $mn - 1, $yn + 100);
	};
	/Quarters/ && do {
	    $mn += 3;
	    if ($mn > 12) { $mn %= 12; $yn++; }
	    $new_time = timelocal(0, 0, 0, $dn, $mn - 1, $yn + 100);
	};
	/Months/ && do {
	    $mn++;
	    if ($mn > 12) { $mn = 1; $yn++; }
	    $new_time = timelocal(0, 0, 0, $dn, $mn - 1, $yn + 100);
	};
	/Weeks/ && do {
	    $new_time = timelocal(0, 0, 0, $db, $mb - 1, $yb + 100);
	    $new_time += 7 * 24 * 3600;
	    (undef,undef,undef,$dn,$mn,$yn,undef,undef,undef) =
		localtime($new_time);
	    $mn++; $yn -= 100;
	};
	/Days/ && do {
	    $new_time = timelocal(0, 0, 0, $db, $mb - 1, $yb + 100);
	    $new_time += 24 * 3600;
	    (undef,undef,undef,$dn,$mn,$yn,undef,undef,undef) =
		localtime($new_time);
	    $mn++; $yn -= 100;
	};
    }

    if ($new_time > $end_time) {
	($yn, $mn, $dn) = ($ye, $me, $de);
    }

    return ($yn, $mn, $dn);
}

#
# Chart the bars for the User and Account queries
# These queries return 2 values per line:  a user or account and its usage.
# The aggregate queries require just one sreport invocation.
# The periodic queries require multiple sreport invocations.
# We use the Expect module to open just one sreport connection and
# issue multiple queries.
#
sub add_bars {
    my ($cmd_base, $request) = @_;
    my ($consumer, $usage, $valid_data, $cmd, @results);

    if (param('x_axis') eq 'aggregate') {
	$obj->set ('legend' => 'none');
	$obj->set ('x_label' => "$time_begin To $time_end");

	$cmd = "$cmd_base $request start=$time_begin end=$time_end";
	@results = `$cmd`;

	for (@results) {
	    ($consumer, $usage) = split;
	    if ($usage) {
		chop($usage) if (param('y_axis') =~ /percent/);
		$obj->add_pt($consumer, $usage);
	    }
	}
	$valid_data = @{$obj->get_data()};
    } else {
	my ($i, $j, $k, $l, @data_points, %consum_id, @legend_labels, $before);
	my ($yn, $mn, $dn);
	my ($start, $end);

	my $exp = new Expect;
	$exp->raw_pty(1);
	$exp->log_stdout(0);
	$exp->spawn($cmd_base) or die "Cannot spawn sreport: $!\n";
	$exp->expect(5, 'sreport:');

	$i = 0; $j = 1;
	while (($i < 12) && ($yb < $ye || $mb < $me || $db < $de)) {
	    ($yn, $mn, $dn) = next_time($yb, $mb, $db);

	    $start = sprintf "%.2d/%.2d/%.2d", $mb, $db, $yb;
	    $end   = sprintf "%.2d/%.2d/%.2d", $mn, $dn, $yn;
	    $exp->send("$request start=$start end=$end\n");
	    $exp->expect(5, 'sreport:');
	    $before = $exp->before();
	    @results = split "\n", $before;

	    $data_points[$i][0] = $start;
	    for (@results) {
		($consumer, $usage) = split;
		if ($usage) {
		    chop($usage) if (param('y_axis') =~ /percent/);
		    unless ($consum_id{$consumer}) {
			$consum_id{$consumer} = $j;
			$legend_labels[$j++ - 1] = $consumer;
		    }
		    $data_points[$i][$consum_id{$consumer}] = $usage;
		}
	    }

	    ($yb, $mb, $db) = ($yn, $mn, $dn);
	    $i++;
	}
	$exp->send("quit\n");
	$exp->soft_close();

	for ($k = 0; $k < $i; $k++) {
	    for ($l = 0; $l < $j; $l++) {
#		Chart.pm complains if you pass it null data points.
		$data_points[$k][$l] = 0 unless ($data_points[$k][$l]);
	    }
	    $obj->add_pt(@{$data_points[$k]});
	}

	$obj->set ('legend_labels' => \@legend_labels);
	$valid_data = ($j > 0);
    }

    return $valid_data;
}

#
# Cluster Utilization queries return 4 values per line
#
sub add_utilizatn_bars {
    my ($cmd_base, $request) = @_;
    my ($allocated, $reserved, $idle, $down, $cmd, $results);

    if (param('x_axis') eq 'aggregate') {
	$obj->set ('legend' => 'none');
	$obj->set ('x_label' => "$time_begin To $time_end");

	$cmd = "$cmd_base $request start=$time_begin end=$time_end";
	$results = `$cmd`;

	($allocated, $reserved, $idle, $down) = split ' ', $results;
	chop($allocated, $reserved, $idle, $down)
	    if (param('y_axis') =~ /percent/);

	$obj->add_pt('Allocated', $allocated);
	$obj->add_pt('Reserved', $reserved);
	$obj->add_pt('Idle', $idle);
	$obj->add_pt('Down', $down);
    } else {
	my ($i, $k, @data_points, @legend_labels);
	my ($yn, $mn, $dn);
	my ($start, $end);

	my $exp = new Expect;
	$exp->raw_pty(1);
	$exp->log_stdout(0);
	$exp->spawn($cmd_base) or die "Cannot spawn sreport: $!\n";
	$exp->expect(5, 'sreport:');

	@legend_labels = qw(
		    Allocated Reserved Idle Down
		    );

	$i = 0;
	while (($i < 12) && ($yb < $ye || $mb < $me || $db < $de)) {
	    ($yn, $mn, $dn) = next_time($yb, $mb, $db);

	    $start = sprintf "%.2d/%.2d/%.2d", $mb, $db, $yb;
	    $end   = sprintf "%.2d/%.2d/%.2d", $mn, $dn, $yn;
	    $exp->send("$request start=$start end=$end\n");
	    $exp->expect(5, 'sreport:');
	    $results = $exp->before();

	    ($allocated, $reserved, $idle, $down) = split ' ', $results;
	    chop($allocated, $reserved, $idle, $down)
		if (param('y_axis') =~ /percent/);

	    $data_points[$i][0] = $start;
	    $data_points[$i][1] = $allocated;
	    $data_points[$i][2] = $reserved;
	    $data_points[$i][3] = $idle;
	    $data_points[$i][4] = $down;

	    ($yb, $mb, $db) = ($yn, $mn, $dn);
	    $i++;
	}
	$exp->send("quit\n");
	$exp->soft_close();

	for ($k = 0; $k < $i; $k++) {
	    $obj->add_pt(@{$data_points[$k]});
	}

	$obj->set ('legend_labels' => \@legend_labels);
    }

    return 1;
}

#
# Discovers the available clusters
#
sub cluster_list {
    my @clusters;
    my $cmd = "/usr/bin/sacctmgr -nr show cluster" .
	" format=cluster";
    my @results = `$cmd`;

    for (@results) {
	chomp;
	s/^\s+//;
	s/\s+$//;
	push @clusters, $_;
    }
    return \@clusters;
}

#
# Charts the user usage requests
#
sub chart_user {
    my $title = param('user') . " Usage of " . param('cluster');
    $obj->set ('title' => $title);

    my $cmd = "/usr/bin/sreport -nt " . param('y_axis');
    my $request = " cluster UserUtilizationByAccount" .
	" cluster=" . param('cluster') . " user=" . param('user') .
	" format=Account,Used";

    if (add_bars($cmd, $request)) {
	$obj->cgi_png();
    }
    else {
	print_msg("No significant usage found for user " . param('user') .
		  " from $time_begin to $time_end");
    }
}

#
# Charts the account usage requests
#
sub chart_account {
    my $title = param('account') . " Usage of " . param('cluster');
    $obj->set ('title' => $title);

    my $cmd = "/usr/bin/sreport -nt " . param('y_axis');
    my $request = " cluster AccountUtilizationByUser" .
	" cluster=" . param('cluster') . " account=" . param('account') .
	" format=Login,Used";

    if (add_bars($cmd, $request)) {
	$obj->cgi_png();
    }
    else {
	print_msg("No significant usage found for account " . param('account') .
		  " from $time_begin to $time_end");
    }
}

#
# Charts the top ten users.  Use the "Group" option to retrieve the combined
# usage from all the accounts the user charged.
#
sub chart_top_users {
    my $title = param('cluster') . " Top Users";
    $obj->set ('title' => $title);

    my $cmd = "/usr/bin/sreport -nt " . param('y_axis');
    my $request = " user TopUsage Group cluster=" . param('cluster') .
	" format=Login,Used";

    if (add_bars($cmd, $request)) {
	$obj->cgi_png();
    }
    else {
	print_msg("Failed to report top users from $time_begin to $time_end");
    }
}

sub chart_top_accounts {
    print_msg("Not Yet Implemented");
#    print "/usr/bin/sreport -npt ", param('y_axis'),
#    " cluster AccountUtilizationByUser cluster=", param('cluster'),
#    " format=Account,Used",
#    " start=$time_begin", " end=$time_end", br;
}

sub chart_job_size {
    print_msg("Not Yet Implemented");
#    print "/usr/bin/sreport -npt minutes",
#    " job SizesByAccount FlatView cluster=", param('cluster'),
#    " start=$time_begin", " end=$time_end", br;
}

sub chart_size_accounts {
    print_msg("Not Yet Implemented");
#    print "/usr/bin/sreport -npt minutes",
#    " job SizesByAccount FlatView cluster=", param('cluster'),
#    " start=$time_begin", " end=$time_end", br;
}

#
# Charts the cluster utilization
#
sub chart_utilizatn {
    my $title = "Utilization of " . param('cluster');
    $obj->set ('title' => $title);

    my $cmd = "/usr/bin/sreport -nt " . param('y_axis');
    my $request = " cluster Utilization cluster=" . param('cluster') .
	" format=Allocated,Reserved,Idle,Down";

    if (add_utilizatn_bars($cmd, $request)) {
	$obj->cgi_png();
    }
    else {
	print_msg("Failed to return Utilization for " . param('cluster') .
		  " from $time_begin to $time_end");
    }
}

#
# Setup some chart attributes and select the subroutine that charts the
# user's request
#
sub plot_results {
    my $sub_title = "From $time_begin To $time_end";
    $obj->set ('sub_title' => $sub_title);
    $obj->set ('x_label' => param('x_axis'));
    if (param('y_axis') =~ /percent/) {
	$obj->set ('y_label' => 'Percent of Cluster');
    } else {
	$obj->set ('y_label' => 'Processor - Hours');
    }
    $obj->set ('include_zero' => 'true');

    if ($_ = param('report')) {
	/user_usage/ && chart_user();
	/acct_usage/ && chart_account();
	/top_user/   && chart_top_users();
	/top_acct/   && chart_top_accounts();
	/job_size/   && chart_job_size();
	/size_acct/  && chart_size_accounts();
	/utilizatn/  && chart_utilizatn();
    }
}
