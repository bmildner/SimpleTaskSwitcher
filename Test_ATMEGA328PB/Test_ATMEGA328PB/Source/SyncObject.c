/*
 * SyncObject.c
 *
 * Created: 25.05.2020 06:36:10
 *  Author: Berti
 */ 

#include "SyncObject.h"

// expects: task switcher is currently paused
//          task is not already waiting for this or any other sync object
__attribute__((always_inline))
static inline void AddTaskToSyncObjectsWaitingList(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT(task->m_pWaitingListNext == NULL);
  
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
}

// expects: task switcher is currently paused
//          task is currently waiting for this sync object
__attribute__((always_inline))
static inline void RemoveTaskFromSyncObjectsWaitingList(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT(task->m_pWaitingListNext != NULL);
  
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
}


void ReleaseSyncObject(SyncObject* syncObject, Task* task)
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

  // set new pending ownership if a task is in the waiting list
  if (syncObject->m_pWaitingList != NULL)
  {
    // set pending new ownership
    syncObject->m_pCurrentOrNextOwner = syncObject->m_pWaitingList;  // new owner is always in front for waiting list
    syncObject->m_HasPendingNewOwner = true;
    
    SWITCHER_DISABLE_INTERRUPTS();
    
    // wake new owner if he is sleeping
    if (syncObject->m_pCurrentOrNextOwner->m_SleepCount > 0)
    {
      syncObject->m_pCurrentOrNextOwner->m_SleepCount = 0;
      g_ActiveTasks++;
    }
    
    SWITCHER_ENABLE_INTERRUPTS();

    // task priority for new owner will be fixed up when next owner takes ownership of sync object
  }
  else
  {
    syncObject->m_pCurrentOrNextOwner = NULL;
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
//          sync object has notification semantic
//          there is at least one task waiting
__attribute__((always_inline))
static inline void SyncObjectNotifyOneImpl(SyncObject* syncObject)
{
  Task* task = syncObject->m_pWaitingList;
  
  RemoveTaskFromSyncObjectsWaitingList(syncObject, task);
  
  SWITCHER_DISABLE_INTERRUPTS();
  
  // wake task if he is sleeping
  if (task->m_SleepCount > 0)
  {
    task->m_SleepCount = 0;
    g_ActiveTasks++;
  }
  
  SWITCHER_ENABLE_INTERRUPTS();
}

void SyncObjectNotifyOne(SyncObject* syncObject)
{
  SWITCHER_ASSERT(syncObject != NULL);
  SWITCHER_ASSERT(!syncObject->m_HasOwnershipSemantic);
  SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);

  SyncObjectNotifyOneImpl(syncObject);  
}

void SyncObjectNotifyAll(SyncObject* syncObject)
{
  SWITCHER_ASSERT(syncObject != NULL);
  SWITCHER_ASSERT(!syncObject->m_HasOwnershipSemantic);
  SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);

  do 
  {
    SyncObjectNotifyOneImpl(syncObject);
  } while (syncObject->m_pWaitingList != NULL);
  
  SWITCHER_ASSERT(syncObject->m_pWaitingList == NULL);
}


void QueueForSyncObject(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT((syncObject->m_HasOwnershipSemantic && !IsFreeSyncObject(syncObject) && !IsCurrentSyncObjectOwner(syncObject, task)) ||
                  !syncObject->m_HasOwnershipSemantic);
  SWITCHER_ASSERT(task->m_pIsWaitingFor == NULL);
  
  // add task to waiting list according to his priority
  AddTaskToSyncObjectsWaitingList(syncObject, task);
  
  // check for priority inversion, in case of ownership semantic, if there is a current owner and his priority is lower than our priority
  while ((syncObject != NULL) &&
         syncObject->m_HasOwnershipSemantic &&
         IsOwnedSyncObject(syncObject) &&
         (syncObject->m_pCurrentOrNextOwner->m_Priority < task->m_Priority))
  {
    // grant current owner our own priority
    syncObject->m_pCurrentOrNextOwner->m_Priority = task->m_Priority;
    
    // check if owner is currently waiting of another sync object so we can (recursively!) update his position in the waiting list and also update the owners priority if necessary
    if (syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor != NULL)
    {
      // fix position in waiting list!
      RemoveTaskFromSyncObjectsWaitingList(syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor, syncObject->m_pCurrentOrNextOwner);
      AddTaskToSyncObjectsWaitingList(syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor, syncObject->m_pCurrentOrNextOwner);
    }
    
    // if owner is waiting for an sync object might also have to bump the owners priority of that sync object!
    syncObject = syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor;
  }
}

void UnqueueFromSyncObject(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);
  SWITCHER_ASSERT(task->m_pIsWaitingFor == syncObject);
  SWITCHER_ASSERT(!IsCurrentSyncObjectOwner(syncObject, task));
  
  RemoveTaskFromSyncObjectsWaitingList(syncObject, task);
  
  // do we have to check the owners priority, only needed for ownership semantic
  while (syncObject->m_HasOwnershipSemantic &&
         IsOwnedSyncObject(syncObject) &&
         (syncObject->m_pCurrentOrNextOwner->m_BasePriority < syncObject->m_pCurrentOrNextOwner->m_Priority) &&
         (syncObject->m_pCurrentOrNextOwner->m_Priority == task->m_Priority))
  {
    Priority newPrio = syncObject->m_pCurrentOrNextOwner->m_BasePriority;
    
    // find new highest priority in all waiting lists of current owners acquired list
    const SyncObject* syncObjectIter = syncObject->m_pCurrentOrNextOwner->m_pAcquiredList;
    
    SWITCHER_ASSERT(syncObjectIter != NULL);

    while (syncObjectIter != NULL)
    {
      // only consider sync objects with ownership, first in waiting list has highest priority
      if (syncObjectIter->m_HasOwnershipSemantic &&
          (syncObjectIter->m_pWaitingList != NULL) &&
          (syncObjectIter->m_pWaitingList->m_Priority > newPrio))
      {
        newPrio = syncObjectIter->m_pWaitingList->m_Priority;
      }
    }

    // no need to trigger a forced switch here, the new priority can only be <= our own priority!
    SWITCHER_ASSERT(newPrio <= task->m_Priority);
    SWITCHER_ASSERT(newPrio >= syncObject->m_pCurrentOrNextOwner->m_BasePriority);
    
    // check if owner is currently waiting of another sync object so we can (recursively!) update his position in the waiting list and also update the owners priority
    if ((syncObject->m_pCurrentOrNextOwner->m_Priority != newPrio) &&
        (syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor != NULL))
    {
      // set new priority
      syncObject->m_pCurrentOrNextOwner->m_Priority = newPrio;
      
      // fix position in waiting list!
      RemoveTaskFromSyncObjectsWaitingList(syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor, syncObject->m_pCurrentOrNextOwner);
      AddTaskToSyncObjectsWaitingList(syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor, syncObject->m_pCurrentOrNextOwner);
    }
    else
    {
      // set new priority
      syncObject->m_pCurrentOrNextOwner->m_Priority = newPrio;
    }

    // if owner is waiting for an sync object with ownership semantic we might also have to bump the owners priority!
    syncObject = syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor;
  }
}
