--TEST--
Test V8Js::setModuleLoader : AMD
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

$JS = <<< EOT
define([
    "path/to/module1",
    "path/to/module2"
], function(module1, module2) {
    module1();
    module2();
});
EOT;

$module1 = <<< EOT
define([
    "./module3"
], function(module3) {
    module3();
    return function() {
        print("module1");
    };
})
EOT;

$module2 = <<< EOT
define([], function() {
    return function() {
        print("module2");
    };
})
EOT;

$module3 = <<< EOT
define([
    "../module4"
], function(module4) {
    module4();
    return function() {
        print("module3");
    };
})
EOT;

$module4 = <<< EOT
define([], function() {
    return function() {
        print("module4");
    };
})
EOT;

$error = <<< EOT
print("ERROR");
EOT;

$v8 = new V8Js();
$v8->setModuleLoader(function($module) {
    global $module1, $module2, $module3, $module4, $error;

    switch ($module) {
        case 'path/to/module1':
            return $module1;

        case 'path/to/module2':
            return $module2;

        case 'path/to/module3':
            return $module3;

        case 'path/module4':
            return $module4;

        default:
            return $error;
  }
});

$v8->executeString($JS, 'amd.js');
?>
===EOF===
--EXPECT--
module4module3module1module2===EOF===
