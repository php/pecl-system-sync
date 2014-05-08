--TEST--
SyncEvent - unnamed automatic event object allocation and firing.
--SKIPIF--
<?php if (!extension_loaded("sync"))  echo "skip"; ?>
--FILE--
<?php
	$event = new SyncEvent();

	var_dump($event->wait(0));
	var_dump($event->fire());
	var_dump($event->wait(0));
	var_dump($event->wait(0));
?>
--EXPECT--
bool(false)
bool(true)
bool(true)
bool(false)
