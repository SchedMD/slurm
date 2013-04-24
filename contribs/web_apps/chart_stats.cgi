#!/usr/bin/perl -Tw
###############################################################################
#
#  chart_stats.cgi - display job usage statistics and cluster utilization
#
# chart_stats creates bar charts of batch job usage and cluster utilization
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
#############################################################################

use strict;
use warnings;

use CGI qw(:standard);
# uncomment to debug...
# use CGI::Carp qw(fatalsToBrowser);
use GD;
use Chart::StackedBars;
use Expect;
use Time::Local;

#
# Sanitize the environment
#
delete @ENV{qw( BASH_ENV CDPATH ENV IFS PATH )};

my $obj = Chart::StackedBars->new(600, 400)
    or die "Failed to create bar chart: $!\n";
my ($time_begin, $time_end);
my ($yb, $mb, $db, $ye, $me, $de);
my ($user, $account, $x_axis, $y_axis, $cluster);


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
		  top_user   => 'Usage - Top Ten Users',
		  top_acct   => 'Usage - Top Ten Accounts',
		  job_size   => 'Job Size',
		  size_acct  => 'Job Size by Accounts',
		  utilizatn  => 'Cluster Utilization',
		  );

    print start_form;

    print h2("SLURM Accounting Reports");
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
	      -value=>'11',
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
	      -value=>'11',
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

sub untaint_user_input {
    my ($user_input) = @_;

    if ($user_input =~ /^(\w+)$/) {
	return $1;
    }
    print_msg("Invalid User Input: $user_input");
    exit;
}

sub untaint_digits {
    my ($user_input) = @_;

    if ($user_input =~ /^(\d+)$/) {
	return $1;
    }
    print_msg("Invalid Numeric Value: $user_input");
    exit;
}

#
# Manually validate the calendar fields
#
sub valid_input {
    $yb = untaint_digits(param('year_begin'));  $yb =~ s/^0*//;
    $mb = untaint_digits(param('month_begin')); $mb =~ s/^0*//;
    $db = untaint_digits(param('day_begin'));   $db =~ s/^0*//;
    $ye = untaint_digits(param('year_end'));    $ye =~ s/^0*//;
    $me = untaint_digits(param('month_end'));   $me =~ s/^0*//;
    $de = untaint_digits(param('day_end'));     $de =~ s/^0*//;
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
	print_msg("Invalid From Year: $yb");
	return 0;
    }
    elsif ($ye < 0) {
	print_msg("Invalid To Year: $ye");
	return 0;
    }
    elsif ($mb < 1 || $mb > 12) {
	print_msg("Invalid From Month: $mb");
	return 0;
    }
    elsif ($me < 1 || $me > 12) {
	print_msg("Invalid To Month: $me");
	return 0;
    }
    elsif ($db < 1 || $db > 31) {
	print_msg("Invalid From Day: $db");
	return 0;
    }
    elsif ($de < 1 || $de > 31) {
	print_msg("Invalid To Day: $de");
	return 0;
    }

    if (timelocal(0, 0, 0, $db, $mb - 1, $yb + 100) >=
	timelocal(0, 0, 0, $de, $me - 1, $ye + 100)) {
	print_msg("From Date Must be Less Than To Date");
	return 0;
    }

    $time_begin = sprintf "%.2d/%.2d/%.2d", $mb, $db, $yb;
    $time_end   = sprintf "%.2d/%.2d/%.2d", $me, $de, $ye;

    if (param('report') =~ /user_usage/) {
	if (!param('user')) {
	    print_msg("User not specified");
	    return 0;
	}
	$user = untaint_user_input(param('user'));
    }
    elsif (param('report') =~ /acct_usage/) {
	if (!param('account')) {
	    print_msg("Account not specified");
	    return 0;
	}
	$account = untaint_user_input(param('account'));
    }

    $cluster = untaint_user_input(param('cluster'));
    $x_axis = untaint_user_input(param('x_axis'));
    $y_axis = untaint_user_input(param('y_axis'));

    return 1;
}

#
# Construct the end time for the next query
#
sub next_time {
    my ($yn, $mn, $dn) = ($yb, $mb, $db);
    my $new_time;
    my $end_time = timelocal(0, 0, 0, $de, $me - 1, $ye + 100);

    if ($_ = $x_axis) {
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

    if ($x_axis eq 'aggregate') {
	$obj->set ('legend' => 'none');
	$obj->set ('x_label' => "$time_begin To $time_end");

	$cmd = "$cmd_base $request start=$time_begin end=$time_end";
	@results = `$cmd`;

	for (@results) {
	    ($consumer, $usage) = split;
	    if ($usage) {
		chop($usage) if ($y_axis =~ /percent/);
		$obj->add_pt($consumer, $usage);
	    }
	}
	$valid_data = @{$obj->get_data()};
    } else {
	my ($i, $j, $k, $l, @data_points, %consum_id, @legend_labels, $before);
	my ($yn, $mn, $dn);
	my ($start, $end);

	my $exp = new Expect or die "Failed to initiate Expect session: $!\n";
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
	    $exp->expect(30, 'sreport:');
	    $before = $exp->before();
	    @results = split "\n", $before;

	    $data_points[$i][0] = $start;
	    for (@results) {
		($consumer, $usage) = split;
		if ($usage) {
		    chop($usage) if ($y_axis =~ /percent/);
		    $consumer = untaint_user_input($consumer);
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
		# Chart.pm complains if you pass it null data points.
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
# There is currently no "account TopUsage" command available in sreport.
# If there were we could use add_bars().  Instead, we must use sreport's
# "cluster AccountUtilizationByUser" command and sift through the copious
# output.  We have to carefully ignore all the lines of user usage as well
# as the parent accounts of accounts.  I.e., we only want to report "leaf"
# accounts.  Then we have to sort the accounts by usage and only report the
# top ten.
# add_top_account_bars() is essentially add_bars() that has been modified to
# do all this.  The %last_account saves each account's usage and is only
# copied to %top_accounts when it is a leaf account.
#
sub add_top_account_bars {
    my ($cmd_base, $request) = @_;
    my ($account, $usage, $valid_data, $cmd, @results);
    my (%last_account, %top_accounts, $m, $key);

    if ($x_axis eq 'aggregate') {
	$obj->set ('legend' => 'none');
	$obj->set ('x_label' => "$time_begin To $time_end");

	$cmd = "$cmd_base $request start=$time_begin end=$time_end";
	@results = `$cmd`;

	for (@results) {
	    ($account, $usage) = split;
	    if ($usage) {
		chop($usage) if ($y_axis =~ /percent/);
		if (!$last_account{$account}) {
		    $last_account{$account} = $usage;
		}
		elsif ($last_account{$account} > 0) {
		    $top_accounts{$account} = $last_account{$account};
		    $last_account{$account} = -1;
		}
	    }
	}

	$m = 1;
	foreach $key (sort { $top_accounts{$b} <=> $top_accounts{$a} } keys
		      %top_accounts)
	{
	    $obj->add_pt($key, $top_accounts{$key});
	    last if ($m++ > 9);
	}
	$valid_data = ($m > 1);
    } else {
	my ($i, $j, $k, $l, @data_points, %acct_id, @legend_labels, $before);
	my ($yn, $mn, $dn);
	my ($start, $end);

	my $exp = new Expect or die "Failed to initiate Expect session: $!\n";
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
	    $exp->expect(30, 'sreport:');
	    $before = $exp->before();
	    @results = split "\n", $before;

	    $data_points[$i][0] = $start;
	    for (@results) {
		($account, $usage) = split;
		if ($usage) {
		    chop($usage) if ($y_axis =~ /percent/);
		    if (!$last_account{$account}) {
			$last_account{$account} = $usage;
		    }
		    elsif ($last_account{$account} > 0) {
			$top_accounts{$account} = $last_account{$account};
			$last_account{$account} = -1;
		    }
		}
	    }

	    $m = 1;
	    foreach $key (sort { $top_accounts{$b} <=> $top_accounts{$a} } keys
			  %top_accounts)
	    {
		unless ($acct_id{$key}) {
		    $acct_id{$key} = $j;
		    $legend_labels[$j++ - 1] = $key;
		}
		$data_points[$i][$acct_id{$key}] = $top_accounts{$key};
		last if ($m++ > 9);
	    }

	    %last_account = ();
	    %top_accounts = ();
	    ($yb, $mb, $db) = ($yn, $mn, $dn);
	    $i++;
	}
	$exp->send("quit\n");
	$exp->soft_close();

	for ($k = 0; $k < $i; $k++) {
	    for ($l = 0; $l < $j; $l++) {
		# Chart.pm complains if you pass it null data points.
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
# add_job_size_bars() presents a report of cluster usage based on the
# size of the jobs that were allocated the cycles.  Four job size
# groups have been pre-selected and are based on the percentage of the
# cluster's processors the job was allocated: 1 to 10%, 10 to 29%, 30
# to 74%, and 75 to 100%.
#
# There is currently no sreport command that returns job size reports
# for the entire cluster.  Instead, this routine has to sum the
# reports for each account and chart the results for the whole
# cluster.
#
sub add_job_size_bars {
    my ($cmd_base, $request, $cpus) = @_;
    my ($cmd, @results, @field, @group, $i);

    $obj->set ('y_label' => 'Percent of Allocated Cycles')
	if ($y_axis =~ /percent/);

    if ($x_axis eq 'aggregate') {
	$obj->set ('legend' => 'none');
	$obj->set ('x_label' =>
		   "Job Size as a Portion of $cluster ($cpus total processors)");

	$cmd = "$cmd_base $request start=$time_begin end=$time_end";
	@results = `$cmd`;

	for (@results) {
	    (undef, $field[0], $field[1], $field[2], $field[3], undef) = split;
	    for ($i = 0; $i < 4; $i++) {
		$group[$i] += $field[$i];
	    }
	    @field = ();
	}

	if ($y_axis =~ /percent/) {
	    my $total_used = $group[0] + $group[1] + $group[2] +  $group[3];
	    $obj->add_pt('1 - 9%',    100 * $group[0] / $total_used);
	    $obj->add_pt('10 - 29%',  100 * $group[1] / $total_used);
	    $obj->add_pt('30 - 74%',  100 * $group[2] / $total_used);
	    $obj->add_pt('75 - 100%', 100 * $group[3] / $total_used);
	}
	else {
	    $obj->add_pt('1 - 9%',    $group[0]);
	    $obj->add_pt('10 - 29%',  $group[1]);
	    $obj->add_pt('30 - 74%',  $group[2]);
	    $obj->add_pt('75 - 100%', $group[3]);
	}
    } else {
	my ($i, $k, $g, @data_points, @legend_labels, $before);
	my ($yn, $mn, $dn);
	my ($start, $end);

	my $exp = new Expect or die "Failed to initiate Expect session: $!\n";
	$exp->raw_pty(1);
	$exp->log_stdout(0);
	$exp->spawn($cmd_base) or die "Cannot spawn sreport: $!\n";
	$exp->expect(5, 'sreport:');

	@legend_labels = ('1 - 9%', '10 - 29%', '30 - 74%', '75 - 100%');

	$i = 0;
	while (($i < 12) && ($yb < $ye || $mb < $me || $db < $de)) {
	    ($yn, $mn, $dn) = next_time($yb, $mb, $db);

	    $start = sprintf "%.2d/%.2d/%.2d", $mb, $db, $yb;
	    $end   = sprintf "%.2d/%.2d/%.2d", $mn, $dn, $yn;
	    $exp->send("$request start=$start end=$end\n");
	    $exp->expect(30, 'sreport:');
	    $before = $exp->before();
	    @results = split "\n", $before;

	    for (@results) {
		(undef, $field[0], $field[1], $field[2], $field[3], undef) = split;
		for ($g = 0; $g < 4; $g++) {
		    $group[$g] += $field[$g];
		}
		@field = ();
	    }

	    $data_points[$i][0] = $start;
	    if ($y_axis =~ /percent/) {
		my $total_used = $group[0] + $group[1] + $group[2] +  $group[3];
		$data_points[$i][1] = 100 * $group[0] / $total_used;
		$data_points[$i][2] = 100 * $group[1] / $total_used;
		$data_points[$i][3] = 100 * $group[2] / $total_used;
		$data_points[$i][4] = 100 * $group[3] / $total_used;
	    }
	    else {
		$data_points[$i][1] = $group[0];
		$data_points[$i][2] = $group[1];
		$data_points[$i][3] = $group[2];
		$data_points[$i][4] = $group[3];
	    }
	    @group = ();

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
# add_job_size_account_bars charts usage for the top ten accounts,
# with each bar divided into job size ranges.  There are a fixed
# number of job size bins which are based on a percentage of the
# cluster: '1 - 9%', '10 - 29%', '30 - 74%', and '75 - 100%'
#
# In order to accommodate both account and job size data on the same
# chart, we must ignore the x_axis selection and provide an aggregate
# picture only.
#
sub add_job_size_account_bars {
    my ($cmd_base, $request) = @_;
    my ($cmd, @results, $account, @field);
    my ($i, $j, $m, @data_points, @top_data, @legend_labels, $total_used);

    $obj->set ('x_label' => 'Accounts');
    $obj->set ('y_label' => 'Percent of Allocated Cycles')
	if ($y_axis =~ /percent/);
    @legend_labels = ('1 - 9%', '10 - 29%', '30 - 74%', '75 - 100%');

    $cmd = "$cmd_base $request start=$time_begin end=$time_end";
    @results = `$cmd`;

    $i = 0;
    for (@results) {
	($account, $field[0], $field[1], $field[2], $field[3], undef) = split;
	chop  @field if ($y_axis =~ /percent/);
	$data_points[$i][0] = $account;
	$data_points[$i][1] = $field[0];
	$data_points[$i][2] = $field[1];
	$data_points[$i][3] = $field[2];
	$data_points[$i][4] = $field[3];
	$data_points[$i][5] = $field[0] + $field[1] + $field[2] +  $field[3];
	$total_used += $data_points[$i][5];
	@field = ();
	$i++;
    }

    $m = 1;
    foreach $i (sort { @$b[5] <=> @$a[5] } @data_points) {
	$top_data[0] = @$i[0];
	for ($j = 1; $j < 5; $j++) {
	    if ($y_axis =~ /percent/) {
		$top_data[$j] = @$i[$j] * 100 / $total_used;
	    }
	    else {
		$top_data[$j] = @$i[$j];
	    }
	}
	$obj->add_pt(@top_data);
	last if ($m++ > 9);
	@top_data = ();
    }

    $obj->set ('legend_labels' => \@legend_labels);

    return ($m > 1);
}

#
# Cluster Utilization queries return 4 values per line
#
sub add_utilizatn_bars {
    my ($cmd_base, $request) = @_;
    my ($allocated, $reserved, $idle, $down, $cmd, $results);

    if ($x_axis eq 'aggregate') {
	$obj->set ('legend' => 'none');
	$obj->set ('x_label' => "$time_begin To $time_end");

	$cmd = "$cmd_base $request start=$time_begin end=$time_end";
	$results = `$cmd`;

	($allocated, $reserved, $idle, $down) = split ' ', $results;
	chop($allocated, $reserved, $idle, $down)
	    if ($y_axis =~ /percent/);

	$obj->add_pt('Allocated', $allocated);
	$obj->add_pt('Reserved', $reserved);
	$obj->add_pt('Idle', $idle);
	$obj->add_pt('Down', $down);
    } else {
	my ($i, $k, @data_points, @legend_labels);
	my ($yn, $mn, $dn);
	my ($start, $end);

	my $exp = new Expect or die "Failed to initiate Expect session: $!\n";
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
	    $exp->expect(30, 'sreport:');
	    $results = $exp->before();

	    ($allocated, $reserved, $idle, $down) = split ' ', $results;
	    chop($allocated, $reserved, $idle, $down)
		if ($y_axis =~ /percent/);

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
    my @results = `/usr/bin/sacctmgr -nr show cluster format=cluster`;

    for (@results) {
	chomp;
	s/^\s+//;
	s/\s+$//;
	push @clusters, $_;
    }
    return \@clusters;
}

#
# Charts the user usage request
#
sub chart_user {
    my $title = "$user Usage of $cluster";
    $obj->set ('title' => $title);

    my $cmd = "/usr/bin/sreport -nt $y_axis";
    my $request = " cluster UserUtilizationByAccount" .
	" cluster=$cluster user=$user format=Account,Used";

    if (add_bars($cmd, $request)) {
	$obj->cgi_png();
    }
    else {
	print_msg("No significant usage found for user $user" .
		  " from $time_begin to $time_end");
    }
}

#
# Charts the account usage request
#
sub chart_account {
    my $title = "$account Usage of $cluster";
    $obj->set ('title' => $title);

    my $cmd = "/usr/bin/sreport -nt $y_axis";
    my $request = " cluster AccountUtilizationByUser" .
	" cluster=$cluster account=$account format=Login,Used";

    if (add_bars($cmd, $request)) {
	$obj->cgi_png();
    }
    else {
	print_msg("No significant usage found for account $account" .
		  " from $time_begin to $time_end");
    }
}

#
# Charts the usage of the top ten users.  Use the "Group" option to
# retrieve the combined usage from all the accounts the user charged.
#
sub chart_top_users {
    my $title = "$cluster Top Users";
    $obj->set ('title' => $title);

    my $cmd = "/usr/bin/sreport -nt $y_axis";
    my $request = " user TopUsage Group cluster=$cluster format=Login,Used";

    if (add_bars($cmd, $request)) {
	$obj->cgi_png();
    }
    else {
	print_msg("Failed to report top users from $time_begin to $time_end");
    }
}

#
# Charts the usage of the top ten accounts.
#
sub chart_top_accounts {
    my $title = "$cluster Top Accounts";
    $obj->set ('title' => $title);

    my $cmd = "/usr/bin/sreport -nt $y_axis";
    my $request = " cluster AccountUtilizationByUser" .
	" cluster=$cluster format=Account,Used";

    if (add_top_account_bars($cmd, $request)) {
	$obj->cgi_png();
    }
    else {
	print_msg("Failed to report top accounts from $time_begin to $time_end");
    }
}

#
# Charts the cluster usage based on job size.
#
sub chart_job_size {
    my @brink;
    my $cpus = `/usr/bin/sacctmgr -nr show cluster cluster=$cluster format=cpucount`;
    chomp;
    $cpus =~ s/^\s+//;
    $cpus =~ s/\s+$//;
    $cpus = untaint_digits($cpus);

    my $title = "$cluster Usage Grouped by Job Size";
    $obj->set ('title' => $title);

    $brink[0] = int(0.1 * $cpus);
    $brink[1] = int(0.3 * $cpus);
    $brink[2] = int(0.75 * $cpus);

    my $cmd = "/usr/bin/sreport -nt hours";
    my $request = " job SizesByAccount cluster=$cluster" .
	" format=Account grouping=$brink[0],$brink[1],$brink[2]";

    if (add_job_size_bars($cmd, $request, $cpus)) {
	$obj->cgi_png();
    }
    else {
	print_msg("Failed to return Job Sizes for $cluster" .
		  " from $time_begin to $time_end");
    }
}

#
# Charts the usage of the top ten accounts broken down by job size
# categories.  We're unable to provide an interval option - only the
# aggregate form is supported.
#
sub chart_size_accounts {
    my @brink;
    my $cpus = `/usr/bin/sacctmgr -nr show cluster cluster=$cluster format=cpucount`;
    chomp;
    $cpus =~ s/^\s+//;
    $cpus =~ s/\s+$//;
    $cpus = untaint_digits($cpus);

    my $title = "$cluster Usage Grouped by Job Size and Accounts";
    $obj->set ('title' => $title);

    $brink[0] = int(0.1 * $cpus);
    $brink[1] = int(0.3 * $cpus);
    $brink[2] = int(0.75 * $cpus);

    my $cmd = "/usr/bin/sreport -nt hours";
    my $request = " job SizesByAccount FlatView cluster=$cluster" .
	" format=Account grouping=$brink[0],$brink[1],$brink[2]";

    if (add_job_size_account_bars($cmd, $request, $cpus)) {
	$obj->cgi_png();
    }
    else {
	print_msg("Failed to return Job Sizes/Accounts for $cluster" .
		  " from $time_begin to $time_end");
    }
}

#
# Charts the cluster utilization
#
sub chart_utilizatn {
    my $title = "Utilization of $cluster";
    $obj->set ('title' => $title);

    my $cmd = "/usr/bin/sreport -nt $y_axis";
    my $request = " cluster Utilization cluster=$cluster" .
	" format=Allocated,Reserved,Idle,Down";

    if (add_utilizatn_bars($cmd, $request)) {
	$obj->cgi_png();
    }
    else {
	print_msg("Failed to return Utilization for $cluster" .
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
    $obj->set ('x_label' => $x_axis);
    if ($y_axis =~ /percent/) {
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
