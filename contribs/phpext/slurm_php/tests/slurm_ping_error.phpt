--TEST--
Test function slurm_ping() by calling it more than or less than its expected arguments
--CREDIT--
Jimmy Tang <jtang@tchpc.tcd.ie>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_ping") or die("skip function slurm_ping unavailable");
?>
--FILE--
<?php

echo "*** Test by calling method or function with incorrect numbers of arguments ***\n";

$extra_arg = NULL;

$ping = slurm_ping( $extra_arg );
if ( $ping["Prim. Controller"] == 0 )
	echo "! slurm_ping $ping == 0 ok\n";
if ( $ping["Sec. Controller"] == -1 )
	echo "! slurm_ping $ping == -1 ok\n";

$ping = slurm_ping( );
if ( $ping["Prim. Controller"] == 0 )
	echo "! slurm_ping $ping == 0 ok\n";
if ( $ping["Sec. Controller"] == -1 )
	echo "! slurm_ping $ping == -1 ok\n";

?>
--EXPECTF--
*** Test by calling method or function with incorrect numbers of arguments ***
! slurm_ping Array == 0 ok
! slurm_ping Array == -1 ok
! slurm_ping Array == 0 ok
! slurm_ping Array == -1 ok
