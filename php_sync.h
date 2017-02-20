/*
	Direct port of and compatible with the cross platform 'sync' library:  https://github.com/cubiclesoft/cross-platform-cpp
	This source file is under the MIT license.
	(C) 2016 CubicleSoft.  All rights reserved.
*/

/* $Id$ */

#ifndef PHP_SYNC_H
#define PHP_SYNC_H

extern zend_module_entry sync_module_entry;
#define phpext_sync_ptr &sync_module_entry

#define PHP_SYNC_VERSION   "1.1.1"

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
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>

#ifdef __APPLE__
#include <mach/clock.h>
#include <mach/mach.h>
#ifndef SHM_NAME_MAX
#	define SHM_NAME_MAX 31
#endif
#else
#ifndef SHM_NAME_MAX
#	define SHM_NAME_MAX 255
#endif
#endif

#endif

PHP_MINIT_FUNCTION(sync);
PHP_MSHUTDOWN_FUNCTION(sync);
PHP_MINFO_FUNCTION(sync);

#if defined(PHP_WIN32)
typedef DWORD sync_ThreadIDType;
#else
typedef pthread_t sync_ThreadIDType;
#endif

#if PHP_MAJOR_VERSION >= 7
#define PHP_SYNC_PHP_5_zend_object_std
#define PHP_SYNC_PHP_7_zend_object_std   zend_object std;
#else
#define PHP_SYNC_PHP_5_zend_object_std   zend_object std;
#define PHP_SYNC_PHP_7_zend_object_std
#endif

/* Generic structures */
#if defined(PHP_WIN32)

/* Nothing for Windows at the moment. */

#else

/* Some platforms are broken even for unnamed semaphores (e.g. Mac OSX). */
/* This allows for implementing all semaphores directly, bypassing POSIX semaphores. */
typedef struct _sync_UnixSemaphoreWrapper {
	pthread_mutex_t *MxMutex;
	volatile uint32_t *MxCount;
	volatile uint32_t *MxMax;
	pthread_cond_t *MxCond;
} sync_UnixSemaphoreWrapper;

/* Implements a more efficient (and portable) event object interface than trying to use semaphores. */
typedef struct _sync_UnixEventWrapper {
	pthread_mutex_t *MxMutex;
	volatile char *MxManual;
	volatile char *MxSignaled;
	volatile uint32_t *MxWaiting;
	pthread_cond_t *MxCond;
} sync_UnixEventWrapper;

#endif


/* Mutex */
typedef struct _sync_Mutex_object {
	PHP_SYNC_PHP_5_zend_object_std

#if defined(PHP_WIN32)
	CRITICAL_SECTION MxWinCritSection;

	HANDLE MxWinMutex;
#else
	pthread_mutex_t MxPthreadCritSection;

	int MxNamed;
	char *MxMem;
	sync_UnixSemaphoreWrapper MxPthreadMutex;

#endif

	volatile sync_ThreadIDType MxOwnerID;
	volatile unsigned int MxCount;

	PHP_SYNC_PHP_7_zend_object_std
} sync_Mutex_object;


/* Semaphore */
typedef struct _sync_Semaphore_object {
	PHP_SYNC_PHP_5_zend_object_std

#if defined(PHP_WIN32)
	HANDLE MxWinSemaphore;
#else
	int MxNamed;
	char *MxMem;
	sync_UnixSemaphoreWrapper MxPthreadSemaphore;
#endif

	int MxAutoUnlock;
	volatile unsigned int MxCount;

	PHP_SYNC_PHP_7_zend_object_std
} sync_Semaphore_object;


/* Event */
typedef struct _sync_Event_object {
	PHP_SYNC_PHP_5_zend_object_std

#if defined(PHP_WIN32)
	HANDLE MxWinWaitEvent;
#else
	int MxNamed;
	char *MxMem;
	sync_UnixEventWrapper MxPthreadEvent;
#endif

	PHP_SYNC_PHP_7_zend_object_std
} sync_Event_object;


/* Reader-Writer */
typedef struct _sync_ReaderWriter_object {
	PHP_SYNC_PHP_5_zend_object_std

#if defined(PHP_WIN32)
	HANDLE MxWinRSemMutex, MxWinRSemaphore, MxWinRWaitEvent, MxWinWWaitMutex;
#else
	int MxNamed;
	char *MxMem;
	sync_UnixSemaphoreWrapper MxPthreadRCountMutex;
	volatile uint32_t *MxRCount;
	sync_UnixEventWrapper MxPthreadRWaitEvent;
	sync_UnixSemaphoreWrapper MxPthreadWWaitMutex;
#endif

	int MxAutoUnlock;
	volatile unsigned int MxReadLocks, MxWriteLock;

	PHP_SYNC_PHP_7_zend_object_std
} sync_ReaderWriter_object;


/* Named shared memory */
typedef struct _sync_SharedMemory_object {
	PHP_SYNC_PHP_5_zend_object_std

	int MxFirst;
	size_t MxSize;
	char *MxMem;

#if defined(PHP_WIN32)
	HANDLE MxFile;
#else
	char *MxMemInternal;
#endif

	PHP_SYNC_PHP_7_zend_object_std
} sync_SharedMemory_object;


#endif	/* PHP_SYNC_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
