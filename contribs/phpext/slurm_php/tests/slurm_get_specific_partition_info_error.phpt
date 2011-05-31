--TEST--
Test function slurm_get_specific_partition_info() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb_pv@hotmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_get_specific_partition_info") or die("skip function slurm_get_specific_partition_info() unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with its expected arguments ***\n";

$part_info_arr = slurm_get_specific_partition_info("ez79e8ezaeze46aze1ze3aer8rz7r897azr798r64f654ff65ds4f56dsf4dc13xcx2wc1wc31c");
if((gettype($part_info_arr)!="integer") && ($part_info_arr != NULL)) {
	echo "! slurm_get_specific_partition_info('ez79e8ezaeze46aze1ze3aer8rz7r897azr798r64f654ff65ds4f56dsf4dc13xcx2wc1wc31c')	:	SUCCESS";
} else if($part_info_arr == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on";
} else if($part_info_arr == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
} else if($part_info_arr == -1) {
	echo "[SLURM:ERROR] -1 : No partition by that name was found on your system";
} else {
	echo "[SLURM:ERROR] -4 : ?Unknown?";
}

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
[SLURM:ERROR] -1 : No partition by that name was found on your system
