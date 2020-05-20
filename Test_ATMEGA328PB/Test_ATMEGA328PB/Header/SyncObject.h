/*
 * SyncObject.h
 *
 * Created: 05.05.2020 08:33:36
 *  Author: Berti
 */ 

#ifndef SYNCOBJECT_H_
#define SYNCOBJECT_H_

#include "Switcher.h"


// SyncObject is the common base for all synchronization objects (like (recursive) mutex, semaphor, event, ...).
// A sync object has either ownership or notification/event semantic, depending on the needs of the synchronization object using it.
// Common to all sync object is a waiting list and a flags field, other members depend on its semantic, ownership or notification.
//
// Possible states for ownership semantic:
//    - Free:              not owned, no one waiting 
//                         (m_pCurrentOwner == NULL, m_pWaitingList == NULL, m_pNextOwner == NULL)
//
//    - Owned:             has current owner, there may or may not be someone waiting
//                         (m_pCurrentOwner != NULL, m_pWaitingList != NULL or m_pWaitingList == NULL, m_pNextOwner == NULL)
//
//    - Pending new owner: has no current owner set, there is someone waiting 
//                         (m_pCurrentOwner == NULL, m_pWaitingList != NULL, m_pNextOwner != NULL)
//                         The previous owner has released the sync object and and woken up the next owner according to the waiting list at the time.
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
    };
  };       
  
  union
  {
    struct  // members for ownership semantic
    {
      SyncObject* m_pAcquiredListNext;  // acquired list next pointer, next pointer for list of all currently acquired sync objects by current owner
      Task*       m_pCurrentOwner;      // current owner task, must not be set in state "pending new owner"!
      Task*       m_pNextOwner;         // task that has been woken up by previous owner to become the new owner 
                                        // Needed to distinguish between timeout and pending ownership for a task.
    };
    
    struct  // members for notification/event semantic 
    {
      Task* m_pNotificationList;  // list of all notified tasks, uses m_pWaitingListNext member in task struct
    };
  };    
} SyncObject;

#define SWITCHER_SYNCOBJECT_WITH_OWNERSHIP_STATIC_INIT()    {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = true,  .m_pAcquiredListNext = NULL, .m_pCurrentOwner = NULL, .m_pNextOwner = NULL}
#define SWITCHER_SYNCOBJECT_WITH_NOTIFICATION_STATIC_INIT() {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = false, .m_pNotificationList = NULL}

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline bool IsFreeSyncObject(const SyncObject* syncObject)
{
  SWITCHER_ASSERT(syncObject != NULL);
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);
  
  if ((syncObject->m_pCurrentOwner == NULL) && (syncObject->m_pWaitingList == NULL))
  {
    SWITCHER_ASSERT(syncObject->m_pNextOwner == NULL);
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

  if (syncObject->m_pCurrentOwner != NULL)
  {
    SWITCHER_ASSERT(syncObject->m_pNextOwner == NULL);
    
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
  
  if (syncObject->m_pCurrentOwner == task)
  {
    SWITCHER_ASSERT(syncObject->m_pNextOwner == NULL);
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
  
  if (syncObject->m_pNextOwner == task)
  {
    SWITCHER_ASSERT(syncObject->m_pCurrentOwner == NULL);
    SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);
    SWITCHER_ASSERT(syncObject->m_pAcquiredListNext == NULL);
    
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
  syncObject->m_pCurrentOwner = task;
  
  // reset next owner ptr
  syncObject->m_pNextOwner = NULL;
  
  // add sync object to acquired list
  syncObject->m_pAcquiredListNext = task->m_pAcquiredList;
  task->m_pAcquiredList = syncObject;
  
  // check for higher prio task in waiting list (someone may have queued up between prev owners release and us taking ownership!)
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
__attribute__((always_inline))
static inline void ReleaseSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);
  SWITCHER_ASSERT(IsCurrentSyncObjectOwner(syncObject, task));
  SWITCHER_ASSERT(task->m_pAcquiredList != NULL);
  
  // remove sync object from tasks acquire list
  if (task->m_pAcquiredList == syncObject)
  {
    task->m_pAcquiredList = task->m_pAcquiredList->m_pAcquiredListNext;
  }
  else
  {
    SyncObject* syncObjectIter = task->m_pAcquiredList;
    
    SWITCHER_ASSERT(syncObjectIter->m_pAcquiredListNext != NULL);
    
    while(syncObjectIter->m_pAcquiredListNext != syncObject)
    {
      syncObjectIter = syncObjectIter->m_pAcquiredListNext;
      
      SWITCHER_ASSERT(syncObjectIter->m_pAcquiredListNext != NULL);
    }
    
    SWITCHER_ASSERT((syncObjectIter != NULL) && (syncObjectIter->m_pAcquiredListNext == syncObject));
    
    syncObjectIter->m_pAcquiredListNext = syncObject->m_pAcquiredListNext;
  }
  
  syncObject->m_pAcquiredListNext = NULL;
  
  syncObject->m_pCurrentOwner = NULL;
  
  // set new pending ownership if a task is in the waiting list
  if (syncObject->m_pWaitingList != NULL)
  {
    // set pending new ownership
    syncObject->m_pNextOwner = syncObject->m_pWaitingList;  // new owner is always in front for waiting list
        
    SWITCHER_DISABLE_INTERRUPTS();
    
    // wake new owner if he is sleeping
    if (syncObject->m_pNextOwner->m_SleepCount > 0)
    {
      syncObject->m_pNextOwner->m_SleepCount = 0;
      g_ActiveTasks++;
    }
    
    SWITCHER_ENABLE_INTERRUPTS();

    // task priority for new owner will be fixed up when next owner takes ownership of sync object
  }
  
  // do we have to check the priority of the releasing task?
  if (task->m_Priority > task->m_BasePriority)
  {
    SWITCHER_ASSERT(task->m_pIsWaitingFor == NULL);
    
    // find highest priority in tasks acquired list that is higher than his base priority
    Priority newPrio = task->m_BasePriority;
    
    const SyncObject* syncObjectIter = task->m_pAcquiredList;
    
    while (syncObjectIter != NULL)
    {
      if ((syncObjectIter->m_pWaitingList != NULL) &&
          (syncObjectIter->m_pWaitingList->m_Priority > newPrio))  // first task in waiting list always has highest priority
      {
        newPrio = syncObjectIter->m_pWaitingList->m_Priority;
      }
      
      syncObjectIter = syncObjectIter->m_pAcquiredListNext;
    }
    
    task->m_Priority = newPrio;
  }
}


// expects: task switcher is currently paused
//          task is not already waiting for another sync object
//          if sync object has ownership semantic it must not be in state Free
__attribute__((always_inline))
static inline void QueueForSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT((syncObject->m_HasOwnershipSemantic && !IsFreeSyncObject(syncObject) && !IsCurrentSyncObjectOwner(syncObject, task)) ||
                   !syncObject->m_HasOwnershipSemantic);
  SWITCHER_ASSERT(task->m_pIsWaitingFor == NULL);
  
  // add task to waiting list
  if (syncObject->m_pWaitingList == NULL)
  {
    syncObject->m_pWaitingList = task;
  }
  else
  {
    // find correct spot in the waiting list
    Task* taskIter = syncObject->m_pWaitingList;
    
    while ((taskIter->m_pWaitingListNext != NULL) &&
    (taskIter->m_pWaitingListNext->m_Priority >= task->m_Priority))
    {
      taskIter = taskIter->m_pWaitingListNext;
    }
    
    SWITCHER_ASSERT((taskIter->m_pWaitingListNext == NULL) || (taskIter->m_pWaitingListNext->m_Priority < task->m_Priority));

    // insert task behind taskIter
    task->m_pWaitingListNext = taskIter->m_pWaitingListNext;
    taskIter->m_pWaitingListNext = task;
  }
  
  // set is waiting for member in task to sync object
  task->m_pIsWaitingFor = syncObject;
  
  // check for priority inversion if we have ownership semantic and there is a current owner
  if (syncObject->m_HasOwnershipSemantic &&
      IsOwnedSyncObject(syncObject) &&
      (syncObject->m_pCurrentOwner->m_Priority < task->m_Priority))
  {
    // grant current owner or own priority
    syncObject->m_pCurrentOwner->m_Priority = task->m_Priority;
    
    // check if owner is currently waiting of another sync object so we can (recursively!) update his position in the waiting list and also update the owners prio
    if (syncObject->m_pCurrentOwner->m_pIsWaitingFor != NULL)
    {
      // TODO: fix position in waiting list!
      // TODO: if owner is waiting for an sync object with ownership semantic we might also have to bump the owners prio!
    }
  }
}

// expects: task switcher is currently paused
//          task is currently waiting for the sync object (and with ownership semantic we must not be the current owner, but may be pending new owner)
__attribute__((always_inline))
static inline void UnqueueFromSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));  
  SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);
  SWITCHER_ASSERT(task->m_pIsWaitingFor == syncObject);
  SWITCHER_ASSERT(!IsCurrentSyncObjectOwner(syncObject, task));
  
  // remove task from waiting list
  if (syncObject->m_pWaitingList == task)
  {
    syncObject->m_pWaitingList = syncObject->m_pWaitingList->m_pWaitingListNext;
  }
  else
  {
    // find task in waiting list
    Task* taskIter = syncObject->m_pWaitingList;

    SWITCHER_ASSERT(taskIter->m_pWaitingListNext != NULL);
    
    while (taskIter->m_pWaitingListNext != task)
    {
      taskIter = taskIter->m_pWaitingListNext;
      
      SWITCHER_ASSERT(taskIter->m_pWaitingListNext != NULL);
    }
    
    SWITCHER_ASSERT(taskIter->m_pWaitingListNext == task);
    
    taskIter->m_pWaitingListNext = task->m_pWaitingListNext;
  }
  
  task->m_pWaitingListNext = NULL;
  task->m_pIsWaitingFor = NULL;
  
  // do we have to change the owners priority, only needed for ownership semantic
  if (syncObject->m_HasOwnershipSemantic &&
      IsOwnedSyncObject(syncObject) &&
      (syncObject->m_pCurrentOwner->m_BasePriority < syncObject->m_pCurrentOwner->m_Priority) &&
      (syncObject->m_pCurrentOwner->m_Priority == task->m_Priority))  // TODO: do we have to inherit prio changes recursively to all tasks which prio depends on this owner !?!?!?
  {
    Priority newPrio = syncObject->m_pCurrentOwner->m_BasePriority;
    
    // find new highest prio in all waiting lists of current owners acquired list
    const SyncObject* syncObjectIter = syncObject->m_pCurrentOwner->m_pAcquiredList;
    
    SWITCHER_ASSERT(syncObjectIter != NULL);

    while (syncObjectIter != NULL)
    {
      // only consider sync objects with ownership, first in waiting list has highest prio
      if (syncObjectIter->m_HasOwnershipSemantic &&
          (syncObjectIter->m_pWaitingList != NULL) && 
          (syncObjectIter->m_pWaitingList->m_Priority > newPrio))
      {
        newPrio = syncObjectIter->m_pWaitingList->m_Priority;
      }
    }

    // no need to trigger a forced switch here, the new prio can only be <= our own prio!
    SWITCHER_ASSERT(newPrio <= task->m_Priority);
    SWITCHER_ASSERT(newPrio >= syncObject->m_pCurrentOwner->m_BasePriority);

    syncObject->m_pCurrentOwner->m_Priority = newPrio;
    
    // check if owner is currently waiting of another sync object so we can (recursively!) update his position in the waiting list and also update the owners prio
    if (syncObject->m_pCurrentOwner->m_pIsWaitingFor != NULL)
    {
      // TODO: fix position in waiting list!
      // TODO: if owner is waiting for an sync object with ownership semantic we might also have to bump the owners prio!
    }
  }
}

#endif /* SYNCOBJECT_H_ */
