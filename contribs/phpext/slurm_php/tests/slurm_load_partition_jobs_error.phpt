--TEST--
Test function slurm_load_partition_jobs() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb_pv@hotmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_load_partition_jobs") or die("skip function slurm_load_partition_jobs() unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with faulty arguments ***\n";

$node_info = slurm_load_partition_jobs(5);
if($node_info == -1) {
	echo "[SLURM:ERROR] -1 : No jobs where found for a partition by that name\n";
}

$node_info = slurm_load_partition_jobs(NULL);
if($node_info == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on\n";
}

$node_info = slurm_load_partition_jobs('');
if($node_info == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on\n";
}

?>
--EXPECT--
*** Test by calling method or function with faulty arguments ***
[SLURM:ERROR] -1 : No jobs where found for a partition by that name
[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on
[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on
