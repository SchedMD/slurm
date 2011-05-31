--TEST--
Test function slurm_get_node_elements() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb_pv@hotmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_get_node_elements") or die("skip function slurm_get_node_elements() unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with its expected arguments ***\n";

$node_arr = slurm_get_node_elements();

if(is_array($node_arr)){
	if(count($node_arr)==0) {
		$node_arr = -1;
	}
}

if((gettype($node_arr)!="integer") && ($node_arr != NULL)) {
	echo "! slurm_get_node_elements()	:	SUCCESS";
} else if($nameArray == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
} else if($node_arr == -1) {
	echo "[SLURM:ERROR] -1 : No nodes could be found on your system";
}

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
! slurm_get_node_elements()	:	SUCCESS
