@kphp_should_fail
/Expected type:	float/
/Actual type:	float4/
<?php

function run(float $x) {}

/**@var float4*/
$x = 10;
run($x);
