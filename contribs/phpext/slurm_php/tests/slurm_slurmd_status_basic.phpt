--TEST--
Test function slurm_slurmd_status() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb_pv@hotmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_slurmd_status") or die("skip function slurm_slurmd_status() unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with its expected arguments ***\n";

$slurm_status = slurm_slurmd_status();
if((gettype($slurm_status)!="integer") && ($slurm_status != NULL)) {
	echo "! slurm_slurmd_status()	:	SUCCESS";
} else if($slurm_status == -2) {
	echo "[SLURM:ERROR] -2 : status couldn't be loaded, this is a sign that the daemons are offline"."\n\n"."Please put the slurmd and slurmctld daemons online";
}  else {
	echo "[SLURM:ERROR] -4 : ?Unknown?";
}

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
! slurm_slurmd_status()	:	SUCCESS
