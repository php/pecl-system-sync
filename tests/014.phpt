--TEST--
SyncReaderWriter - named reader-writer allocation, locking, and unlocking freeze test.
--SKIPIF--
<?php if (!extension_loaded("sync"))  echo "skip"; ?>
--FILE--
<?php
	$readwrite = new SyncReaderWriter("Awesome_" . PHP_INT_SIZE);

	var_dump($readwrite->readlock(0));
	var_dump($readwrite->readlock(0));
	var_dump($readwrite->readunlock());
	var_dump($readwrite->writelock(0));
	var_dump($readwrite->readunlock());
	var_dump($readwrite->readunlock());
	var_dump($readwrite->writelock(0));
	var_dump($readwrite->writelock(0));
	var_dump($readwrite->readlock(0));
	var_dump($readwrite->writeunlock());
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
bool(false)
bool(false)
bool(true)
