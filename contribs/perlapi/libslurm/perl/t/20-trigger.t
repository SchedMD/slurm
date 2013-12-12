#!/usr/bin/perl -T
use Test::More tests => 5;
use Slurm qw(:constant);
use POSIX qw(:errno_h);


# 1
my $slurm = Slurm::new();
ok(defined $slurm,  "create slurm object with default configuration");


# 2
my $trig_set;
SKIP: {
    skip "not super user", 1 if $>;
    my $rc = $slurm->set_trigger( { trig_type => TRIGGER_TYPE_RECONFIG,
				    res_type => TRIGGER_RES_TYPE_NODE,
				    program => "/bin/true",
				  } );
    ok($rc == SLURM_SUCCESS, "set trigger") and $trig_set = 1 or diag("set_trigger: " . $slurm->strerror());
}


# 3
my $resp;
$resp = $slurm->get_triggers();
ok(ref($resp) eq "HASH", "getting triggers");



# 4
SKIP: {
    skip "not super user", 1 if $>;
    skip "trigger not set", 1 unless $trig_set;
    my $rc = $slurm->pull_trigger ( {trig_res_type => TRIGGER_RES_TYPE_NODE} );
    ok($rc == SLURM_ERROR && $slurm->get_errno() == EINVAL, "pull trigger") or diag("pull_trigger: " . $slurm->strerror());
}


# 5
SKIP: {
    skip "not super user", 1 if $>;
    skip "trigger not set", 1 unless $trig_set;
    my $trig_id;
    foreach my $trig(@{$resp->{trigger_array}}) {
	next unless $trig->{program} eq "/bin/true";
	$trig_id = $trig->{trig_id};
    }
    skip "trigger not found", 1 unless $trig_id;
    my $rc = $slurm->clear_trigger ( {trig_id => $trig_id, user_id => 0} );
    ok($rc == SLURM_SUCCESS, "clear trigger") or diag("clear_trigger: " . $slurm->strerror());
}
