@kphp_should_fail
/Failed to parse phpdoc type: something left at the end after parsing/
<?php

/**
 * @kphp-infer
 * @param int... $args
 */
function ints(...$args) {
  var_dump($args);
}

ints(1, 2);