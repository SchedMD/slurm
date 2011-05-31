--TEST--
Test function slurm_load_partition_jobs() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb.peterv@gmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_load_partition_jobs") or die("skip function slurm_load_partition_jobs() unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with correct arguments ***\n";

$arr = slurm_print_partition_names();
if(is_array($arr)) {
	if(count($arr)!=0) {
		$partition = slurm_load_partition_jobs($arr[0]);
		if(is_array($partition)) {
			echo "[SLURM:SUCCESS]";
		} else if($partition == -3) {
			echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on";
		} else if($partition == -2) {
			echo "[SLURM:ERROR] -2 : Daemons not online";
		} else if($partition == -1) {
			echo "[SLURM:SUCCESS]";
		}
	} else {
		echo "! No partitions available !";
	}
} else {
	echo "[SLURM:ERROR] CODE=".$arr."| while trying to retreive partition names";
}
?>
--EXPECTF--
*** Test by calling method or function with correct arguments ***
[SLURM:SUCCESS]
