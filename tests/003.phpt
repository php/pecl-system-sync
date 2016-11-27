--TEST--
SyncMutex - named mutex allocation, locking, and unlocking.
--SKIPIF--
<?php if (!extension_loaded("sync"))  echo "skip"; ?>
--FILE--
<?php
	$mutex = new SyncMutex("Awesome_" . PHP_INT_SIZE);

	var_dump($mutex->lock(0));
	var_dump($mutex->lock(0));
	var_dump($mutex->unlock());
	var_dump($mutex->unlock());
	var_dump($mutex->unlock());
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(false)
