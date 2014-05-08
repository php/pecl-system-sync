--TEST--
Check for sync presence
--SKIPIF--
<?php if (!extension_loaded("sync"))  echo "skip"; ?>
--FILE--
<?php
echo "sync extension is available";
?>
--EXPECT--
sync extension is available
