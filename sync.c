/*
	Direct port of and compatible with the cross platform 'sync' library:  https://github.com/cubiclesoft/cross-platform-cpp
	This source file is under the MIT license.
	(C) 2016 CubicleSoft.  All rights reserved.
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "zend_exceptions.h"
#include "ext/standard/info.h"
#if defined(PHP_WIN32) && PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION <= 4
#include "win32/php_stdint.h"
#else
#include <stdint.h>
#endif

/* Macros.  Oh joy. */
#if PHP_MAJOR_VERSION >= 7
#include "zend_portability.h"
#endif

#include "php_sync.h"
#define PORTABLE_default_zend_object_name   std

/* PHP_FE_END not defined in PHP < 5.3.7 */
#ifndef PHP_FE_END
#define PHP_FE_END {NULL, NULL, NULL}
#endif

/* For PHP 8 */
#ifndef TSRMLS_D
#define TSRMLS_D void
#define TSRMLS_DC
#define TSRMLS_C
#define TSRMLS_CC
#define TSRMLS_FETCH()
#endif

/* {{{ sync_module_entry
 */
zend_module_entry sync_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"sync",
	NULL,
	PHP_MINIT(sync),
	PHP_MSHUTDOWN(sync),
	NULL,
	NULL,
	PHP_MINFO(sync),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_SYNC_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_SYNC
ZEND_GET_MODULE(sync)
#endif

#ifndef INFINITE
#	define INFINITE   0xFFFFFFFF
#endif


/* Define some generic functions used several places. */
#if defined(PHP_WIN32)

/* Windows. */
sync_ThreadIDType sync_GetCurrentThreadID()
{
	return GetCurrentThreadId();
}

uint64_t sync_GetUnixMicrosecondTime()
{
	FILETIME TempTime;
	ULARGE_INTEGER TempTime2;
	uint64_t Result;

	GetSystemTimeAsFileTime(&TempTime);
	TempTime2.HighPart = TempTime.dwHighDateTime;
	TempTime2.LowPart = TempTime.dwLowDateTime;
	Result = TempTime2.QuadPart;

	Result = (Result / 10) - (uint64_t)11644473600000000ULL;

	return Result;
}

#else

/* POSIX pthreads. */
sync_ThreadIDType sync_GetCurrentThreadID()
{
	return pthread_self();
}

uint64_t sync_GetUnixMicrosecondTime()
{
	struct timeval TempTime;

	if (gettimeofday(&TempTime, NULL))  return 0;

	return (uint64_t)((uint64_t)TempTime.tv_sec * (uint64_t)1000000 + (uint64_t)TempTime.tv_usec);
}

/* Dear Apple:  You hire plenty of developers, so please fix your OS. */
int sync_CSGX__ClockGetTimeRealtime(struct timespec *ts)
{
#ifdef __APPLE__
	clock_serv_t cclock;
	mach_timespec_t mts;

	if (host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock) != KERN_SUCCESS)  return -1;
	if (clock_get_time(cclock, &mts) != KERN_SUCCESS)  return -1;
	if (mach_port_deallocate(mach_task_self(), cclock) != KERN_SUCCESS)  return -1;

	ts->tv_sec = mts.tv_sec;
	ts->tv_nsec = mts.tv_nsec;

	return 0;
#else
	return clock_gettime(CLOCK_REALTIME, ts);
#endif
}

size_t sync_GetUnixSystemAlignmentSize()
{
	struct {
		int MxInt;
	} x;

	struct {
		int MxInt;
		char MxChar;
	} y;

	return sizeof(y) - sizeof(x);
}

size_t sync_AlignUnixSize(size_t Size)
{
	size_t AlignSize = sync_GetUnixSystemAlignmentSize();

	if (Size % AlignSize)  Size += AlignSize - (Size % AlignSize);

	return Size;
}

int sync_InitUnixNamedMem(char **ResultMem, size_t *StartPos, const char *Prefix, const char *Name, size_t Size)
{
	int Result = -1;
	*ResultMem = NULL;
	*StartPos = (Name != NULL ? sync_AlignUnixSize(1) + sync_AlignUnixSize(sizeof(pthread_mutex_t)) + sync_AlignUnixSize(sizeof(uint32_t)) : 0);

	/* First byte indicates initialization status (0 = completely uninitialized, 1 = first mutex initialized, 2 = ready). */
	/* Next few bytes are a shared mutex object. */
	/* Size bytes follow for whatever. */
	Size += *StartPos;
	Size = sync_AlignUnixSize(Size);

	if (Name == NULL)
	{
		*ResultMem = (char *)ecalloc(1, Size);

		Result = 0;
	}
	else
	{
		/* Deal with really small name limits with a pseudo-hash. */
		char Name2[SHM_NAME_MAX], Nums[50];
		size_t x, x2 = 0, y = strlen(Prefix), z = 0;

		memset(Name2, 0, sizeof(Name2));

		for (x = 0; x < y; x++)
		{
			Name2[x2] = (char)(((unsigned int)(unsigned char)Name2[x2]) * 37 + ((unsigned int)(unsigned char)Prefix[x]));
			x2++;

			if (x2 == sizeof(Name2) - 1)
			{
				x2 = 1;
				z++;
			}
		}

		sprintf(Nums, "-%u-%u-", (unsigned int)sync_GetUnixSystemAlignmentSize(), (unsigned int)Size);

		y = strlen(Nums);
		for (x = 0; x < y; x++)
		{
			Name2[x2] = (char)(((unsigned int)(unsigned char)Name2[x2]) * 37 + ((unsigned int)(unsigned char)Nums[x]));
			x2++;

			if (x2 == sizeof(Name2) - 1)
			{
				x2 = 1;
				z++;
			}
		}

		y = strlen(Name);
		for (x = 0; x < y; x++)
		{
			Name2[x2] = (char)(((unsigned int)(unsigned char)Name2[x2]) * 37 + ((unsigned int)(unsigned char)Name[x]));
			x2++;

			if (x2 == sizeof(Name2) - 1)
			{
				x2 = 1;
				z++;
			}
		}

		/* Normalize the alphabet if it looped. */
		if (z)
		{
			unsigned char TempChr;
			y = (z > 1 ? sizeof(Name2) - 1 : x2);
			for (x = 1; x < y; x++)
			{
				TempChr = ((unsigned char)Name2[x]) & 0x3F;

				if (TempChr < 10)  TempChr += '0';
				else if (TempChr < 36)  TempChr = TempChr - 10 + 'A';
				else if (TempChr < 62)  TempChr = TempChr - 36 + 'a';
				else if (TempChr == 62)  TempChr = '_';
				else  TempChr = '-';

				Name2[x] = (char)TempChr;
			}
		}

		for (x = 1; x < sizeof(Name2) && Name2[x]; x++)
		{
			if (Name2[x] == '\\' || Name2[x] == '/')  Name2[x] = '_';
		}

		pthread_mutex_t *MutexPtr;
		uint32_t *RefCountPtr;

		/* Attempt to create the named shared memory object. */
		mode_t PrevMask = umask(0);
		int fp = shm_open(Name2, O_RDWR | O_CREAT | O_EXCL, 0666);
		if (fp > -1)
		{
			/* Ignore platform errors (for now). */
			while (ftruncate(fp, Size) < 0 && errno == EINTR)
			{
			}

			*ResultMem = (char *)mmap(NULL, Size, PROT_READ | PROT_WRITE, MAP_SHARED, fp, 0);
			if ((*ResultMem) == MAP_FAILED)  *ResultMem = NULL;
			else
			{
				pthread_mutexattr_t MutexAttr;

				pthread_mutexattr_init(&MutexAttr);
				pthread_mutexattr_setpshared(&MutexAttr, PTHREAD_PROCESS_SHARED);

				MutexPtr = (pthread_mutex_t *)((*ResultMem) + sync_AlignUnixSize(1));
				RefCountPtr = (uint32_t *)((*ResultMem) + sync_AlignUnixSize(1) + sync_AlignUnixSize(sizeof(pthread_mutex_t)));

				pthread_mutex_init(MutexPtr, &MutexAttr);
				pthread_mutex_lock(MutexPtr);

				(*ResultMem)[0] = '\x01';
				RefCountPtr[0] = 1;

				Result = 0;
			}

			close(fp);
		}
		else
		{
			/* Attempt to open the named shared memory object. */
			fp = shm_open(Name2, O_RDWR, 0666);
			if (fp > -1)
			{
				/* Ignore platform errors (for now). */
				while (ftruncate(fp, Size) < 0 && errno == EINTR)
				{
				}

				*ResultMem = (char *)mmap(NULL, Size, PROT_READ | PROT_WRITE, MAP_SHARED, fp, 0);
				if (*ResultMem == MAP_FAILED)  ResultMem = NULL;
				else
				{
					/* Wait until the space is fully initialized. */
					if ((*ResultMem)[0] == '\x00')
					{
						while ((*ResultMem)[0] == '\x00')
						{
							usleep(2000);
						}
					}

					char *MemPtr = (*ResultMem) + sync_AlignUnixSize(1);
					MutexPtr = (pthread_mutex_t *)(MemPtr);
					MemPtr += sync_AlignUnixSize(sizeof(pthread_mutex_t));

					RefCountPtr = (uint32_t *)(MemPtr);
					MemPtr += sync_AlignUnixSize(sizeof(uint32_t));

					pthread_mutex_lock(MutexPtr);

					if (RefCountPtr[0])  Result = 1;
					else
					{
						/* If this is the first reference, reset the RAM to 0's for platform consistency to force a rebuild of the object. */
						memset(MemPtr, 0, Size);

						Result = 0;
					}

					RefCountPtr[0]++;

					pthread_mutex_unlock(MutexPtr);
				}

				close(fp);
			}
		}

		umask(PrevMask);
	}

	return Result;
}

void sync_UnixNamedMemReady(char *MemPtr)
{
	pthread_mutex_unlock((pthread_mutex_t *)(MemPtr + sync_AlignUnixSize(1)));
}

void sync_UnmapUnixNamedMem(char *MemPtr, size_t Size)
{
	pthread_mutex_t *MutexPtr;
	uint32_t *RefCountPtr;

	char *MemPtr2 = MemPtr + sync_AlignUnixSize(1);
	MutexPtr = (pthread_mutex_t *)(MemPtr2);
	MemPtr2 += sync_AlignUnixSize(sizeof(pthread_mutex_t));

	RefCountPtr = (uint32_t *)(MemPtr2);

	pthread_mutex_lock(MutexPtr);
	if (RefCountPtr[0])  RefCountPtr[0]--;
	pthread_mutex_unlock(MutexPtr);

	munmap(MemPtr, sync_AlignUnixSize(1) + sync_AlignUnixSize(sizeof(pthread_mutex_t)) + sync_AlignUnixSize(sizeof(uint32_t)) + Size);
}

/* Basic *NIX Semaphore functions. */
size_t sync_GetUnixSemaphoreSize()
{
	return sync_AlignUnixSize(sizeof(pthread_mutex_t)) + sync_AlignUnixSize(sizeof(uint32_t)) + sync_AlignUnixSize(sizeof(uint32_t)) + sync_AlignUnixSize(sizeof(pthread_cond_t));
}

void sync_GetUnixSemaphore(sync_UnixSemaphoreWrapper *Result, char *Mem)
{
	Result->MxMutex = (pthread_mutex_t *)(Mem);
	Mem += sync_AlignUnixSize(sizeof(pthread_mutex_t));

	Result->MxCount = (uint32_t *)(Mem);
	Mem += sync_AlignUnixSize(sizeof(uint32_t));

	Result->MxMax = (uint32_t *)(Mem);
	Mem += sync_AlignUnixSize(sizeof(uint32_t));

	Result->MxCond = (pthread_cond_t *)(Mem);
}

void sync_InitUnixSemaphore(sync_UnixSemaphoreWrapper *UnixSemaphore, int Shared, uint32_t Start, uint32_t Max)
{
	pthread_mutexattr_t MutexAttr;
	pthread_condattr_t CondAttr;

	pthread_mutexattr_init(&MutexAttr);
	pthread_condattr_init(&CondAttr);

	if (Shared)
	{
		pthread_mutexattr_setpshared(&MutexAttr, PTHREAD_PROCESS_SHARED);
		pthread_condattr_setpshared(&CondAttr, PTHREAD_PROCESS_SHARED);
	}

	pthread_mutex_init(UnixSemaphore->MxMutex, &MutexAttr);
	if (Start > Max)  Start = Max;
	UnixSemaphore->MxCount[0] = Start;
	UnixSemaphore->MxMax[0] = Max;
	pthread_cond_init(UnixSemaphore->MxCond, &CondAttr);

	pthread_condattr_destroy(&CondAttr);
	pthread_mutexattr_destroy(&MutexAttr);
}

int sync_WaitForUnixSemaphore(sync_UnixSemaphoreWrapper *UnixSemaphore, uint32_t Wait)
{
	if (Wait == 0)
	{
		/* Avoid the scenario of deadlock on the semaphore itself for 0 wait. */
		if (pthread_mutex_trylock(UnixSemaphore->MxMutex) != 0)  return 0;
	}
	else
	{
		if (pthread_mutex_lock(UnixSemaphore->MxMutex) != 0)  return 0;
	}

	int Result = 0;

	if (UnixSemaphore->MxCount[0])
	{
		UnixSemaphore->MxCount[0]--;

		Result = 1;
	}
	else if (Wait == INFINITE)
	{
		int Result2;
		do
		{
			Result2 = pthread_cond_wait(UnixSemaphore->MxCond, UnixSemaphore->MxMutex);
			if (Result2 != 0)  break;
		} while (!UnixSemaphore->MxCount[0]);

		if (Result2 == 0)
		{
			UnixSemaphore->MxCount[0]--;

			Result = 1;
		}
	}
	else if (Wait == 0)
	{
		/* Failed to obtain lock.  Nothing to do. */
	}
	else
	{
		struct timespec TempTime;

		if (sync_CSGX__ClockGetTimeRealtime(&TempTime) == -1)  return 0;
		TempTime.tv_sec += Wait / 1000;
		TempTime.tv_nsec += (Wait % 1000) * 1000000;
		TempTime.tv_sec += TempTime.tv_nsec / 1000000000;
		TempTime.tv_nsec = TempTime.tv_nsec % 1000000000;

		int Result2;
		do
		{
			/* Some platforms have pthread_cond_timedwait() but not pthread_mutex_timedlock() or sem_timedwait() (e.g. Mac OSX). */
			Result2 = pthread_cond_timedwait(UnixSemaphore->MxCond, UnixSemaphore->MxMutex, &TempTime);
			if (Result2 != 0)  break;
		} while (!UnixSemaphore->MxCount[0]);

		if (Result2 == 0)
		{
			UnixSemaphore->MxCount[0]--;

			Result = 1;
		}
	}

	pthread_mutex_unlock(UnixSemaphore->MxMutex);

	return Result;
}

int sync_ReleaseUnixSemaphore(sync_UnixSemaphoreWrapper *UnixSemaphore, uint32_t *PrevVal)
{
	if (pthread_mutex_lock(UnixSemaphore->MxMutex) != 0)  return 0;

	if (PrevVal != NULL)  *PrevVal = UnixSemaphore->MxCount[0];
	UnixSemaphore->MxCount[0]++;
	if (UnixSemaphore->MxCount[0] > UnixSemaphore->MxMax[0])  UnixSemaphore->MxCount[0] = UnixSemaphore->MxMax[0];

	/* Let a waiting thread have at it. */
	pthread_cond_signal(UnixSemaphore->MxCond);

	pthread_mutex_unlock(UnixSemaphore->MxMutex);

	return 1;
}

void sync_FreeUnixSemaphore(sync_UnixSemaphoreWrapper *UnixSemaphore)
{
	pthread_mutex_destroy(UnixSemaphore->MxMutex);
	pthread_cond_destroy(UnixSemaphore->MxCond);
}

/* Basic *NIX Event functions. */
size_t sync_GetUnixEventSize()
{
	return sync_AlignUnixSize(sizeof(pthread_mutex_t)) + sync_AlignUnixSize(2) + sync_AlignUnixSize(sizeof(uint32_t)) + sync_AlignUnixSize(sizeof(pthread_cond_t));
}

void sync_GetUnixEvent(sync_UnixEventWrapper *Result, char *Mem)
{
	Result->MxMutex = (pthread_mutex_t *)(Mem);
	Mem += sync_AlignUnixSize(sizeof(pthread_mutex_t));

	Result->MxManual = Mem;
	Result->MxSignaled = Mem + 1;
	Mem += sync_AlignUnixSize(2);

	Result->MxWaiting = (uint32_t *)(Mem);
	Mem += sync_AlignUnixSize(sizeof(uint32_t));

	Result->MxCond = (pthread_cond_t *)(Mem);
}

void sync_InitUnixEvent(sync_UnixEventWrapper *UnixEvent, int Shared, int Manual, int Signaled)
{
	pthread_mutexattr_t MutexAttr;
	pthread_condattr_t CondAttr;

	pthread_mutexattr_init(&MutexAttr);
	pthread_condattr_init(&CondAttr);

	if (Shared)
	{
		pthread_mutexattr_setpshared(&MutexAttr, PTHREAD_PROCESS_SHARED);
		pthread_condattr_setpshared(&CondAttr, PTHREAD_PROCESS_SHARED);
	}

	pthread_mutex_init(UnixEvent->MxMutex, &MutexAttr);
	UnixEvent->MxManual[0] = (Manual ? '\x01' : '\x00');
	UnixEvent->MxSignaled[0] = (Signaled ? '\x01' : '\x00');
	UnixEvent->MxWaiting[0] = 0;
	pthread_cond_init(UnixEvent->MxCond, &CondAttr);

	pthread_condattr_destroy(&CondAttr);
	pthread_mutexattr_destroy(&MutexAttr);
}

int sync_WaitForUnixEvent(sync_UnixEventWrapper *UnixEvent, uint32_t Wait)
{
	if (Wait == 0)
	{
		/* Avoid the scenario of deadlock on the semaphore itself for 0 wait. */
		if (pthread_mutex_trylock(UnixEvent->MxMutex) != 0)  return 0;
	}
	else
	{
		if (pthread_mutex_lock(UnixEvent->MxMutex) != 0)  return 0;
	}

	int Result = 0;

	/* Avoid a potential starvation issue by only allowing signaled manual events OR if there are no other waiting threads. */
	if (UnixEvent->MxSignaled[0] != '\x00' && (UnixEvent->MxManual[0] != '\x00' || !UnixEvent->MxWaiting[0]))
	{
		/* Reset auto events. */
		if (UnixEvent->MxManual[0] == '\x00')  UnixEvent->MxSignaled[0] = '\x00';

		Result = 1;
	}
	else if (Wait == INFINITE)
	{
		UnixEvent->MxWaiting[0]++;

		int Result2;
		do
		{
			Result2 = pthread_cond_wait(UnixEvent->MxCond, UnixEvent->MxMutex);
			if (Result2 != 0)  break;
		} while (UnixEvent->MxSignaled[0] == '\x00');

		UnixEvent->MxWaiting[0]--;

		if (Result2 == 0)
		{
			/* Reset auto events. */
			if (UnixEvent->MxManual[0] == '\x00')  UnixEvent->MxSignaled[0] = '\x00';

			Result = 1;
		}
	}
	else if (Wait == 0)
	{
		/* Failed to obtain lock.  Nothing to do. */
	}
	else
	{
		struct timespec TempTime;

		if (sync_CSGX__ClockGetTimeRealtime(&TempTime) == -1)
		{
			pthread_mutex_unlock(UnixEvent->MxMutex);

			return 0;
		}

		TempTime.tv_sec += Wait / 1000;
		TempTime.tv_nsec += (Wait % 1000) * 1000000;
		TempTime.tv_sec += TempTime.tv_nsec / 1000000000;
		TempTime.tv_nsec = TempTime.tv_nsec % 1000000000;

		UnixEvent->MxWaiting[0]++;

		int Result2;
		do
		{
			/* Some platforms have pthread_cond_timedwait() but not pthread_mutex_timedlock() or sem_timedwait() (e.g. Mac OSX). */
			Result2 = pthread_cond_timedwait(UnixEvent->MxCond, UnixEvent->MxMutex, &TempTime);
			if (Result2 != 0)  break;
		} while (UnixEvent->MxSignaled[0] == '\x00');

		UnixEvent->MxWaiting[0]--;

		if (Result2 == 0)
		{
			/* Reset auto events. */
			if (UnixEvent->MxManual[0] == '\x00')  UnixEvent->MxSignaled[0] = '\x00';

			Result = 1;
		}
	}

	pthread_mutex_unlock(UnixEvent->MxMutex);

	return Result;
}

int sync_FireUnixEvent(sync_UnixEventWrapper *UnixEvent)
{
	if (pthread_mutex_lock(UnixEvent->MxMutex) != 0)  return 0;

	UnixEvent->MxSignaled[0] = '\x01';

	/* Let all waiting threads through for manual events, otherwise just one waiting thread (if any). */
	if (UnixEvent->MxManual[0] != '\x00')  pthread_cond_broadcast(UnixEvent->MxCond);
	else  pthread_cond_signal(UnixEvent->MxCond);

	pthread_mutex_unlock(UnixEvent->MxMutex);

	return 1;
}

/* Only call for manual events. */
int sync_ResetUnixEvent(sync_UnixEventWrapper *UnixEvent)
{
	if (UnixEvent->MxManual[0] == '\x00')  return 0;
	if (pthread_mutex_lock(UnixEvent->MxMutex) != 0)  return 0;

	UnixEvent->MxSignaled[0] = '\x00';

	pthread_mutex_unlock(UnixEvent->MxMutex);

	return 1;
}

void sync_FreeUnixEvent(sync_UnixEventWrapper *UnixEvent)
{
	pthread_mutex_destroy(UnixEvent->MxMutex);
	pthread_cond_destroy(UnixEvent->MxCond);
}

#endif


/* {{{ PHP 5 and 7 crossover compatibility defines and functions */
#if PHP_MAJOR_VERSION >= 7

#define PORTABLE_new_zend_object_func(funcname)   zend_object *funcname(zend_class_entry *ce)
#define PORTABLE_new_zend_object_return_var
#define PORTABLE_new_zend_object_return_var_ref   NULL
#define PORTABLE_allocate_zend_object(size, ce)   ecalloc(1, (size) + zend_object_properties_size(ce));
#define PORTABLE_new_zend_object_return(std)   return (std)

void PORTABLE_InitZendObject(void *obj, zend_object *std, void *unused1, void *FreeFunc, zend_object_handlers *Handlers, zend_class_entry *ce TSRMLS_DC)
{
	/* Initialize Zend. */
	zend_object_std_init(std, ce TSRMLS_CC);
	object_properties_init(std, ce);

	/* Initialize destructor callback. */
	std->handlers = Handlers;
}

typedef zend_long   PORTABLE_ZPP_ARG_long;
typedef size_t   PORTABLE_ZPP_ARG_size;
typedef zval *   PORTABLE_ZPP_ARG_zval_ref;
#define PORTABLE_ZPP_ARG_zval_ref_deref(var)   var

static inline void *PHP_7_zend_object_to_object(zend_object *std)
{
	return (void *)((char *)std - std->handlers->offset);
}

#define PORTABLE_zend_object_store_get_object()   PHP_7_zend_object_to_object(Z_OBJ_P(getThis()))

#define PORTABLE_free_zend_object_func(funcname)   void funcname(zend_object *object)
#define PORTABLE_free_zend_object_get_object(object)   PHP_7_zend_object_to_object(object)
#define PORTABLE_free_zend_object_free_object(obj)   zend_object_std_dtor(&obj->std);

#define PORTABLE_RETURN_STRINGL(str, len)   RETURN_STRINGL(str, len)

#else

#define PORTABLE_new_zend_object_func(funcname)   zend_object_value funcname(zend_class_entry *ce TSRMLS_DC)
#define PORTABLE_new_zend_object_return_var   zend_object_value retval
#define PORTABLE_new_zend_object_return_var_ref   &retval
#define PORTABLE_new_zend_object_return(std)   return retval
#define PORTABLE_allocate_zend_object(size, ce)   ecalloc(1, (size));

void PORTABLE_InitZendObject(void *obj, zend_object *std, zend_object_value *retval, zend_objects_free_object_storage_t FreeFunc, zend_object_handlers *Handlers, zend_class_entry *ce TSRMLS_DC)
{
#if PHP_VERSION_ID < 50399
	zval *tmp;
#endif

	/* Initialize Zend. */
	zend_object_std_init(std, ce TSRMLS_CC);
#if PHP_VERSION_ID < 50399
	zend_hash_copy(std->properties, &ce->default_properties, (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *));
#else
	object_properties_init(std, ce);
#endif

	/* Initialize destructor callback. */
	retval->handle = zend_objects_store_put(obj, (zend_objects_store_dtor_t)zend_objects_destroy_object, FreeFunc, NULL TSRMLS_CC);
	retval->handlers = Handlers;
}

typedef long   PORTABLE_ZPP_ARG_long;
typedef int   PORTABLE_ZPP_ARG_size;
typedef zval **   PORTABLE_ZPP_ARG_zval_ref;
#define PORTABLE_ZPP_ARG_zval_ref_deref(var)   *var

#define PORTABLE_zend_object_store_get_object()   zend_object_store_get_object(getThis() TSRMLS_CC)

#define PORTABLE_free_zend_object_func(funcname)   void funcname(void *object TSRMLS_DC)
#define PORTABLE_free_zend_object_get_object(object)   object;
#define PORTABLE_free_zend_object_free_object(obj)   zend_object_std_dtor(&obj->std TSRMLS_CC);  efree(obj);

#define PORTABLE_RETURN_STRINGL(str, len)   RETURN_STRINGL(str, len, 1)

#endif
/* }}} */


/* Mutex */
PHP_SYNC_API zend_class_entry *sync_Mutex_ce;
static zend_object_handlers sync_Mutex_object_handlers;

PORTABLE_free_zend_object_func(sync_Mutex_free_object);

/* {{{ Initialize internal Mutex structure. */
PORTABLE_new_zend_object_func(sync_Mutex_create_object)
{
	PORTABLE_new_zend_object_return_var;
	sync_Mutex_object *obj;

	/* Create the object. */
	obj = (sync_Mutex_object *)PORTABLE_allocate_zend_object(sizeof(sync_Mutex_object), ce);

	PORTABLE_InitZendObject(obj, &obj->std, PORTABLE_new_zend_object_return_var_ref, sync_Mutex_free_object, &sync_Mutex_object_handlers, ce TSRMLS_CC);

	/* Initialize Mutex information. */
#if defined(PHP_WIN32)
	obj->MxWinMutex = NULL;
	InitializeCriticalSection(&obj->MxWinCritSection);
#else
	obj->MxNamed = 0;
	obj->MxMem = NULL;
	pthread_mutex_init(&obj->MxPthreadCritSection, NULL);
#endif
	obj->MxOwnerID = 0;
	obj->MxCount = 0;

	PORTABLE_new_zend_object_return(&obj->std);
}
/* }}} */

/* {{{ Unlocks a mutex. */
int sync_Mutex_unlock_internal(sync_Mutex_object *obj, int all)
{
#if defined(PHP_WIN32)

	EnterCriticalSection(&obj->MxWinCritSection);

	/* Make sure the mutex exists and make sure it is owned by the calling thread. */
	if (obj->MxWinMutex == NULL || obj->MxOwnerID != sync_GetCurrentThreadID())
	{
		LeaveCriticalSection(&obj->MxWinCritSection);

		return 0;
	}

	if (all)  obj->MxCount = 1;

	obj->MxCount--;
	if (!obj->MxCount)
	{
		obj->MxOwnerID = 0;

		/* Release the mutex. */
		ReleaseMutex(obj->MxWinMutex);
	}

	LeaveCriticalSection(&obj->MxWinCritSection);

#else

	if (pthread_mutex_lock(&obj->MxPthreadCritSection) != 0)  return 0;

	/* Make sure the mutex exists and make sure it is owned by the calling thread. */
	if (obj->MxMem == NULL || obj->MxOwnerID != sync_GetCurrentThreadID())
	{
		pthread_mutex_unlock(&obj->MxPthreadCritSection);

		return 0;
	}

	if (all)  obj->MxCount = 1;

	obj->MxCount--;
	if (!obj->MxCount)
	{
		obj->MxOwnerID = 0;

		/* Release the mutex. */
		sync_ReleaseUnixSemaphore(&obj->MxPthreadMutex, NULL);
	}

	pthread_mutex_unlock(&obj->MxPthreadCritSection);

#endif

	return 1;
}
/* }}} */

/* {{{ Free internal Mutex structure. */
PORTABLE_free_zend_object_func(sync_Mutex_free_object)
{
	sync_Mutex_object *obj = (sync_Mutex_object *)PORTABLE_free_zend_object_get_object(object);

	sync_Mutex_unlock_internal(obj, 1);

#if defined(PHP_WIN32)
	if (obj->MxWinMutex != NULL)  CloseHandle(obj->MxWinMutex);
	DeleteCriticalSection(&obj->MxWinCritSection);
#else
	if (obj->MxMem != NULL)
	{
		if (obj->MxNamed)  sync_UnmapUnixNamedMem(obj->MxMem, sync_GetUnixSemaphoreSize());
		else
		{
			sync_FreeUnixSemaphore(&obj->MxPthreadMutex);

			efree(obj->MxMem);
		}
	}

	pthread_mutex_destroy(&obj->MxPthreadCritSection);
#endif

	PORTABLE_free_zend_object_free_object(obj);
}
/* }}} */

/* {{{ proto void Sync_Mutex::__construct([string $name = null])
   Constructs a named or unnamed mutex object. */
PHP_METHOD(sync_Mutex, __construct)
{
	char *name = NULL;
	PORTABLE_ZPP_ARG_size name_len;
	sync_Mutex_object *obj;
#if defined(PHP_WIN32)
	SECURITY_ATTRIBUTES SecAttr;
#else
	size_t Pos, TempSize;
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s!", &name, &name_len) == FAILURE)  return;

	obj = (sync_Mutex_object *)PORTABLE_zend_object_store_get_object();

	if (name_len < 1)  name = NULL;

#if defined(PHP_WIN32)

	SecAttr.nLength = sizeof(SecAttr);
	SecAttr.lpSecurityDescriptor = NULL;
	SecAttr.bInheritHandle = TRUE;

	obj->MxWinMutex = CreateMutexA(&SecAttr, FALSE, name);
	if (obj->MxWinMutex == NULL)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Mutex could not be created", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Mutex could not be created", 0 TSRMLS_CC);
#endif
		return;
	}

#else

	TempSize = sync_GetUnixSemaphoreSize();
	obj->MxNamed = (name != NULL ? 1 : 0);
	int Result = sync_InitUnixNamedMem(&obj->MxMem, &Pos, "/Sync_Mutex", name, TempSize);

	if (Result < 0)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Mutex could not be created", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Mutex could not be created", 0 TSRMLS_CC);
#endif

		return;
	}

	sync_GetUnixSemaphore(&obj->MxPthreadMutex, obj->MxMem + Pos);

	/* Handle the first time this mutex has been opened. */
	if (Result == 0)
	{
		sync_InitUnixSemaphore(&obj->MxPthreadMutex, obj->MxNamed, 1, 1);

		if (obj->MxNamed)  sync_UnixNamedMemReady(obj->MxMem);
	}

#endif
}
/* }}} */

/* {{{ proto bool Sync_Mutex::lock([int $wait = -1])
   Locks a mutex object. */
PHP_METHOD(sync_Mutex, lock)
{
	PORTABLE_ZPP_ARG_long wait = -1;
	sync_Mutex_object *obj;
#if defined(PHP_WIN32)
	DWORD Result;
#else
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &wait) == FAILURE)  return;

	obj = (sync_Mutex_object *)PORTABLE_zend_object_store_get_object();

#if defined(PHP_WIN32)

	EnterCriticalSection(&obj->MxWinCritSection);

	/* Check to see if this is already owned by the calling thread. */
	if (obj->MxOwnerID == sync_GetCurrentThreadID())
	{
		obj->MxCount++;
		LeaveCriticalSection(&obj->MxWinCritSection);

		RETURN_TRUE;
	}

	LeaveCriticalSection(&obj->MxWinCritSection);

	/* Acquire the mutex. */
	Result = WaitForSingleObject(obj->MxWinMutex, (DWORD)(wait > -1 ? wait : INFINITE));
	if (Result != WAIT_OBJECT_0)  RETURN_FALSE;

	EnterCriticalSection(&obj->MxWinCritSection);
	obj->MxOwnerID = sync_GetCurrentThreadID();
	obj->MxCount = 1;
	LeaveCriticalSection(&obj->MxWinCritSection);

#else

	if (pthread_mutex_lock(&obj->MxPthreadCritSection) != 0)
	{
		
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Unable to acquire mutex critical section", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Unable to acquire mutex critical section", 0 TSRMLS_CC);
#endif

		RETURN_FALSE;
	}

	/* Check to see if this mutex is already owned by the calling thread. */
	if (obj->MxOwnerID == sync_GetCurrentThreadID())
	{
		obj->MxCount++;
		pthread_mutex_unlock(&obj->MxPthreadCritSection);

		RETURN_TRUE;
	}

	pthread_mutex_unlock(&obj->MxPthreadCritSection);

	if (!sync_WaitForUnixSemaphore(&obj->MxPthreadMutex, (uint32_t)(wait > -1 ? wait : INFINITE)))  RETURN_FALSE;

	pthread_mutex_lock(&obj->MxPthreadCritSection);
	obj->MxOwnerID = sync_GetCurrentThreadID();
	obj->MxCount = 1;
	pthread_mutex_unlock(&obj->MxPthreadCritSection);

#endif

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool Sync_Mutex::unlock([bool $all = false])
   Unlocks a mutex object. */
PHP_METHOD(sync_Mutex, unlock)
{
	PORTABLE_ZPP_ARG_long all = 0;
	sync_Mutex_object *obj;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &all) == FAILURE)  return;

	obj = (sync_Mutex_object *)PORTABLE_zend_object_store_get_object();

	if (!sync_Mutex_unlock_internal(obj, all))  RETURN_FALSE;

	RETURN_TRUE;
}
/* }}} */


ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_mutex___construct, 0, 0, 0)
	ZEND_ARG_INFO(0, name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_mutex_lock, 0, 0, 0)
	ZEND_ARG_INFO(0, wait)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_mutex_unlock, 0, 0, 0)
	ZEND_ARG_INFO(0, all)
ZEND_END_ARG_INFO()

static const zend_function_entry sync_Mutex_methods[] = {
	PHP_ME(sync_Mutex, __construct, arginfo_sync_mutex___construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	PHP_ME(sync_Mutex, lock, arginfo_sync_mutex_lock, ZEND_ACC_PUBLIC)
	PHP_ME(sync_Mutex, unlock, arginfo_sync_mutex_unlock, ZEND_ACC_PUBLIC)
	PHP_FE_END
};



/* Semaphore */
PHP_SYNC_API zend_class_entry *sync_Semaphore_ce;
static zend_object_handlers sync_Semaphore_object_handlers;

PORTABLE_free_zend_object_func(sync_Semaphore_free_object);

/* {{{ Initialize internal Semaphore structure. */
PORTABLE_new_zend_object_func(sync_Semaphore_create_object)
{
	PORTABLE_new_zend_object_return_var;
	sync_Semaphore_object *obj;

	/* Create the object. */
	obj = (sync_Semaphore_object *)PORTABLE_allocate_zend_object(sizeof(sync_Semaphore_object), ce);

	PORTABLE_InitZendObject(obj, &obj->std, PORTABLE_new_zend_object_return_var_ref, sync_Semaphore_free_object, &sync_Semaphore_object_handlers, ce TSRMLS_CC);

	/* Initialize Semaphore information. */
#if defined(PHP_WIN32)
	obj->MxWinSemaphore = NULL;
#else
	obj->MxNamed = 0;
	obj->MxMem = NULL;
#endif
	obj->MxAutoUnlock = 0;
	obj->MxCount = 0;

	PORTABLE_new_zend_object_return(&obj->std);
}
/* }}} */

/* {{{ Free internal Semaphore structure. */
PORTABLE_free_zend_object_func(sync_Semaphore_free_object)
{
	sync_Semaphore_object *obj = (sync_Semaphore_object *)PORTABLE_free_zend_object_get_object(object);

	if (obj->MxAutoUnlock)
	{
		while (obj->MxCount)
		{
#if defined(PHP_WIN32)
			ReleaseSemaphore(obj->MxWinSemaphore, 1, NULL);
#else
			sync_ReleaseUnixSemaphore(&obj->MxPthreadSemaphore, NULL);
#endif

			obj->MxCount--;
		}
	}

#if defined(PHP_WIN32)
	if (obj->MxWinSemaphore != NULL)  CloseHandle(obj->MxWinSemaphore);
#else
	if (obj->MxMem != NULL)
	{
		if (obj->MxNamed)  sync_UnmapUnixNamedMem(obj->MxMem, sync_GetUnixSemaphoreSize());
		else
		{
			sync_FreeUnixSemaphore(&obj->MxPthreadSemaphore);

			efree(obj->MxMem);
		}
	}
#endif

	PORTABLE_free_zend_object_free_object(obj);
}
/* }}} */

/* {{{ proto void Sync_Semaphore::__construct([string $name = null, [int $initialval = 1, [bool $autounlock = true]]])
   Constructs a named or unnamed semaphore object.  Don't set $autounlock to false unless you really know what you are doing. */
PHP_METHOD(sync_Semaphore, __construct)
{
	char *name = NULL;
	PORTABLE_ZPP_ARG_size name_len;
	PORTABLE_ZPP_ARG_long initialval = 1;
	PORTABLE_ZPP_ARG_long autounlock = 1;
	sync_Semaphore_object *obj;
#if defined(PHP_WIN32)
	SECURITY_ATTRIBUTES SecAttr;
#else
	size_t Pos, TempSize;
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s!ll", &name, &name_len, &initialval, &autounlock) == FAILURE)  return;

	obj = (sync_Semaphore_object *)PORTABLE_zend_object_store_get_object();

	if (name_len < 1)  name = NULL;

	obj->MxAutoUnlock = (autounlock ? 1 : 0);

#if defined(PHP_WIN32)

	SecAttr.nLength = sizeof(SecAttr);
	SecAttr.lpSecurityDescriptor = NULL;
	SecAttr.bInheritHandle = TRUE;

	obj->MxWinSemaphore = CreateSemaphoreA(&SecAttr, (LONG)initialval, (LONG)initialval, name);
	if (obj->MxWinSemaphore == NULL)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Semaphore could not be created", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Semaphore could not be created", 0 TSRMLS_CC);
#endif
		return;
	}

#else

	TempSize = sync_GetUnixSemaphoreSize();
	obj->MxNamed = (name != NULL ? 1 : 0);
	int Result = sync_InitUnixNamedMem(&obj->MxMem, &Pos, "/Sync_Semaphore", name, TempSize);

	if (Result < 0)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Semaphore could not be created", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Semaphore could not be created", 0 TSRMLS_CC);
#endif

		return;
	}

	sync_GetUnixSemaphore(&obj->MxPthreadSemaphore, obj->MxMem + Pos);

	/* Handle the first time this semaphore has been opened. */
	if (Result == 0)
	{
		sync_InitUnixSemaphore(&obj->MxPthreadSemaphore, obj->MxNamed, (uint32_t)initialval, (uint32_t)initialval);

		if (obj->MxNamed)  sync_UnixNamedMemReady(obj->MxMem);
	}

#endif
}
/* }}} */

/* {{{ proto bool Sync_Semaphore::lock([int $wait = -1])
   Locks a semaphore object. */
PHP_METHOD(sync_Semaphore, lock)
{
	PORTABLE_ZPP_ARG_long wait = -1;
	sync_Semaphore_object *obj;
#if defined(PHP_WIN32)
	DWORD Result;
#else
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &wait) == FAILURE)  return;

	obj = (sync_Semaphore_object *)PORTABLE_zend_object_store_get_object();

#if defined(PHP_WIN32)

	Result = WaitForSingleObject(obj->MxWinSemaphore, (DWORD)(wait > -1 ? wait : INFINITE));
	if (Result != WAIT_OBJECT_0)  RETURN_FALSE;

#else

	if (!sync_WaitForUnixSemaphore(&obj->MxPthreadSemaphore, (uint32_t)(wait > -1 ? wait : INFINITE)))  RETURN_FALSE;

#endif

	if (obj->MxAutoUnlock)  obj->MxCount++;

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool Sync_Semaphore::unlock([int &$prevcount])
   Unlocks a semaphore object. */
PHP_METHOD(sync_Semaphore, unlock)
{
	PORTABLE_ZPP_ARG_zval_ref zprevcount = NULL;
	sync_Semaphore_object *obj;
	PORTABLE_ZPP_ARG_long count;
#if defined(PHP_WIN32)
	LONG PrevCount;
#else
	uint32_t PrevCount;
#endif

#if PHP_MAJOR_VERSION >= 7
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|z/", &zprevcount) == FAILURE)  return;
#else
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|Z", &zprevcount) == FAILURE)  return;
#endif

	obj = (sync_Semaphore_object *)PORTABLE_zend_object_store_get_object();

#if defined(PHP_WIN32)

	if (!ReleaseSemaphore(obj->MxWinSemaphore, 1, &PrevCount))  RETURN_FALSE;

#else

	sync_ReleaseUnixSemaphore(&obj->MxPthreadSemaphore, &PrevCount);

#endif

	if (zprevcount != NULL)
	{
		count = (PORTABLE_ZPP_ARG_long)PrevCount;

		zval_dtor(PORTABLE_ZPP_ARG_zval_ref_deref(zprevcount));
		ZVAL_LONG(PORTABLE_ZPP_ARG_zval_ref_deref(zprevcount), count);
	}

	if (obj->MxAutoUnlock)  obj->MxCount--;

	RETURN_TRUE;
}
/* }}} */


ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_semaphore___construct, 0, 0, 0)
	ZEND_ARG_INFO(0, name)
	ZEND_ARG_INFO(0, initialval)
	ZEND_ARG_INFO(0, autounlock)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_semaphore_lock, 0, 0, 0)
	ZEND_ARG_INFO(0, wait)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_semaphore_unlock, 0, 0, 0)
	ZEND_ARG_INFO(1, prevcount)
ZEND_END_ARG_INFO()

static const zend_function_entry sync_Semaphore_methods[] = {
	PHP_ME(sync_Semaphore, __construct, arginfo_sync_semaphore___construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	PHP_ME(sync_Semaphore, lock, arginfo_sync_semaphore_lock, ZEND_ACC_PUBLIC)
	PHP_ME(sync_Semaphore, unlock, arginfo_sync_semaphore_unlock, ZEND_ACC_PUBLIC)
	PHP_FE_END
};



/* Event */
PHP_SYNC_API zend_class_entry *sync_Event_ce;
static zend_object_handlers sync_Event_object_handlers;

PORTABLE_free_zend_object_func(sync_Event_free_object);

/* {{{ Initialize internal Event structure. */
PORTABLE_new_zend_object_func(sync_Event_create_object)
{
	PORTABLE_new_zend_object_return_var;
	sync_Event_object *obj;

	/* Create the object. */
	obj = (sync_Event_object *)PORTABLE_allocate_zend_object(sizeof(sync_Event_object), ce);

	PORTABLE_InitZendObject(obj, &obj->std, PORTABLE_new_zend_object_return_var_ref, sync_Event_free_object, &sync_Event_object_handlers, ce TSRMLS_CC);

	/* Initialize Event information. */
#if defined(PHP_WIN32)
	obj->MxWinWaitEvent = NULL;
#else
	obj->MxNamed = 0;
	obj->MxMem = NULL;
#endif

	PORTABLE_new_zend_object_return(&obj->std);
}
/* }}} */

/* {{{ Free internal Event structure. */
PORTABLE_free_zend_object_func(sync_Event_free_object)
{
	sync_Event_object *obj = (sync_Event_object *)PORTABLE_free_zend_object_get_object(object);

#if defined(PHP_WIN32)
	if (obj->MxWinWaitEvent != NULL)  CloseHandle(obj->MxWinWaitEvent);
#else
	if (obj->MxMem != NULL)
	{
		if (obj->MxNamed)  sync_UnmapUnixNamedMem(obj->MxMem, sync_GetUnixEventSize());
		else
		{
			sync_FreeUnixEvent(&obj->MxPthreadEvent);

			efree(obj->MxMem);
		}
	}
#endif

	PORTABLE_free_zend_object_free_object(obj);
}
/* }}} */

/* {{{ proto void Sync_Event::__construct([string $name = null, [bool $manual = false, [bool $prefire = false]]])
   Constructs a named or unnamed event object. */
PHP_METHOD(sync_Event, __construct)
{
	char *name = NULL;
	PORTABLE_ZPP_ARG_size name_len;
	PORTABLE_ZPP_ARG_long manual = 0, prefire = 0;
	sync_Event_object *obj;
#if defined(PHP_WIN32)
	SECURITY_ATTRIBUTES SecAttr;
#else
	size_t Pos, TempSize;
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s!ll", &name, &name_len, &manual, &prefire) == FAILURE)  return;

	obj = (sync_Event_object *)PORTABLE_zend_object_store_get_object();

	if (name_len < 1)  name = NULL;

#if defined(PHP_WIN32)

	SecAttr.nLength = sizeof(SecAttr);
	SecAttr.lpSecurityDescriptor = NULL;
	SecAttr.bInheritHandle = TRUE;

	obj->MxWinWaitEvent = CreateEventA(&SecAttr, (BOOL)manual, (prefire ? TRUE : FALSE), name);
	if (obj->MxWinWaitEvent == NULL)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Event object could not be created", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Event object could not be created", 0 TSRMLS_CC);
#endif
		return;
	}

#else

	TempSize = sync_GetUnixEventSize();
	obj->MxNamed = (name != NULL ? 1 : 0);
	int Result = sync_InitUnixNamedMem(&obj->MxMem, &Pos, "/Sync_Event", name, TempSize);

	if (Result < 0)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Event object could not be created", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Event object could not be created", 0 TSRMLS_CC);
#endif

		return;
	}

	sync_GetUnixEvent(&obj->MxPthreadEvent, obj->MxMem + Pos);

	/* Handle the first time this event has been opened. */
	if (Result == 0)
	{
		sync_InitUnixEvent(&obj->MxPthreadEvent, obj->MxNamed, (manual ? 1 : 0), (prefire ? 1 : 0));

		if (obj->MxNamed)  sync_UnixNamedMemReady(obj->MxMem);
	}

#endif
}
/* }}} */

/* {{{ proto bool Sync_Event::wait([int $wait = -1])
   Waits for an event object to fire. */
PHP_METHOD(sync_Event, wait)
{
	PORTABLE_ZPP_ARG_long wait = -1;
	sync_Event_object *obj;
#if defined(PHP_WIN32)
	DWORD Result;
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &wait) == FAILURE)  return;

	obj = (sync_Event_object *)PORTABLE_zend_object_store_get_object();

#if defined(PHP_WIN32)

	Result = WaitForSingleObject(obj->MxWinWaitEvent, (DWORD)(wait > -1 ? wait : INFINITE));
	if (Result != WAIT_OBJECT_0)  RETURN_FALSE;

#else

	if (!sync_WaitForUnixEvent(&obj->MxPthreadEvent, (uint32_t)(wait > -1 ? wait : INFINITE)))  RETURN_FALSE;

#endif

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool Sync_Event::fire()
   Lets a thread through that is waiting.  Lets multiple threads through that are waiting if the event object is 'manual'. */
PHP_METHOD(sync_Event, fire)
{
	sync_Event_object *obj;

	obj = (sync_Event_object *)PORTABLE_zend_object_store_get_object();

#if defined(PHP_WIN32)

	if (!SetEvent(obj->MxWinWaitEvent))  RETURN_FALSE;

#else

	if (!sync_FireUnixEvent(&obj->MxPthreadEvent))  RETURN_FALSE;

#endif

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool Sync_Event::reset()
   Resets the event object state.  Only use when the event object is 'manual'. */
PHP_METHOD(sync_Event, reset)
{
	sync_Event_object *obj;

	obj = (sync_Event_object *)PORTABLE_zend_object_store_get_object();

#if defined(PHP_WIN32)

	if (!ResetEvent(obj->MxWinWaitEvent))  RETURN_FALSE;

#else

	if (!sync_ResetUnixEvent(&obj->MxPthreadEvent))  RETURN_FALSE;

#endif

	RETURN_TRUE;
}
/* }}} */


ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_event___construct, 0, 0, 0)
	ZEND_ARG_INFO(0, name)
	ZEND_ARG_INFO(0, manual)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_event_wait, 0, 0, 0)
	ZEND_ARG_INFO(0, wait)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_event_fire, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_event_reset, 0, 0, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry sync_Event_methods[] = {
	PHP_ME(sync_Event, __construct, arginfo_sync_event___construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	PHP_ME(sync_Event, wait, arginfo_sync_event_wait, ZEND_ACC_PUBLIC)
	PHP_ME(sync_Event, fire, arginfo_sync_event_fire, ZEND_ACC_PUBLIC)
	PHP_ME(sync_Event, reset, arginfo_sync_event_reset, ZEND_ACC_PUBLIC)
	PHP_FE_END
};



/* Reader-Writer */
PHP_SYNC_API zend_class_entry *sync_ReaderWriter_ce;
static zend_object_handlers sync_ReaderWriter_object_handlers;

PORTABLE_free_zend_object_func(sync_ReaderWriter_free_object);

/* {{{ Initialize internal Reader-Writer structure. */
PORTABLE_new_zend_object_func(sync_ReaderWriter_create_object)
{
	PORTABLE_new_zend_object_return_var;
	sync_ReaderWriter_object *obj;

	/* Create the object. */
	obj = (sync_ReaderWriter_object *)PORTABLE_allocate_zend_object(sizeof(sync_ReaderWriter_object), ce);

	PORTABLE_InitZendObject(obj, &obj->std, PORTABLE_new_zend_object_return_var_ref, sync_ReaderWriter_free_object, &sync_ReaderWriter_object_handlers, ce TSRMLS_CC);

	/* Initialize Reader-Writer information. */
#if defined(PHP_WIN32)
	obj->MxWinRSemMutex = NULL;
	obj->MxWinRSemaphore = NULL;
	obj->MxWinRWaitEvent = NULL;
	obj->MxWinWWaitMutex = NULL;
#else
	obj->MxNamed = 0;
	obj->MxMem = NULL;
	obj->MxRCount = NULL;
#endif

	obj->MxAutoUnlock = 1;
	obj->MxReadLocks = 0;
	obj->MxWriteLock = 0;

	PORTABLE_new_zend_object_return(&obj->std);
}
/* }}} */

/* {{{ Unlocks a read lock. */
int sync_ReaderWriter_readunlock_internal(sync_ReaderWriter_object *obj)
{
#if defined(PHP_WIN32)

	DWORD Result;
	LONG Val;

	if (obj->MxWinRSemMutex == NULL || obj->MxWinRSemaphore == NULL || obj->MxWinRWaitEvent == NULL)  return 0;

	/* Acquire the semaphore mutex. */
	Result = WaitForSingleObject(obj->MxWinRSemMutex, INFINITE);
	if (Result != WAIT_OBJECT_0)  return 0;

	if (obj->MxReadLocks)  obj->MxReadLocks--;

	/* Release the semaphore. */
	if (!ReleaseSemaphore(obj->MxWinRSemaphore, 1, &Val))
	{
		ReleaseSemaphore(obj->MxWinRSemMutex, 1, NULL);

		return 0;
	}

	/* Update the event state. */
	if (Val == LONG_MAX - 1)
	{
		if (!SetEvent(obj->MxWinRWaitEvent))
		{
			ReleaseSemaphore(obj->MxWinRSemMutex, 1, NULL);

			return 0;
		}
	}

	/* Release the semaphore mutex. */
	ReleaseSemaphore(obj->MxWinRSemMutex, 1, NULL);

#else

	if (obj->MxMem == NULL)  return 0;

	/* Acquire the counter mutex. */
	if (!sync_WaitForUnixSemaphore(&obj->MxPthreadRCountMutex, INFINITE))  return 0;

	if (obj->MxReadLocks)  obj->MxReadLocks--;

	/* Decrease the number of readers. */
	if (obj->MxRCount[0])  obj->MxRCount[0]--;
	else
	{
		sync_ReleaseUnixSemaphore(&obj->MxPthreadRCountMutex, NULL);

		return 0;
	}

	/* Update the event state. */
	if (!obj->MxRCount[0] && !sync_FireUnixEvent(&obj->MxPthreadRWaitEvent))
	{
		sync_ReleaseUnixSemaphore(&obj->MxPthreadRCountMutex, NULL);

		return 0;
	}

	/* Release the counter mutex. */
	sync_ReleaseUnixSemaphore(&obj->MxPthreadRCountMutex, NULL);

#endif

	return 1;
}
/* }}} */

/* {{{ Unlocks a write lock. */
int sync_ReaderWriter_writeunlock_internal(sync_ReaderWriter_object *obj)
{
#if defined(PHP_WIN32)

	if (obj->MxWinWWaitMutex == NULL)  return 0;

	obj->MxWriteLock = 0;

	/* Release the write lock. */
	ReleaseSemaphore(obj->MxWinWWaitMutex, 1, NULL);

#else

	if (obj->MxMem == NULL)  return 0;

	obj->MxWriteLock = 0;

	/* Release the write lock. */
	sync_ReleaseUnixSemaphore(&obj->MxPthreadWWaitMutex, NULL);

#endif

	return 1;
}
/* }}} */

/* {{{ Free internal Reader-Writer structure. */
PORTABLE_free_zend_object_func(sync_ReaderWriter_free_object)
{
	sync_ReaderWriter_object *obj = (sync_ReaderWriter_object *)PORTABLE_free_zend_object_get_object(object);

	if (obj->MxAutoUnlock)
	{
		while (obj->MxReadLocks)  sync_ReaderWriter_readunlock_internal(obj);

		if (obj->MxWriteLock)  sync_ReaderWriter_writeunlock_internal(obj);
	}

#if defined(PHP_WIN32)
	if (obj->MxWinWWaitMutex != NULL)  CloseHandle(obj->MxWinWWaitMutex);
	if (obj->MxWinRWaitEvent != NULL)  CloseHandle(obj->MxWinRWaitEvent);
	if (obj->MxWinRSemaphore != NULL)  CloseHandle(obj->MxWinRSemaphore);
	if (obj->MxWinRSemMutex != NULL)  CloseHandle(obj->MxWinRSemMutex);
#else
	if (obj->MxMem != NULL)
	{
		if (obj->MxNamed)  sync_UnmapUnixNamedMem(obj->MxMem, sync_GetUnixSemaphoreSize() + sync_AlignUnixSize(sizeof(uint32_t)) + sync_GetUnixEventSize() + sync_GetUnixSemaphoreSize());
		else
		{
			sync_FreeUnixSemaphore(&obj->MxPthreadRCountMutex);
			sync_FreeUnixEvent(&obj->MxPthreadRWaitEvent);
			sync_FreeUnixSemaphore(&obj->MxPthreadWWaitMutex);

			efree(obj->MxMem);
		}
	}
#endif

	PORTABLE_free_zend_object_free_object(obj);
}
/* }}} */

/* {{{ proto void Sync_ReaderWriter::__construct([string $name = null, [bool $autounlock = true]])
   Constructs a named or unnamed reader-writer object.  Don't set $autounlock to false unless you really know what you are doing. */
PHP_METHOD(sync_ReaderWriter, __construct)
{
	char *name = NULL;
	PORTABLE_ZPP_ARG_size name_len;
	PORTABLE_ZPP_ARG_long autounlock = 1;
	sync_ReaderWriter_object *obj;
#if defined(PHP_WIN32)
	char *name2;
	SECURITY_ATTRIBUTES SecAttr;
#else
	size_t Pos, TempSize;
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s!l", &name, &name_len, &autounlock) == FAILURE)  return;

	obj = (sync_ReaderWriter_object *)PORTABLE_zend_object_store_get_object();

	if (name_len < 1)  name = NULL;

	obj->MxAutoUnlock = (autounlock ? 1 : 0);

#if defined(PHP_WIN32)

	if (name == NULL)  name2 = NULL;
	else  name2 = emalloc(name_len + 20);

	SecAttr.nLength = sizeof(SecAttr);
	SecAttr.lpSecurityDescriptor = NULL;
	SecAttr.bInheritHandle = TRUE;

	/* Create the mutexes, semaphore, and event objects. */
	if (name2 != NULL)  sprintf(name2, "%s-Sync_ReadWrite-0", name);
	obj->MxWinRSemMutex = CreateSemaphoreA(&SecAttr, 1, 1, name2);
	if (name2 != NULL)  sprintf(name2, "%s-Sync_ReadWrite-1", name);
	obj->MxWinRSemaphore = CreateSemaphoreA(&SecAttr, LONG_MAX, LONG_MAX, name2);
	if (name2 != NULL)  sprintf(name2, "%s-Sync_ReadWrite-2", name);
	obj->MxWinRWaitEvent = CreateEventA(&SecAttr, TRUE, TRUE, name2);
	if (name2 != NULL)  sprintf(name2, "%s-Sync_ReadWrite-3", name);
	obj->MxWinWWaitMutex = CreateSemaphoreA(&SecAttr, 1, 1, name2);

	if (name2 != NULL)  efree(name2);

	if (obj->MxWinRSemMutex == NULL || obj->MxWinRSemaphore == NULL || obj->MxWinRWaitEvent == NULL || obj->MxWinWWaitMutex == NULL)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Reader-Writer object could not be created", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Reader-Writer object could not be created", 0 TSRMLS_CC);
#endif

		return;
	}

#else

	TempSize = sync_GetUnixSemaphoreSize() + sync_AlignUnixSize(sizeof(uint32_t)) + sync_GetUnixEventSize() + sync_GetUnixSemaphoreSize();
	obj->MxNamed = (name != NULL ? 1 : 0);
	int Result = sync_InitUnixNamedMem(&obj->MxMem, &Pos, "/Sync_ReadWrite", name, TempSize);

	if (Result < 0)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Reader-Writer object could not be created", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Reader-Writer object could not be created", 0 TSRMLS_CC);
#endif

		return;
	}

	/* Load the pointers. */
	char *MemPtr = obj->MxMem + Pos;
	sync_GetUnixSemaphore(&obj->MxPthreadRCountMutex, MemPtr);
	MemPtr += sync_GetUnixSemaphoreSize();

	obj->MxRCount = (volatile uint32_t *)(MemPtr);
	MemPtr += sync_AlignUnixSize(sizeof(uint32_t));

	sync_GetUnixEvent(&obj->MxPthreadRWaitEvent, MemPtr);
	MemPtr += sync_GetUnixEventSize();

	sync_GetUnixSemaphore(&obj->MxPthreadWWaitMutex, MemPtr);

	/* Handle the first time this reader/writer lock has been opened. */
	if (Result == 0)
	{
		sync_InitUnixSemaphore(&obj->MxPthreadRCountMutex, obj->MxNamed, 1, 1);
		obj->MxRCount[0] = 0;
		sync_InitUnixEvent(&obj->MxPthreadRWaitEvent, obj->MxNamed, 1, 1);
		sync_InitUnixSemaphore(&obj->MxPthreadWWaitMutex, obj->MxNamed, 1, 1);

		if (obj->MxNamed)  sync_UnixNamedMemReady(obj->MxMem);
	}

#endif
}
/* }}} */

/* {{{ proto bool Sync_ReaderWriter::readlock([int $wait = -1])
   Read locks a reader-writer object. */
PHP_METHOD(sync_ReaderWriter, readlock)
{
	PORTABLE_ZPP_ARG_long wait = -1;
	sync_ReaderWriter_object *obj;
	uint32_t WaitAmt;
	uint64_t StartTime, CurrTime;
#if defined(PHP_WIN32)
	DWORD Result;
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &wait) == FAILURE)  return;

	obj = (sync_ReaderWriter_object *)PORTABLE_zend_object_store_get_object();

	WaitAmt = (uint32_t)(wait > -1 ? wait : INFINITE);

	/* Get current time in milliseconds. */
	StartTime = (WaitAmt == INFINITE ? 0 : sync_GetUnixMicrosecondTime() / 1000000);

#if defined(PHP_WIN32)

	/* Acquire the write lock mutex.  Guarantees that readers can't starve the writer. */
	Result = WaitForSingleObject(obj->MxWinWWaitMutex, WaitAmt);
	if (Result != WAIT_OBJECT_0)  RETURN_FALSE;

	/* Acquire the semaphore mutex. */
	CurrTime = (WaitAmt == INFINITE ? 0 : sync_GetUnixMicrosecondTime() / 1000000);
	if (WaitAmt < CurrTime - StartTime)
	{
		ReleaseSemaphore(obj->MxWinWWaitMutex, 1, NULL);

		RETURN_FALSE;
	}
	Result = WaitForSingleObject(obj->MxWinRSemMutex, WaitAmt - (DWORD)(CurrTime - StartTime));
	if (Result != WAIT_OBJECT_0)
	{
		ReleaseSemaphore(obj->MxWinWWaitMutex, 1, NULL);

		RETURN_FALSE;
	}

	/* Acquire the semaphore. */
	CurrTime = (WaitAmt == INFINITE ? 0 : sync_GetUnixMicrosecondTime() / 1000000);
	if (WaitAmt < CurrTime - StartTime)
	{
		ReleaseSemaphore(obj->MxWinRSemMutex, 1, NULL);
		ReleaseSemaphore(obj->MxWinWWaitMutex, 1, NULL);

		RETURN_FALSE;
	}
	Result = WaitForSingleObject(obj->MxWinRSemaphore, WaitAmt - (DWORD)(CurrTime - StartTime));
	if (Result != WAIT_OBJECT_0)
	{
		ReleaseSemaphore(obj->MxWinRSemMutex, 1, NULL);
		ReleaseSemaphore(obj->MxWinWWaitMutex, 1, NULL);

		RETURN_FALSE;
	}

	/* Update the event state. */
	if (!ResetEvent(obj->MxWinRWaitEvent))
	{
		ReleaseSemaphore(obj->MxWinRSemaphore, 1, NULL);
		ReleaseSemaphore(obj->MxWinRSemMutex, 1, NULL);
		ReleaseSemaphore(obj->MxWinWWaitMutex, 1, NULL);

		RETURN_FALSE;
	}

	obj->MxReadLocks++;

	/* Release the mutexes. */
	ReleaseSemaphore(obj->MxWinRSemMutex, 1, NULL);
	ReleaseSemaphore(obj->MxWinWWaitMutex, 1, NULL);

#else

	/* Acquire the write lock mutex.  Guarantees that readers can't starve the writer. */
	if (!sync_WaitForUnixSemaphore(&obj->MxPthreadWWaitMutex, WaitAmt))  RETURN_FALSE;

	/* Acquire the counter mutex. */
	CurrTime = (WaitAmt == INFINITE ? 0 : sync_GetUnixMicrosecondTime() / 1000000);
	if (WaitAmt < CurrTime - StartTime || !sync_WaitForUnixSemaphore(&obj->MxPthreadRCountMutex, WaitAmt - (CurrTime - StartTime)))
	{
		sync_ReleaseUnixSemaphore(&obj->MxPthreadWWaitMutex, NULL);

		RETURN_FALSE;
	}

	/* Update the event state. */
	if (!sync_ResetUnixEvent(&obj->MxPthreadRWaitEvent))
	{
		sync_ReleaseUnixSemaphore(&obj->MxPthreadRCountMutex, NULL);
		sync_ReleaseUnixSemaphore(&obj->MxPthreadWWaitMutex, NULL);

		RETURN_FALSE;
	}

	/* Increment the number of readers. */
	obj->MxRCount[0]++;

	obj->MxReadLocks++;

	/* Release the mutexes. */
	sync_ReleaseUnixSemaphore(&obj->MxPthreadRCountMutex, NULL);
	sync_ReleaseUnixSemaphore(&obj->MxPthreadWWaitMutex, NULL);

#endif

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool Sync_ReaderWriter::writelock([int $wait = -1])
   Write locks a reader-writer object. */
PHP_METHOD(sync_ReaderWriter, writelock)
{
	PORTABLE_ZPP_ARG_long wait = -1;
	sync_ReaderWriter_object *obj;
	uint32_t WaitAmt;
	uint64_t StartTime, CurrTime;
#if defined(PHP_WIN32)
	DWORD Result;
#else
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &wait) == FAILURE)  return;

	obj = (sync_ReaderWriter_object *)PORTABLE_zend_object_store_get_object();

	WaitAmt = (uint32_t)(wait > -1 ? wait : INFINITE);

	/* Get current time in milliseconds. */
	StartTime = (WaitAmt == INFINITE ? 0 : sync_GetUnixMicrosecondTime() / 1000000);

#if defined(PHP_WIN32)

	/* Acquire the write lock mutex. */
	Result = WaitForSingleObject(obj->MxWinWWaitMutex, WaitAmt);
	if (Result != WAIT_OBJECT_0)  RETURN_FALSE;

	/* Wait for readers to reach zero. */
	CurrTime = (WaitAmt == INFINITE ? 0 : sync_GetUnixMicrosecondTime() / 1000000);
	Result = WaitForSingleObject(obj->MxWinRWaitEvent, WaitAmt - (DWORD)(CurrTime - StartTime));
	if (Result != WAIT_OBJECT_0)
	{
		ReleaseSemaphore(obj->MxWinWWaitMutex, 1, NULL);

		RETURN_FALSE;
	}

#else

	/* Acquire the write lock mutex. */
	if (!sync_WaitForUnixSemaphore(&obj->MxPthreadWWaitMutex, WaitAmt))  RETURN_FALSE;

	/* Wait for readers to reach zero. */
	CurrTime = (WaitAmt == INFINITE ? 0 : sync_GetUnixMicrosecondTime() / 1000000);
	if (WaitAmt < CurrTime - StartTime || !sync_WaitForUnixEvent(&obj->MxPthreadRWaitEvent, WaitAmt - (CurrTime - StartTime)))
	{
		sync_ReleaseUnixSemaphore(&obj->MxPthreadWWaitMutex, NULL);

		RETURN_FALSE;
	}

#endif

	obj->MxWriteLock = 1;

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool Sync_ReaderWriter::readunlock()
   Read unlocks a reader-writer object. */
PHP_METHOD(sync_ReaderWriter, readunlock)
{
	sync_ReaderWriter_object *obj;

	obj = (sync_ReaderWriter_object *)PORTABLE_zend_object_store_get_object();

	if (!sync_ReaderWriter_readunlock_internal(obj))  RETURN_FALSE;

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool Sync_ReaderWriter::writeunlock()
   Write unlocks a reader-writer object. */
PHP_METHOD(sync_ReaderWriter, writeunlock)
{
	sync_ReaderWriter_object *obj;

	obj = (sync_ReaderWriter_object *)PORTABLE_zend_object_store_get_object();

	if (!sync_ReaderWriter_writeunlock_internal(obj))  RETURN_FALSE;

	RETURN_TRUE;
}
/* }}} */


ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_readerwriter___construct, 0, 0, 0)
	ZEND_ARG_INFO(0, name)
	ZEND_ARG_INFO(0, autounlock)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_readerwriter_readlock, 0, 0, 0)
	ZEND_ARG_INFO(0, wait)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_readerwriter_writelock, 0, 0, 0)
	ZEND_ARG_INFO(0, wait)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_readerwriter_readunlock, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_readerwriter_writeunlock, 0, 0, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry sync_ReaderWriter_methods[] = {
	PHP_ME(sync_ReaderWriter, __construct, arginfo_sync_readerwriter___construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	PHP_ME(sync_ReaderWriter, readlock, arginfo_sync_readerwriter_readlock, ZEND_ACC_PUBLIC)
	PHP_ME(sync_ReaderWriter, writelock, arginfo_sync_readerwriter_writelock, ZEND_ACC_PUBLIC)
	PHP_ME(sync_ReaderWriter, readunlock, arginfo_sync_readerwriter_readunlock, ZEND_ACC_PUBLIC)
	PHP_ME(sync_ReaderWriter, writeunlock, arginfo_sync_readerwriter_writeunlock, ZEND_ACC_PUBLIC)
	PHP_FE_END
};



/* Shared Memory */
PHP_SYNC_API zend_class_entry *sync_SharedMemory_ce;
static zend_object_handlers sync_SharedMemory_object_handlers;

PORTABLE_free_zend_object_func(sync_SharedMemory_free_object);

/* {{{ Initialize internal Shared Memory structure. */
PORTABLE_new_zend_object_func(sync_SharedMemory_create_object)
{
	PORTABLE_new_zend_object_return_var;
	sync_SharedMemory_object *obj;

	/* Create the object. */
	obj = (sync_SharedMemory_object *)PORTABLE_allocate_zend_object(sizeof(sync_SharedMemory_object), ce);

	PORTABLE_InitZendObject(obj, &obj->std, PORTABLE_new_zend_object_return_var_ref, sync_SharedMemory_free_object, &sync_SharedMemory_object_handlers, ce TSRMLS_CC);

	/* Initialize Shared Memory information. */
#if defined(PHP_WIN32)
	obj->MxFile = NULL;
#else
	obj->MxMemInternal = NULL;
#endif

	obj->MxFirst = 0;
	obj->MxSize = 0;
	obj->MxMem = NULL;

	PORTABLE_new_zend_object_return(&obj->std);
}
/* }}} */

/* {{{ Free internal Shared Memory structure. */
PORTABLE_free_zend_object_func(sync_SharedMemory_free_object)
{
	sync_SharedMemory_object *obj = (sync_SharedMemory_object *)PORTABLE_free_zend_object_get_object(object);

#if defined(PHP_WIN32)
	if (obj->MxMem != NULL)  UnmapViewOfFile(obj->MxMem);
	if (obj->MxFile != NULL)  CloseHandle(obj->MxFile);
#else
	if (obj->MxMemInternal != NULL)  sync_UnmapUnixNamedMem(obj->MxMemInternal, obj->MxSize);
#endif

	PORTABLE_free_zend_object_free_object(obj);
}
/* }}} */

/* {{{ proto void Sync_SharedMemory::__construct(string $name, int $size)
   Constructs a named shared memory object. */
PHP_METHOD(sync_SharedMemory, __construct)
{
	char *name;
	PORTABLE_ZPP_ARG_size name_len;
	PORTABLE_ZPP_ARG_long size;
	sync_SharedMemory_object *obj;
#if defined(PHP_WIN32)
	char *name2;
	SECURITY_ATTRIBUTES SecAttr;
#else
	size_t Pos, TempSize;
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl", &name, &name_len, &size) == FAILURE)  return;

	if (name_len < 1)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "An invalid name was passed", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "An invalid name was passed", 0 TSRMLS_CC);
#endif

		return;
	}

	obj = (sync_SharedMemory_object *)PORTABLE_zend_object_store_get_object();

#if defined(PHP_WIN32)

	name2 = emalloc(name_len + 30);

	SecAttr.nLength = sizeof(SecAttr);
	SecAttr.lpSecurityDescriptor = NULL;
	SecAttr.bInheritHandle = TRUE;

	/* Create the file mapping object backed by the system page file. */
	sprintf(name2, "%s-%u-Sync_SharedMem", name, (unsigned int)size);
	obj->MxFile = CreateFileMappingA(INVALID_HANDLE_VALUE, &SecAttr, PAGE_READWRITE, 0, (DWORD)size, name2);
	if (obj->MxFile == NULL)
	{
		obj->MxFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, TRUE, name2);

		if (obj->MxFile == NULL)
		{
#if PHP_VERSION_ID < 80500
			zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Shared memory file mapping could not be created/opened", 0 TSRMLS_CC);
#else
			zend_throw_exception(zend_ce_exception, "Shared memory file mapping could not be created/opened", 0 TSRMLS_CC);
#endif

			return;
		}
	}
	else if (GetLastError() != ERROR_ALREADY_EXISTS)
	{
		obj->MxFirst = 1;
	}

	efree(name2);

	obj->MxMem = (char *)MapViewOfFile(obj->MxFile, FILE_MAP_ALL_ACCESS, 0, 0, (DWORD)size);

	if (obj->MxMem == NULL)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Shared memory segment could not be mapped", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Shared memory segment could not be mapped", 0 TSRMLS_CC);
#endif

		return;
	}

	obj->MxSize = (size_t)size;

#else

	TempSize = (size_t)size;
	int Result = sync_InitUnixNamedMem(&obj->MxMemInternal, &Pos, "/Sync_SharedMem", name, TempSize);

	if (Result < 0)
	{
#if PHP_VERSION_ID < 80500
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Shared memory object could not be created/opened", 0 TSRMLS_CC);
#else
		zend_throw_exception(zend_ce_exception, "Shared memory object could not be created/opened", 0 TSRMLS_CC);
#endif

		return;
	}

	/* Load the pointers. */
	obj->MxMem = obj->MxMemInternal + Pos;
	obj->MxSize = (size_t)size;

	/* Handle the first time this named memory has been opened. */
	if (Result == 0)
	{
		sync_UnixNamedMemReady(obj->MxMemInternal);

		obj->MxFirst = 1;
	}

#endif
}
/* }}} */

/* {{{ proto bool Sync_SharedMemory::first()
   Returns whether or not this shared memory segment is the first time accessed (i.e. not initialized). */
PHP_METHOD(sync_SharedMemory, first)
{
	sync_SharedMemory_object *obj;

	obj = (sync_SharedMemory_object *)PORTABLE_zend_object_store_get_object();

	RETURN_BOOL(obj->MxFirst);
}
/* }}} */

/* {{{ proto int Sync_SharedMemory::size()
   Returns the shared memory size. */
PHP_METHOD(sync_SharedMemory, size)
{
	sync_SharedMemory_object *obj;

	obj = (sync_SharedMemory_object *)PORTABLE_zend_object_store_get_object();

	RETURN_LONG((PORTABLE_ZPP_ARG_long)obj->MxSize);
}
/* }}} */

/* {{{ proto int Sync_SharedMemory::write(string $string, [int $start = 0])
   Copies data to shared memory. */
PHP_METHOD(sync_SharedMemory, write)
{
	char *str;
	PORTABLE_ZPP_ARG_size str_len;
	PORTABLE_ZPP_ARG_long start = 0, maxval;
	sync_SharedMemory_object *obj;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l", &str, &str_len, &start) == FAILURE)  return;

	obj = (sync_SharedMemory_object *)PORTABLE_zend_object_store_get_object();
	maxval = (PORTABLE_ZPP_ARG_long)obj->MxSize;

	if (start < 0)  start += maxval;
	if (start < 0)  start = 0;
	if (start > maxval)  start = maxval;

	if (start + str_len > maxval)  str_len = maxval - start;

	memcpy(obj->MxMem + (size_t)start, str, str_len);

	RETURN_LONG((PORTABLE_ZPP_ARG_long)str_len);
}
/* }}} */

/* {{{ proto string Sync_SharedMemory::read([int $start = 0, [int $length = null]])
   Copies data from shared memory. */
PHP_METHOD(sync_SharedMemory, read)
{
	PORTABLE_ZPP_ARG_long start = 0;
	PORTABLE_ZPP_ARG_long length, maxval;
	sync_SharedMemory_object *obj;

	obj = (sync_SharedMemory_object *)PORTABLE_zend_object_store_get_object();
	maxval = (PORTABLE_ZPP_ARG_long)obj->MxSize;
	length = maxval;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|ll", &start, &length) == FAILURE)  return;

	if (start < 0)  start += maxval;
	if (start < 0)  start = 0;
	if (start > maxval)  start = maxval;

	if (length < 0)  length += maxval - start;
	if (length < 0)  length = 0;
	if (start + length > maxval)  length = maxval - start;

	PORTABLE_RETURN_STRINGL(obj->MxMem + start, length);
}
/* }}} */


ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_sharedmemory___construct, 0, 0, 2)
	ZEND_ARG_INFO(0, name)
	ZEND_ARG_INFO(0, size)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_sharedmemory_first, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_sharedmemory_size, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_sharedmemory_write, 0, 0, 1)
	ZEND_ARG_INFO(0, string)
	ZEND_ARG_INFO(0, start)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sync_sharedmemory_read, 0, 0, 0)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, length)
ZEND_END_ARG_INFO()

static const zend_function_entry sync_SharedMemory_methods[] = {
	PHP_ME(sync_SharedMemory, __construct, arginfo_sync_sharedmemory___construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	PHP_ME(sync_SharedMemory, first, arginfo_sync_sharedmemory_first, ZEND_ACC_PUBLIC)
	PHP_ME(sync_SharedMemory, size, arginfo_sync_sharedmemory_size, ZEND_ACC_PUBLIC)
	PHP_ME(sync_SharedMemory, write, arginfo_sync_sharedmemory_write, ZEND_ACC_PUBLIC)
	PHP_ME(sync_SharedMemory, read, arginfo_sync_sharedmemory_read, ZEND_ACC_PUBLIC)
	PHP_FE_END
};


/* {{{ PHP_MINIT_FUNCTION(sync)
 */
PHP_MINIT_FUNCTION(sync)
{
	zend_class_entry ce;

	/* Mutex */
	memcpy(&sync_Mutex_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	sync_Mutex_object_handlers.clone_obj = NULL;
#if PHP_MAJOR_VERSION >= 7
	sync_Mutex_object_handlers.offset = XtOffsetOf(sync_Mutex_object, PORTABLE_default_zend_object_name);
	sync_Mutex_object_handlers.free_obj = sync_Mutex_free_object;
#endif

	INIT_CLASS_ENTRY(ce, "SyncMutex", sync_Mutex_methods);
	ce.create_object = sync_Mutex_create_object;
	sync_Mutex_ce = zend_register_internal_class(&ce TSRMLS_CC);


	/* Semaphore */
	memcpy(&sync_Semaphore_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	sync_Semaphore_object_handlers.clone_obj = NULL;
#if PHP_MAJOR_VERSION >= 7
	sync_Semaphore_object_handlers.offset = XtOffsetOf(sync_Semaphore_object, PORTABLE_default_zend_object_name);
	sync_Semaphore_object_handlers.free_obj = sync_Semaphore_free_object;
#endif

	INIT_CLASS_ENTRY(ce, "SyncSemaphore", sync_Semaphore_methods);
	ce.create_object = sync_Semaphore_create_object;
	sync_Semaphore_ce = zend_register_internal_class(&ce TSRMLS_CC);


	/* Event */
	memcpy(&sync_Event_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	sync_Event_object_handlers.clone_obj = NULL;
#if PHP_MAJOR_VERSION >= 7
	sync_Event_object_handlers.offset = XtOffsetOf(sync_Event_object, PORTABLE_default_zend_object_name);
	sync_Event_object_handlers.free_obj = sync_Event_free_object;
#endif

	INIT_CLASS_ENTRY(ce, "SyncEvent", sync_Event_methods);
	ce.create_object = sync_Event_create_object;
	sync_Event_ce = zend_register_internal_class(&ce TSRMLS_CC);


	/* Reader-Writer */
	memcpy(&sync_ReaderWriter_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	sync_ReaderWriter_object_handlers.clone_obj = NULL;
#if PHP_MAJOR_VERSION >= 7
	sync_ReaderWriter_object_handlers.offset = XtOffsetOf(sync_ReaderWriter_object, PORTABLE_default_zend_object_name);
	sync_ReaderWriter_object_handlers.free_obj = sync_ReaderWriter_free_object;
#endif

	INIT_CLASS_ENTRY(ce, "SyncReaderWriter", sync_ReaderWriter_methods);
	ce.create_object = sync_ReaderWriter_create_object;
	sync_ReaderWriter_ce = zend_register_internal_class(&ce TSRMLS_CC);


	/* Named Shared Memory */
	memcpy(&sync_SharedMemory_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	sync_SharedMemory_object_handlers.clone_obj = NULL;
#if PHP_MAJOR_VERSION >= 7
	sync_SharedMemory_object_handlers.offset = XtOffsetOf(sync_SharedMemory_object, PORTABLE_default_zend_object_name);
	sync_SharedMemory_object_handlers.free_obj = sync_SharedMemory_free_object;
#endif

	INIT_CLASS_ENTRY(ce, "SyncSharedMemory", sync_SharedMemory_methods);
	ce.create_object = sync_SharedMemory_create_object;
	sync_SharedMemory_ce = zend_register_internal_class(&ce TSRMLS_CC);

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(sync)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(sync)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "sync support", "enabled");
	php_info_print_table_end();
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
