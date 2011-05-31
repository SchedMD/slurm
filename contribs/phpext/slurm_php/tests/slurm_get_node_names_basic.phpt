--TEST--
Test function slurm_get_node_names() by calling it with its expected arguments
--FILE--
<?php

echo "*** Test by calling method or function with its expected arguments ***\n" ;

$nameArray = slurm_get_node_names();

if(is_array($nameArray)){
	if(count($nameArray)==0) {
		$nameArray = -1;
	}
}

if((gettype($nameArray)=="array") && ($nameArray != NULL)) {
	echo "! slurm_get_node_names	:	SUCCESS";
} else if($nameArray == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on";
} else if($nameArray == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
} else if($nameArray == -1) {
	echo "[SLURM:ERROR] -1 : No nodes reside on your system";
}

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
! slurm_get_node_names	:	SUCCESS
