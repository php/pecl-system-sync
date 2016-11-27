CubicleSoft PHP Extension:  Synchronization Objects (sync)
==========================================================

The 'sync' extension introduces synchronization objects into PHP.  Named and unnamed Mutex, Semaphore, Event, Reader-Writer, and named Shared Memory objects provide OS-level synchronization mechanisms on both *NIX (POSIX shared memory and pthread shared memory synchronization required) and Windows platforms.  The extension comes with a test suite that integrates cleanly into 'make test'.

The 'sync' extension is a direct port of and compatible with the cross platform 'sync' library:  https://github.com/cubiclesoft/cross-platform-cpp

This extension uses the liberal MIT open source license.  And, of course, it sits on GitHub for all of that pull request and issue tracker goodness to easily submit changes and ideas respectively.

Details
-------

An exception may be thrown from the constructors if the target object can't be created for some reason.

All synchronization objects are attempted to be unlocked cleanly within PHP itself.  The exception is if an object's $autounlock option is initialized to false.  If PHP terminates a script and doesn't unlock the object, it can leave the object in an unpredictable state.

NOTE:  When using "named" objects, the initialization must be identical for a given name and have a specific purpose.  Reusing named objects for other purposes is not a good idea and will probably result in breaking both applications.  However, different object types can share the same name (e.g. a Mutex and an Event object can have the same name).

````
void SyncMutex::__construct([string $name = null])
  Constructs a named or unnamed mutex object.

bool SyncMutex::lock([int $wait = -1])
  Locks a mutex object.  $wait is in milliseconds.

bool SyncMutex::unlock([bool $all = false])
  Unlocks a mutex object.


void SyncSemaphore::__construct([string $name = null, [int $initialval = 1, [bool $autounlock = true]]])
  Constructs a named or unnamed semaphore object.  Don't set $autounlock to false unless you really know what you are doing.

bool SyncSemaphore::lock([int $wait = -1])
  Locks a semaphore object.  $wait is in milliseconds.

bool SyncSemaphore::unlock([int &$prevcount])
  Unlocks a semaphore object.


void SyncEvent::__construct([string $name = null, [bool $manual = false], [bool $prefire = false]])
  Constructs a named or unnamed event object.

bool SyncEvent::wait([int $wait = -1])
  Waits for an event object to fire.  $wait is in milliseconds.

bool SyncEvent::fire()
  Lets a thread through that is waiting.  Lets multiple threads through that are waiting if the event object is 'manual'.

bool SyncEvent::reset()
  Resets the event object state.  Only use when the event object is 'manual'.


void SyncReaderWriter::__construct([string $name = null, [bool $autounlock = true]])
  Constructs a named or unnamed reader-writer object.  Don't set $autounlock to false unless you really know what you are doing.

bool SyncReaderWriter::readlock([int $wait = -1])
  Read locks a reader-writer object.  $wait is in milliseconds.

bool SyncReaderWriter::writelock([int $wait = -1])
  Write locks a reader-writer object.  $wait is in milliseconds.

bool SyncReaderWriter::readunlock()
  Read unlocks a reader-writer object.

bool SyncReaderWriter::writeunlock()
  Write unlocks a reader-writer object.


void SyncSharedMemory::__construct(string $name, int $size)
  Constructs a named shared memory object.

bool SyncSharedMemory::first()
  Returns whether or not this shared memory segment is the first time accessed (i.e. not initialized).

int SyncSharedMemory::size()
  Returns the shared memory size.

int SyncSharedMemory::write(string $string, [int $start = 0])
  Copies data to shared memory.

string SyncSharedMemory::read([int $start = 0, [int $length = null]])
  Copies data from shared memory.
````

Usage Examples
--------------

Example Mutex usage:

```php
$mutex = new SyncMutex();

$mutex->lock();
...
$mutex->unlock();


$mutex2 = new SyncMutex("UniqueName");

if (!$mutex2->lock(3000))
{
	echo "Unable to lock mutex.";

	exit();
}

...

$mutex2->unlock();
```

Example Semaphore usage:

```php
$semaphore = new SyncSemaphore("LimitedResource_2_clients", 2);

if (!$semaphore->lock(3000))
{
	echo "Unable to lock semaphore.";

	exit();
}

...

$semaphore->unlock();
```

Example Event usage:

```php
// In a web application:
$event = new SyncEvent("GetAppReport");
$event->fire();

// In a cron job:
$event = new SyncEvent("GetAppReport");
$event->wait();
```

Example Reader-Writer usage:

```php
$readwrite = new SyncReaderWriter("FileCacheLock");
$readwrite->readlock();
...
$readwrite->readunlock();

$readwrite->writelock();
...
$readwrite->writeunlock();
```

Example Shared Memory usage:

```php
// You will probably need to protect shared memory with other synchronization objects.
// Shared memory goes away when the last reference to it disappears.
$mem = new SyncSharedMemory("AppReportName", 1024);
if ($mem->first())
{
	// Do first time initialization work here.
}

$result = $mem->write(json_encode(array("name" => "my_report.txt")));
```