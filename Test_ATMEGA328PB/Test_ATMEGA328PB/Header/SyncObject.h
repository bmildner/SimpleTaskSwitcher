/*
 * SyncObject.h
 *
 * Created: 05.05.2020 08:33:36
 *  Author: Berti
 */ 

#ifndef SYNCOBJECT_H_
#define SYNCOBJECT_H_

#include "Switcher.h"


// SyncObject is the common base for all synchronization objects (like (recursive) mutex, semaphore, event, ...).
// A sync object has either ownership or notification/event semantic, depending on the needs of the synchronization object using it.
// Common to all sync object is a waiting list and a flags field, other members depend on its semantic, ownership or notification.
//
// Possible states for ownership semantic:
//    - Free:              not owned, no one waiting 
//                         (m_pCurrentOrNextOwner == NULL, m_pWaitingList == NULL, m_HasPendingNewOwner == false)
//
//    - Owned:             has current owner, there may or may not be someone waiting
//                         (m_pCurrentOrNextOwner != NULL, m_pWaitingList != NULL or m_pWaitingList == NULL, m_HasPendingNewOwner == false)
//
//    - Pending new owner: has no current owner, there is someone waiting 
//                         (m_pCurrentOrNextOwner != NULL, m_pWaitingList != NULL, m_HasPendingNewOwner = true)
//                         The previous owner has released the sync object and woken up the next owner according to the waiting list at the time.
//                         Any task trying to acquire a sync object in this state has to queue up in the waiting list even if it has a higher priority
//                         than anyone already waiting (incl. the task woken by the previous owner!).
//
typedef struct  SyncObject_  // all members require the task switcher to be paused
{
  Task* m_pWaitingList;  // waiting list head, waiting tasks are sorted by priority (highest first) and FIFO within a given priority
                         // -> first task in waiting list is of the highest priority currently waiting and is the next to aquire
  union
  {
    uint8_t m_Flags;  // storage of bitfield flags below
    
    struct    
    {
      bool m_HasOwnershipSemantic : 1;  // set if sync object is used with ownership semantic, notification/event semantic otherwise
      bool m_HasPendingNewOwner : 1;    // set if state is "Pending new Owner"
    };
  };       
  
  union
  {
    struct  // members for ownership semantic
    {
      SyncObject* m_pAcquiredListNext;    // acquired list next pointer, next pointer for list of all currently acquired sync objects by current owner
      Task*       m_pCurrentOrNextOwner;  // current or next owner task, 
    };
    
    struct  // members for notification/event semantic 
    {
    };
  };    
} SyncObject;

#define SWITCHER_SYNCOBJECT_WITH_OWNERSHIP_STATIC_INIT()    {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = true,  .m_pAcquiredListNext = NULL, .m_pCurrentOrNextOwner = NULL}
#define SWITCHER_SYNCOBJECT_WITH_NOTIFICATION_STATIC_INIT() {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = false}

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline bool IsFreeSyncObject(const SyncObject* syncObject)
{
  SWITCHER_ASSERT(syncObject != NULL);
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);
  
  if (syncObject->m_pCurrentOrNextOwner == NULL)
  {
    SWITCHER_ASSERT(!syncObject->m_HasPendingNewOwner);
    SWITCHER_ASSERT(syncObject->m_pWaitingList == NULL);
    SWITCHER_ASSERT(syncObject->m_pAcquiredListNext == NULL);
    
    return true;
  }
  
  return false;
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline bool IsOwnedSyncObject(const SyncObject* syncObject)
{
  SWITCHER_ASSERT(syncObject != NULL);
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);

  if ((syncObject->m_pCurrentOrNextOwner != NULL) && (!syncObject->m_HasPendingNewOwner))
  {
    return true;
  }
  
  return false;
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline bool IsCurrentSyncObjectOwner(const SyncObject* syncObject, const Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);
  
  if ((syncObject->m_pCurrentOrNextOwner == task) && (!syncObject->m_HasPendingNewOwner))
  {
    SWITCHER_ASSERT(task->m_pAcquiredList != NULL);
    
    return true;
  }
  
  return false;
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline bool IsNextSyncObjectOwner(const SyncObject* syncObject, const Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);
  
  if ((syncObject->m_pCurrentOrNextOwner == task) && (syncObject->m_HasPendingNewOwner))
  {
    SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);
    
    return true;
  }
  
  return false;
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
//          sync object has to be either in state free or pending new owner where <task> is the designated new owner
__attribute__((always_inline))
static inline void AcquireSyncObject(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);  
  SWITCHER_ASSERT(IsFreeSyncObject(syncObject) || IsNextSyncObjectOwner(syncObject, task));
  
  // set new owner
  syncObject->m_pCurrentOrNextOwner = task;
  
  // reset has pending new owner flag
  syncObject->m_HasPendingNewOwner = false;
  
  // add sync object to acquired list
  syncObject->m_pAcquiredListNext = task->m_pAcquiredList;
  task->m_pAcquiredList = syncObject;
  
  // check for higher priority task in waiting list (someone may have queued up between previous owners release and us taking ownership!)
  if ((syncObject->m_pWaitingList != NULL) && 
      (syncObject->m_pWaitingList->m_Priority > task->m_Priority))
  {
    task->m_Priority = syncObject->m_pWaitingList->m_Priority;
    
    SWITCHER_ASSERT(task->m_Priority > task->m_BasePriority);
  }
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
//          task is the current owner of the sync object
void ReleaseSyncObject(SyncObject* syncObject, Task* task);


// expects: task switcher is currently paused
//          sync object has notification semantic
//          there is at least one task waiting
void SyncObjectNotifyOne(SyncObject* syncObject);

// expects: task switcher is currently paused
//          sync object has notification semantic
//          there is at least one task waiting
void SyncObjectNotifyAll(SyncObject* syncObject);


// expects: task switcher is currently paused
//          task is not already waiting for another sync object
//          if sync object has ownership semantic it must not be in state Free
void QueueForSyncObject(SyncObject* syncObject, Task* task);

// expects: task switcher is currently paused
//          task is currently waiting for the sync object (and with ownership semantic we must not be the current owner, but may be pending new owner)
void UnqueueFromSyncObject(SyncObject* syncObject, Task* task);

#endif /* SYNCOBJECT_H_ */
