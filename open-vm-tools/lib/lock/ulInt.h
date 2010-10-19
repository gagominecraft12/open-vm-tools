/*********************************************************
 * Copyright (C) 2009 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

#ifndef _ULINT_H_
#define _ULINT_H_

#if defined(_WIN32)
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32)
typedef DWORD MXThreadID;
#define MXUSER_INVALID_OWNER 0xFFFFFFFF
#else
#include <pthread.h>
typedef pthread_t MXThreadID;
#endif

#include "vm_basic_types.h"
#include "vthreadBase.h"

#include "circList.h"

#define MXUSER_STAT_CLASS_ACQUISITION "a"
#define MXUSER_STAT_CLASS_HELD        "h"

/*
 * A portable recursive lock.
 */

#define MXUSER_MAX_REC_DEPTH 16

typedef struct {
#if defined(_WIN32)
   CRITICAL_SECTION nativeLock;       // Native lock object
#else
   pthread_mutex_t  nativeLock;       // Native lock object
#endif

   int              referenceCount;   // Acquisition count
   MXThreadID       nativeThreadID;   // Native thread ID
} MXRecLock;


/*
 * Environment specific implementations of portable recursive locks.
 *
 * A recursive lock is used throughput the MXUser locks because:
 *     - it can be used recursively for recursive locks
 *     - exclusive (non-recursive) locks catch the recursion and panic
 *       rather than deadlock.
 *
 * There are 8 environment specific primitives:
 *
 * MXRecLockCreateInternal      Create lock before use
 * MXRecLockDestroyInternal     Destroy lock after use
 * MXRecLockIsOwner             Is lock owned by the caller?
 * MXRecLockSetNoOwner          Set lock as owner by "nobody"
 * MXRecLockSetOwner            Set lock owner
 * MXRecLockAcquireInternal     Lock the lock
 * MXRecLockTryAcquireInternal  conditionally acquire the lock
 * MXRecLockReleaseInternal     Unlock the lock
 *
 * Windows has a native recursive lock, the CRITICAL_SECTION. POSIXen,
 * unfortunately, do not ensure access to such a facility. The recursive
 * attribute of pthread_mutex_t is not implemented in all environments so
 * we create a recursive implementation using an exclusive pthread_mutex_t
 * and a few lines of code (most of which we need to do anyway).
 */

#if defined(_WIN32)
static INLINE int
MXRecLockCreateInternal(MXRecLock *lock)  // IN/OUT:
{
   Bool success;

   /* http://msdn.microsoft.com/en-us/library/ms682530(VS.85).aspx */
   /* magic number - allocate resources immediately; spin 0x400 times */
   success = InitializeCriticalSectionAndSpinCount(&lock->nativeLock,
                                                   0x80000400);

   return success ? 0 : GetLastError();
}


static INLINE int
MXRecLockDestroyInternal(MXRecLock *lock)  // IN/OUT:
{
   DeleteCriticalSection(&lock->nativeLock);

   return 0;
}


static INLINE Bool
MXRecLockIsOwner(const MXRecLock *lock)  // IN:
{
   return lock->nativeThreadID == GetCurrentThreadId();
}


static INLINE void
MXRecLockSetNoOwner(MXRecLock *lock)  // IN:
{
   lock->nativeThreadID = MXUSER_INVALID_OWNER;
}


static INLINE void
MXRecLockSetOwner(MXRecLock *lock)  // IN/OUT:
{
   lock->nativeThreadID = GetCurrentThreadId();
}


static INLINE int
MXRecLockAcquireInternal(MXRecLock *lock)  // IN:
{
   EnterCriticalSection(&lock->nativeLock);

   return 0;
}


static INLINE int
MXRecLockTryAcquireInternal(MXRecLock *lock)  // IN:
{
   return TryEnterCriticalSection(&lock->nativeLock) ? 0 : EBUSY;
}


static INLINE int
MXRecLockReleaseInternal(MXRecLock *lock)  // IN:
{
   LeaveCriticalSection(&lock->nativeLock);

   return 0;
}
#else
static INLINE int
MXRecLockCreateInternal(MXRecLock *lock)  // IN/OUT:
{
   return pthread_mutex_init(&lock->nativeLock, NULL);
}


static INLINE int
MXRecLockDestroyInternal(MXRecLock *lock)  // IN:
{
   return pthread_mutex_destroy(&lock->nativeLock);
}


static INLINE Bool
MXRecLockIsOwner(const MXRecLock *lock)  // IN:
{
   return pthread_equal(lock->nativeThreadID, pthread_self());
}


static INLINE void
MXRecLockSetNoOwner(MXRecLock *lock)  // IN/OUT:
{
   /* a hack but it works portably */
   memset((void *) &lock->nativeThreadID, 0xFF, sizeof(lock->nativeThreadID));
}


static INLINE void
MXRecLockSetOwner(MXRecLock *lock)  // IN:
{
   lock->nativeThreadID = pthread_self();
}


static INLINE int
MXRecLockAcquireInternal(MXRecLock *lock)  // IN:
{
   return pthread_mutex_lock(&lock->nativeLock);
}


static INLINE int
MXRecLockTryAcquireInternal(MXRecLock *lock)  // IN:
{
   return pthread_mutex_trylock(&lock->nativeLock);
}


static INLINE int
MXRecLockReleaseInternal(MXRecLock *lock)  // IN:
{
   return pthread_mutex_unlock(&lock->nativeLock);
}
#endif


static INLINE Bool
MXRecLockInit(MXRecLock *lock)  // IN/OUT:
{
   Bool success = (MXRecLockCreateInternal(lock) == 0);

   if (success) {
      MXRecLockSetNoOwner(lock);

      lock->referenceCount = 0;
   }

   return success;
}


static INLINE void
MXRecLockDestroy(MXRecLock *lock)  // IN/OUT:
{
   int err = MXRecLockDestroyInternal(lock);

   if (vmx86_debug && (err != 0)) {
      Panic("%s: MXRecLockDestroyInternal returned %d\n", __FUNCTION__, err);
   }
} 


static INLINE uint32
MXRecLockCount(const MXRecLock *lock)  // IN:
{
   return lock->referenceCount;
}


static INLINE void
MXRecLockIncCount(MXRecLock *lock,  // IN/OUT:
                  uint32 count)     // IN:
{
   if (MXRecLockCount(lock) == 0) {
      MXRecLockSetOwner(lock);
   }

   lock->referenceCount += count;
}


static INLINE Bool
MXRecLockAcquire(MXRecLock *lock)  // IN/OUT:
{
   Bool contended;

   if ((MXRecLockCount(lock) != 0) && MXRecLockIsOwner(lock)) {
      ASSERT((MXRecLockCount(lock) > 0) &&
             (MXRecLockCount(lock) < MXUSER_MAX_REC_DEPTH));

      contended = FALSE;
   } else {
      int err = MXRecLockTryAcquireInternal(lock);

      if (err == 0) {
         contended = FALSE;
      } else {
         if (vmx86_debug && (err != EBUSY)) {
            Panic("%s: MXRecLockTryAcquireInternal returned %d\n",
                  __FUNCTION__, err);
         }

         err = MXRecLockAcquireInternal(lock);
         contended = TRUE;
      }

      if (vmx86_debug && (err != 0)) {
         Panic("%s: MXRecLockAcquireInternal returned %d\n", __FUNCTION__,
               err);
      }

      ASSERT(lock->referenceCount == 0);
   }

   MXRecLockIncCount(lock, 1);

   return contended;
}


static INLINE Bool
MXRecLockTryAcquire(MXRecLock *lock)  // IN/OUT:
{
   int err;
   Bool acquired;

   err = MXRecLockTryAcquireInternal(lock);

   if (err == 0) {
      MXRecLockIncCount(lock, 1);

      ASSERT((MXRecLockCount(lock) > 0) &&
             (MXRecLockCount(lock) < MXUSER_MAX_REC_DEPTH));

      acquired = TRUE;
   } else {
      if (vmx86_debug && (err != EBUSY)) {
         Panic("%s: MXRecLockTryAcquireInternal returned %d\n", __FUNCTION__,
               err);
      }

      acquired = FALSE;
   }

   return acquired;
}

static INLINE void
MXRecLockDecCount(MXRecLock *lock,  // IN/OUT:
                  uint32 count)     // IN:
{
   ASSERT(count <= lock->referenceCount);
   lock->referenceCount -= count;

   if (MXRecLockCount(lock) == 0) {
      MXRecLockSetNoOwner(lock);
   }
}


static INLINE void
MXRecLockRelease(MXRecLock *lock)  // IN/OUT:
{
   ASSERT((MXRecLockCount(lock) > 0) &&
          (MXRecLockCount(lock) < MXUSER_MAX_REC_DEPTH));

   MXRecLockDecCount(lock, 1);

   if (MXRecLockCount(lock) == 0) {
      int err = MXRecLockReleaseInternal(lock);

      if (vmx86_debug && (err != 0)) {
         Panic("%s: MXRecLockReleaseInternal returned %d\n", __FUNCTION__,
               err);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * MXUserGetNativeTID --
 *
 *      Gets a native representation of the thread ID, which can be stored
 *      in a pointer.
 *
 * Results:
 *      Native representation of a thread ID.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static INLINE void *
MXUserGetNativeTID(void)
{
   /* All thread types must fit into a uintptr_t  */

#if defined(_WIN32)
   ASSERT_ON_COMPILE(sizeof(DWORD) <= sizeof (void *));
   return (void *) (uintptr_t) GetCurrentThreadId();
#else
   ASSERT_ON_COMPILE(sizeof(pthread_t) <= sizeof (void *));
   return (void *) (uintptr_t) pthread_self();
#endif
}


/*
 * MXUser header - all MXUser objects start with this
 */

typedef struct MXUserHeader {
   uint32       signature;
   MX_Rank      rank;
   char        *name;
   uint32       identifier;
   void       (*dumpFunc)(struct MXUserHeader *);

#if defined(MXUSER_STATS)
   void       (*statsFunc)(struct MXUserHeader *);
   ListItem     item;
#endif
} MXUserHeader;


/*
 * Internal functions
 */

void MXUserDumpAndPanic(MXUserHeader *header,
                        const char *fmt,
                        ...);

MXRecLock *MXUserInternalSingleton(Atomic_Ptr *storage);

#if defined(MXUSER_DEBUG)
void MXUserAcquisitionTracking(MXUserHeader *header,
                           Bool checkRank);
void MXUserReleaseTracking(MXUserHeader *header);
#else
static INLINE void
MXUserAcquisitionTracking(MXUserHeader *header,  // IN:
                          Bool checkRank)        // IN:
{
   return;
}

static INLINE void
MXUserReleaseTracking(MXUserHeader *header)  // IN:
{
   return;
}
#endif

static INLINE Bool
MXUserTryAcquireFail(const char *name)  // IN:
{
   extern Bool (*MXUserTryAcquireForceFail)(const char *name);

   return vmx86_debug && MXUserTryAcquireForceFail &&
          (*MXUserTryAcquireForceFail)(name);
}

MXUserCondVar *MXUserCreateCondVar(MXUserHeader *header,
                                   MXRecLock *lock);

Bool MXUserWaitCondVar(MXUserHeader *header,
                       MXRecLock *lock,
                       MXUserCondVar *condVar,
                       uint32 msecWait);


typedef struct {
   char    *typeName;        // Name
   uint64   numSamples;      // Population sample size
   uint64   minTime;         // Minimum
   uint64   maxTime;         // Maximum
   uint64   timeSum;         // Sum of times (for mean)
   double   timeSquaredSum;  // Sum of times^2 (for S.D.)
} MXUserBasicStats;

typedef struct {
   uint64            numAttempts;
   uint64            numSuccesses;
   uint64            numSuccessesContended;
   uint64            successContentionTime;
   uint64            totalContentionTime;

   MXUserBasicStats  basicStats;
} MXUserAcquisitionStats;

typedef struct {
   MXUserBasicStats  basicStats;       // total held statistics
} MXUserReleaseStats;

uint32 MXUserAllocID(void);

#if defined(MXUSER_STATS)
void MXUserAddToList(MXUserHeader *header);
void MXUserRemoveFromList(MXUserHeader *header);
#else
static INLINE void
MXUserAddToList(MXUserHeader *header)
{
   return;
}

static INLINE void
MXUserRemoveFromList(MXUserHeader *header)
{
   return;
}
#endif

typedef struct MXUserHisto MXUserHisto;

MXUserHisto *MXUserHistoSetUp(char *typeName,
                              uint64 minValue,
                              uint32 decades);

void MXUserHistoTearDown(MXUserHisto *histo);

void MXUserHistoSample(MXUserHisto *histo,
                       uint64 value,
                       void *caller);

void MXUserHistoDump(MXUserHisto *histo,
                     MXUserHeader *header);

void MXUserAcquisitionStatsSetUp(MXUserAcquisitionStats *stats);

void MXUserAcquisitionSample(MXUserAcquisitionStats *stats,
                             Bool wasAcquired,
                             Bool wasContended,
                             uint64 elapsedTime);

void MXUserDumpAcquisitionStats(MXUserAcquisitionStats *stats,
                                MXUserHeader *header);

void MXUserAcquisitionStatsTearDown(MXUserAcquisitionStats *stats);

void
MXUserBasicStatsSetUp(MXUserBasicStats *stats,
                      char *typeName);

void
MXUserBasicStatsSample(MXUserBasicStats *stats,
                       uint64 value);
 
void MXUserDumpBasicStats(MXUserBasicStats *stats,
                          MXUserHeader *header);

void MXUserBasicStatsTearDown(MXUserBasicStats *stats);

void MXUserKitchen(MXUserAcquisitionStats *stats,
                   double *contentionRatio,
                   Bool *isHot,
                   Bool *doLog);

void MXUserForceHisto(Atomic_Ptr *histoPtr,
                      char *typeName,
                      uint64 minValue,
                      uint32 decades);

extern void (*MXUserMX_LockRec)(struct MX_MutexRec *lock);
extern void (*MXUserMX_UnlockRec)(struct MX_MutexRec *lock);
extern Bool (*MXUserMX_TryLockRec)(struct MX_MutexRec *lock);
extern Bool (*MXUserMX_IsLockedByCurThreadRec)(const struct MX_MutexRec *lock);

#endif
