--TEST--
Test function slurm_get_control_configuration_keys() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb.peterv@gmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_get_control_configuration_keys") or die("skip function slurm_get_control_configuration_keys unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with its expected arguments ***\n";

$config_keys_arr = slurm_get_control_configuration_keys();

if(is_array($config_keys_arr)){
	if(count($config_keys_arr)==0) {
		$config_keys_arr = -1;
	}
}

if((gettype($config_keys_arr)=="array") && ($config_keys_arr != NULL)) {
	echo "! slurm_get_control_configuration_keys	:	SUCCESS";
} else if($config_keys_arr == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on";
} else if($config_keys_arr == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
} else if($config_keys_arr == -1) {
	echo "[SLURM:ERROR] -1 : No configuration data was found on your system";
}

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
! slurm_get_control_configuration_keys	:	SUCCESS
