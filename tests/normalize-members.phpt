--TEST--
Testing normalizing members
--DESCRIPTION--
This tests that normalizing members works without effort
--FILE--
<?php
class Test extends Threaded {
	public function run() { 
	}
}

$t = new Test();
$t[] = "one";
$t[] = "two";
$t["three"] = "three";

/* get a normal array */
$normal = (array) $t;
var_dump(is_array($normal));
?>
--EXPECT--
bool(true)
