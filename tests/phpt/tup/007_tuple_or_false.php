@ok
<?php
require_once 'polyfill/tuple-php-polyfill.php';

function getTuple($arg) {
    return tuple(1, ['str', 'array'], $arg);
}

function getTupleOrFalse($arg) {
    if(!$arg)
        return false;

    return getTuple($arg);
}

function getAlwaysFalse() {
    return getTupleOrFalse(false);
}

function demo1() {
    $t1 = getTupleOrFalse(88);
    echo $t1 ? "t1 arg = " . $t1[2] : "t1 is false", "\n";
}

function demo2() {
    $t1 = getTupleOrFalse(10);
    if ($t1 == false) {             // not === (cause === makes var for now)
        echo "t1 is false", "\n";
    } else {
        echo "t1 arg = " . $t1[2], "\n";
    }

    $t1 = getTupleOrFalse(20);
    if (!$t1) {
        echo "t1 is false", "\n";
    } else {
        echo "t1 arg = " . $t1[2], "\n";
    }

    $t1 = getTupleOrFalse(false);
    if (!$t1) {
        echo "t1 is false", "\n";
    } else {
        echo "t1 arg = " . $t1[2], "\n";
    }
}

function demo3() {
    $t1 = getAlwaysFalse();
    $t2 = $t1;

    if (!$t1 && !$t2) {
        echo "both false\n";
    }
}

function demo4() {
    $t = getAlwaysFalse();
    if($t) {
        // type inferring works (see .cpp and comment-guaranteed)
        $int = $t[0];
        $str_array = $t[1];
        $int = $t[2];
        $t[0] /*:= int */;
        $t[1] /*:= array < string > */;
        $t[2] /*:= OrFalse< int > */;
    }
}

demo1();
demo2();
demo3();
demo4();