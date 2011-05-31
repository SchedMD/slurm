--TEST--
Test function slurm_print_partition_names() by calling it with its expected arguments
--CREDIT--
Jimmy Tang <jtang@tchpc.tcd.ie>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_print_partition_names") or die("skip function slurm_print_partition_names unavailable");
?>
--FILE--
<?php


echo "*** Test by calling method or function with its expected arguments ***\n";

$nameArray = slurm_print_partition_names();
if((gettype($nameArray)=="array") && ($nameArray != NULL)) {
	echo "! slurm_print_partition_names	:	SUCCESS";
} else if($nameArray == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on";
} else if($nameArray == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
} else if($nameArray == -1) {
	echo "[SLURM:ERROR] -1 : No partitions were found on the system";
}  else {
	echo "[SLURM:ERROR] -4 : ?Unknown?";
}


?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
! slurm_print_partition_names	:	SUCCESS
