/*
 * Event.c
 *
 * Created: 26.05.2020 10:17:20
 *  Author: Berti
 */ 

#include "Event.h"

SwitcherError WaitForEvent(Event* event, Timeout timeout)
{
  if (g_CurrentTask == NULL)
  {
    return SwitcherNotInitialized;
  }

  if (event == NULL)
  {
    return SwitcherInvalidParameter;
  }

  PauseSwitching();

  if (event->m_PendingNotification)
  {
    event->m_PendingNotification = false;
    
    ResumeSwitching();
    
    return SwitcherNoError;
  }
  
  if (timeout == TimeoutNone)
  {
    ResumeSwitching();
      
    return SwitcherTimeout;
  }
    
  QueueForSyncObject(&event->m_SyncObject, g_CurrentTask);
    
  Sleep(timeout);
  
  // did we time out
  if (g_CurrentTask->m_pIsWaitingFor == &event->m_SyncObject)
  {
    UnqueueFromSyncObject(&event->m_SyncObject, g_CurrentTask);
    
    SWITCHER_ASSERT(g_CurrentTask->m_pIsWaitingFor == NULL);
    
    ResumeSwitching();
    
    return SwitcherTimeout;
  }
  
  SWITCHER_ASSERT(g_CurrentTask->m_pIsWaitingFor == NULL);
    
  ResumeSwitching();
  
  return SwitcherNoError;
}

SwitcherError EventNotifyOne(Event* event)
{
  if (g_CurrentTask == NULL)
  {
    return SwitcherNotInitialized;
  }

  if (event == NULL)
  {
    return SwitcherInvalidParameter;
  }

  PauseSwitching();

  if (event->m_SyncObject.m_pWaitingList == NULL)
  {
    event->m_PendingNotification = true;    
  }  
  else
  {
    SyncObjectNotifyOne(&event->m_SyncObject);
  }  
  
  ResumeSwitching();
    
  return SwitcherNoError;  
}

SwitcherError EventNotifyAll(Event* event)
{
  if (g_CurrentTask == NULL)
  {
    return SwitcherNotInitialized;
  }

  if (event == NULL)
  {
    return SwitcherInvalidParameter;
  }

  PauseSwitching();

  if (event->m_SyncObject.m_pWaitingList == NULL)
  {
    event->m_PendingNotification = true;
  }
  else
  {
    SyncObjectNotifyAll(&event->m_SyncObject);
  }
  
  ResumeSwitching();
  
  return SwitcherNoError;  
}
