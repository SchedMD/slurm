--TEST--
Test function slurm_load_job_information() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb_pv@hotmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_load_job_information") or die("skip function slurm_load_job_information unavailable");
?>
--FILE--
<?php


echo "*** Test by calling method or function with its expected arguments ***\n";

$jobArr = slurm_load_job_information();
if((gettype($jobArr)=="array") && ($jobArr != NULL)) {
	echo "! slurm_load_job_information	:	SUCCESS";
} else if($jobArr == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on";
} else if($jobArr == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
} else if($jobArr == -1) {
	echo "! slurm_load_job_information	:	SUCCESS";
}  else {
	echo "[SLURM:ERROR] -4 : ?Unknown?";
}

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
! slurm_load_job_information	:	SUCCESS
