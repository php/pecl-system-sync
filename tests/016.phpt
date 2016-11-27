--TEST--
SyncSharedMemory - named shared memory allocation reuse test.
--SKIPIF--
<?php if (!extension_loaded("sync"))  echo "skip"; ?>
--FILE--
<?php
	$shm = new SyncSharedMemory("Awesome_" . PHP_INT_SIZE, 150);

	var_dump($shm->first());
	var_dump($shm->size());
	var_dump($shm->write("Everything is awesome.", 1));
	var_dump($shm->read(1, 22));

	$shm2 = new SyncSharedMemory("Awesome_" . PHP_INT_SIZE, 150);

	var_dump($shm2->first());
	var_dump($shm2->size());
	var_dump($shm2->read(1, 22));

	unset($shm2);
	unset($shm);

	// Should be brand new.
	$shm = new SyncSharedMemory("Awesome_" . PHP_INT_SIZE, 150);

	var_dump($shm->first());
	var_dump($shm->size());
	var_dump($shm->write("Everything is awesome.", 1));
	var_dump($shm->read(1, 22));
?>
--EXPECT--
bool(true)
int(150)
int(22)
string(22) "Everything is awesome."
bool(false)
int(150)
string(22) "Everything is awesome."
bool(true)
int(150)
int(22)
string(22) "Everything is awesome."
