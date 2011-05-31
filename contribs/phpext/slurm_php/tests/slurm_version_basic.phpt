--TEST--
Test function slurm_version() by calling it with its expected arguments
--CREDIT--
Jimmy Tang <jtang@tchpc.tcd.ie>
Peter Vermeulen <nmb.peterv@gmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_version") or die("skip function slurm_version unavailable");
?>
--FILE--
<?php

echo "*** Test by calling method or function with its expected arguments ***\n";

$ver = slurm_version(0);
if((gettype($ver)=="integer") && ($ver != NULL) && ($ver>0)) {
	echo "! slurm_version	:	SUCCESS";
} else if($ver == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on";
} else if($ver == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
} else if($ver == -1) {
	echo "[SLURM:ERROR] -1 : No version was found on the system";
}

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
! slurm_version	:	SUCCESS
