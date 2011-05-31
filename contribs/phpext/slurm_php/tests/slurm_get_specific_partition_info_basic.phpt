--TEST--
Test function slurm_get_specific_partition_info() by calling it with its expected arguments
--CREDIT--
Jimmy Tang <jtang@tchpc.tcd.ie>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_get_specific_partition_info") or die("skip function slurm_get_specific_partition_info unavailable");
function_exists("slurm_print_partition_names") or die("skip function slurm_print_partition_names unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with its expected arguments ***\n";

$arr = slurm_print_partition_names();
if(is_array($arr)) {
	if(count($arr)!=0) {
		$partition = slurm_get_specific_partition_info($arr[0]);
		if(is_array($partition)) {
			echo "[SLURM:SUCCESS]";
		} else if($partition == -3) {
			echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on";
		} else if($partition == -2) {
			echo "[SLURM:ERROR] -2 : Daemons not online";
		} else if($partition == -1) {
			echo "[SLURM:ERROR] -1 : No partition by that name was found on your system";
		}
	} else {
		echo "! No partitions available !";
	}
} else {
	echo "[SLURM:ERROR] CODE=".$arr."| while trying to retreive partition names";
}
?>
--EXPECTF--
*** Test by calling method or function with its expected arguments ***
[SLURM:SUCCESS]
