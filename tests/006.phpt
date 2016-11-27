--TEST--
SyncSemaphore (1) - named semaphore allocation, locking, and unlocking.
--SKIPIF--
<?php if (!extension_loaded("sync"))  echo "skip"; ?>
--FILE--
<?php
	$semaphore = new SyncSemaphore("Awesome_" . PHP_INT_SIZE);

	var_dump($semaphore->lock());
	var_dump($semaphore->unlock());
	var_dump($semaphore->lock());
	$val = 5;
	var_dump($semaphore->unlock($val));
	var_dump($val);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
int(0)
