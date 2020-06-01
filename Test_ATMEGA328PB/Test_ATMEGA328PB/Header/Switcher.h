/*
 * Switcher.h
 *
 * Created: 15.02.2019 11:51:01
 *  Author: Berti
 */ 

#ifndef SWITCHER_H_
#define SWITCHER_H_

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdnoreturn.h>

#include <avr/io.h>
#include <avr/interrupt.h>

#ifdef RAMPX
# define SWITCHER_RAMPX_SIZE 1
#else
# define SWITCHER_RAMPX_SIZE 0
#endif

#ifdef RAMPY
# define SWITCHER_RAMPY_SIZE 1
#else
# define SWITCHER_RAMPY_SIZE 0
#endif

#ifdef RAMPZ
# define SWITCHER_RAMPZ_SIZE 1
#else
# define SWITCHER_RAMPZ_SIZE 0
#endif

#ifdef RAMPD
# define SWITCHER_RAMPD_SIZE 1
#else
# define SWITCHER_RAMPD_SIZE 0
#endif

#ifdef EIND
# define SWITCHER_EIND_SIZE 1
#else
# define SWITCHER_EIND_SIZE 0
#endif

#define SWITCHER_EXTENSION_REGS_SIZE (SWITCHER_RAMPX_SIZE + SWITCHER_RAMPY_SIZE + SWITCHER_RAMPX_SIZE + SWITCHER_RAMPD_SIZE + SWITCHER_EIND_SIZE)

#ifdef __AVR_2_BYTE_PC__
# define SWITCHER_RETURN_ADDR_SIZE 2
#elif defined(__AVR_3_BYTE_PC__)
# define SWITCHER_RETURN_ADDR_SIZE 3
#else
# error "Unknown return address size"
#endif


#define SWITCHER_DISABLE_INTERRUPTS()            \
  __attribute__ ((unused)) uint8_t sreg = SREG;  \
  cli();                                         \
  asm volatile ("" ::: "memory")


#define SWITCHER_ENABLE_INTERRUPTS()          \
  SREG = sreg;                                \
  asm volatile ("" ::: "memory")

#ifdef DEBUG
# define SWITCHER_ASSERT(condition)     \
           if (!(condition))            \
           {                            \
             while (true)               \
             {                          \
               asm volatile ("break");  \
             }                          \
           }
#else
# define SWITCHER_ASSERT(condition)
#endif


// error codes
typedef enum {SwitcherNoError = 0,
              SwitcherNotInitialized,
              SwitcherInvalidParameter,
              SwitcherTimeout,
              SwitcherTooManyTasks,
              SwitcherResourceNotOwned,
              } SwitcherError;

// 64 bit arithmetic produces horrible code (especially within an ISR!)
// doing it manually produces denser AND faster code in an ISR ....
typedef struct
{
  union
  {
    uint64_t m_TickCount;
    
    struct
    {
      uint32_t m_TickCountLow;
      uint32_t m_TickCountHigh;
    };
  };
} TickCount;


typedef enum {PriorityIdle    = 0, 
              PriorityLowest  = 1,
              PriorityLow     = 64,
              PriorityNormal  = 128,
              PriorityHigh    = 192,
              PriorityHighest = 255
              } Priority;

typedef enum {TimeoutNone     = 0,
              TimeoutMaximum  = 0xfffe,
              TimeoutInfinite = 0xffff
              } Timeout;

enum {MaxNumberOfTasks = 0xff};

typedef struct Task_       Task;
typedef struct SyncObject_ SyncObject;

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
      uint16_t m_NotificationCounter;  // unused by SyncObject implementation, users may freely use
      bool     m_PendingNotification;         // unused by SyncObject implementation, users may freely use
    };
  };
} SyncObject;

typedef struct Task_
{
  void*    m_pStackPointer;          // points to the last saved byte on the stack, not the first free byte
  
  uint16_t m_SleepCount;             // remaining number of switcher ticks this task is sleeping, requires IRQ protection
                                     // 0 == active, 0xffff = infinite
  uint8_t  m_PauseSwitchingCounter;  // tracks number of PauseSwitching calls, allows for nested Pause/Resume blocks, requires IRQ protection, only use Pause-/ResumeSwitching

  Task*    m_pTaskListNext;          // task list next pointer, ring list, requires task switcher to be paused
  Task*    m_pWaitingListNext;       // waiting list next pointer, linear list, task waits for an synchronization object, requires task switcher to be paused
  
  SyncObject* m_pAcquiredList;       // list of all currently acquired sync object by this task, requires task switcher to be paused
  SyncObject* m_pIsWaitingFor;       // pointer to sync object the task is currently waiting for, requires task switcher to be paused

  SyncObject m_JoinNotification;     // sync object used to implement join
  
  Priority m_BasePriority;           // assigned base priority, requires task switcher to be paused
  Priority m_Priority;               // actual current priority, requires task switcher to be paused

#if 0
  // stack check only
  void*    m_StackBuffer;            // may be NULL for main task
  uint16_t m_StackSize;              // may be 0 for main task
#endif  
} Task;

typedef void (*TaskFunction)(void*);

extern Task* g_CurrentTask;   // DO NOT TOUCH!
extern uint8_t g_ActiveTasks; // DO NOT TOUCH!

static_assert(sizeof(g_CurrentTask->m_SleepCount) == sizeof(Timeout), "Task.m_SleepCount must have same size as Timeout type");

static_assert(MaxNumberOfTasks == (sizeof(g_ActiveTasks) * 0xff), "Mismatch between MaxNumberOfTasks and sizeof(g_ActiveTasks)");

// TODO: check if SWITCHER_TASK_STATE_SIZE and SWITCHER_TASK_STATE_MIN_STACK_SIZE are correct if we have (SWITCHER_RETURN_ADDR_SIZE > 2)!!!

// 32 register + SREG + extension registers + return address 
#define SWITCHER_TASK_STATE_SIZE (32 + 1 + SWITCHER_EXTENSION_REGS_SIZE + SWITCHER_RETURN_ADDR_SIZE)

// task state + call into task function + address of task function + parameter for task function
#define SWITCHER_TASK_STATE_MIN_STACK_SIZE (SWITCHER_TASK_STATE_SIZE)

SwitcherError Initialize(Task* mainTask);

// preserves the global interrupt flag and state of switcher IRQs
// interrupts are enabled during execution but state is restored before return
void Yield();

// terminates the current task, does not return
noreturn void TerminateTask();

// preserves the global interrupt flag and state of switcher IRQs
// interrupts are enabled during execution but state is restored before return
void Sleep(Timeout timeout);

// TODO: test if this causes more or less code then using ABI!
#define PauseSwitching() asm volatile ("call PauseSwitchingImpl" ::: "r18", "r19", "r30", "r31", "memory")

#define ResumeSwitching() asm volatile ("call ResumeSwitchingImpl" ::: "r18", "r19", "r30", "r31", "memory")

// <stackBuffer> points to the beginning of the stack memory, 
//               first stack location will be (stackBuffer + (stackSize - 1))!
SwitcherError AddTask(Task* task,
                      void* stackBuffer, size_t stackSize,
                      TaskFunction taskFunction, void* taskParameter,
                      Priority priority);

// returns SwitcherNoError if task is unknown or has been joined
// returns SwitcherInvalidParameter for task == g_CurrentTask
SwitcherError JoinTask(Task* task, Timeout timeout);

TickCount GetSwitcherTickCount();

bool IsKnownTask(const Task* task);

#endif /* SWITCHER_H_ */
