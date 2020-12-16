@ok
<?php
require_once 'kphp_tester_include.php';

/**@param $x float4 */
function cmp_vars($x, float $y) {
    var_dump($x != $y);
    var_dump(!($x == $y));
    var_dump($x > $y);
    var_dump(!($x < $y));
}

function calc_cos() {
    /**@var float4*/
    $x = 10.123;

    /**@var float*/
    $y = 10.123;
    var_dump(cos($x) != cos($y));
}

function calc_sum_of_array() {
    /**@var float4[]*/
    $a = [0.123, 0.123, 0.123, 0.123];

    /**@var float[]*/
    $b = [0.123, 0.123, 0.123, 0.123];

    var_dump(array_sum($a) != array_sum($b));
}

/**
 * @param $tp tuple(float, int)
 * @param $tp4 tuple(float4, int)
 */
function cmp_tuples($tp, $tp4) {
    var_dump($tp != $tp4);

    /**@var tuple(float, int)*/
    $tp2 = tuple((float)$tp4[0], $tp4[1]);
    var_dump($tp2 == $tp4);
}

cmp_vars(1.123, 1.123);
calc_cos();
calc_sum_of_array();
cmp_tuples(tuple(.123, 536), tuple(.123, 536));
