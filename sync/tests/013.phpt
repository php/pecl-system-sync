--TEST--
SyncReaderWriter - named reader-writer allocation, locking, and unlocking.
--SKIPIF--
<?php if (!extension_loaded("sync"))  echo "skip"; ?>
--FILE--
<?php
	$readwrite = new SyncReaderWriter("Awesome");

	var_dump($readwrite->readlock(0));
	var_dump($readwrite->readlock(0));
	var_dump($readwrite->readunlock(0));
	var_dump($readwrite->writelock(0));
	var_dump($readwrite->readunlock(0));
	var_dump($readwrite->readunlock(0));
	var_dump($readwrite->writelock(0));
	var_dump($readwrite->writelock(0));
	var_dump($readwrite->readlock(0));
	var_dump($readwrite->writeunlock(0));
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
