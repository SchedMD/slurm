#!/usr/bin/perl
use IO::Socket;

$pid = fork();
if ($pid)
{
#stdout
	my $sock1 = new IO::Socket::INET (
			LocalHost => 'localhost',
			LocalPort => '7071',
			Proto => 'tcp',
			Listen => 1,
			Reuse => 1,
			);
	my $new_sock1 = $sock1->accept();
	while(<$new_sock1>) 
	{
		print STDOUT $_;
	}
	close($sock1);
	print "CLOSED STD OUT SOCKET" ;

}
else
{
#stderr
	my $sock2 = new IO::Socket::INET (
			LocalHost => 'localhost',
			LocalPort => '7072',
			Proto => 'tcp',
			Listen => 1,
			Reuse => 1,
			);
	my $new_sock2 = $sock2->accept();
	while(<$new_sock2>) 
	{
		print STDERR $_;
	}
	close($sock2);
	print "CLOSED STD ERR SOCKET" ;
}
