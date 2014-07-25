/*
	Direct port of the cross platform 'sync' library:  https://github.com/cubiclesoft/cross-platform-cpp
	This source file is under the MIT, LGPL, or version 3.01 of the PHP license, your choice.
	(C) 2014 CubicleSoft.  All rights reserved.
*/

/* $Id$ */

#ifndef PHP_SYNC_H
#define PHP_SYNC_H

extern zend_module_entry sync_module_entry;
#define phpext_sync_ptr &sync_module_entry

#define PHP_SYNC_VERSION   "1.0.1"

#ifdef PHP_WIN32
#	define PHP_SYNC_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define PHP_SYNC_API __attribute__ ((visibility("default")))
#else
#	define PHP_SYNC_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#ifdef PHP_WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#endif

PHP_MINIT_FUNCTION(sync);
PHP_MSHUTDOWN_FUNCTION(sync);
PHP_MINFO_FUNCTION(sync);

#if defined(PHP_WIN32)
typedef DWORD sync_ThreadIDType;
#else
typedef pthread_t sync_ThreadIDType;
#endif

/* Mutex */
typedef struct _sync_Mutex_object {
	zend_object std;

#if defined(PHP_WIN32)
	CRITICAL_SECTION MxWinCritSection;

	HANDLE MxWinMutex;
#else
	pthread_mutex_t MxPthreadCritSection;

	sem_t *MxSemMutex;
	int MxAllocated;
#endif

	volatile sync_ThreadIDType MxOwnerID;
	volatile unsigned int MxCount;
} sync_Mutex_object;


/* Semaphore */
typedef struct _sync_Semaphore_object {
	zend_object std;

#if defined(PHP_WIN32)
	HANDLE MxWinSemaphore;
#else
	sem_t *MxSemSemaphore;
	int MxAllocated;
#endif

	int MxAutoUnlock;
	volatile unsigned int MxCount;
} sync_Semaphore_object;


/* Event */
typedef struct _sync_Event_object {
	zend_object std;

#if defined(PHP_WIN32)
	HANDLE MxWinWaitEvent;
#else
	sem_t *MxSemWaitMutex, *MxSemWaitEvent, *MxSemWaitCount, *MxSemWaitStatus;
	int MxAllocated;
	int MxManual;
#endif
} sync_Event_object;


/* Reader-Writer */
typedef struct _sync_ReaderWriter_object {
	zend_object std;

#if defined(PHP_WIN32)
	HANDLE MxWinRSemMutex, MxWinRSemaphore, MxWinRWaitEvent, MxWinWWaitMutex;
#else
	sem_t *MxSemRSemMutex, *MxSemRSemaphore, *MxSemRWaitEvent, *MxSemWWaitMutex;
	int MxAllocated;
#endif

	int MxAutoUnlock;
	volatile unsigned int MxReadLocks, MxWriteLock;
} sync_ReaderWriter_object;


#endif	/* PHP_SYNC_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
