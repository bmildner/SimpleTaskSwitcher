/*
 * Switcher.c
 *
 * Created: 15.02.2019 11:50:37
 *  Author: Berti
 */ 

#include "Switcher.h"

#include <avr/sleep.h>

#include <inttypes.h>
#include <string.h>
#include <stddef.h>

#include "SwitcherConfig.h"


#define SWITCHER_STACK_SIZE 32  // TODO: correctly size stack!!

typedef enum {Yielded = 0, PreemptiveSwitch, ForcedSwitch, SwitcherTick, TerminatingTask} SwitchingSource;

// used as return value for SwitcherCore() function
typedef struct
{
  void* m_NewSP;         // new stack pointer after switch, may be same as current
  
  Task* m_PreviousTask;  // pointer to task struct of previous task, may be same as current task
} SwitcherResult;


Task* g_CurrentTask = NULL;  // requires task switcher to be paused

static uint8_t g_Tasks = 0; // total number of tasks excl. IdleTask, requires task switcher to be paused
uint8_t g_ActiveTasks = 0;  // number of currently active task excl. IdleTask, requires IRQ protection!

static TickCount g_TickCount;  // total number (64bit) of switcher tick counts since initialization

static uint8_t g_SwitcherStackData[SWITCHER_STACK_SIZE];

static Task    g_IdleTask;
static uint8_t g_IdleTaskStackBuffer[SWITCHER_TASK_STATE_SIZE * 2];  // TODO: correctly size stack!!


__attribute__((naked, used))
void YieldImpl();

__attribute__((naked, used))
noreturn void TerminateTaskImpl();

__attribute__((naked, used))
void PauseSwitchingImpl();

__attribute__((naked, used))
void ResumeSwitchingImpl();


// Common part of task switcher asm implementation, calls SwitcherCore
__attribute__((naked, used))
static void SwitcherImpl();

// Task switcher core implementation in C
//   Takes switching source and SP of current task, returns SP of new task and pointer to previous task
__attribute__((noinline, used))
static SwitcherResult SwitcherCore(SwitchingSource source, void* stackPointer);

// Handles monotone switch tick, called from SwitchCore
__attribute__((always_inline, used))
static inline Task* SwitcherTickCore();


// Common asm code every switcher entry function (Yield, ISRs, ...) starts with
//   expects: interrupts are disabled
#define SwitcherEntryImpl(source)                                               \
  asm volatile ("push r17         \n\t"   /* save r17 */                        \
                "ldi r17,%[src]   \n\t"   /* setup switcher source */           \
                "jmp SwitcherImpl"        /* jump to switcher implementation */ \
                :: [src] "i"(source))

__attribute__((used))
static noreturn void TaskStartup(TaskFunction taskFunction, void* taskParameter);

typedef void (*TaskStartupFunctionPointer) (TaskFunction, void*);

ISR(SWITCHER_FORCED_SWITCH_VECTOR, ISR_NAKED)
{
  SwitcherEntryImpl(ForcedSwitch);
}

ISR(SWITCHER_PREEMPTIVE_SWITCH_VECTOR, ISR_NAKED)
{
  SwitcherEntryImpl(PreemptiveSwitch);
}

// TODO: maybe change to normal IRQ + trigger a forced switch if needed!?!?!
ISR(SWITCHER_TICK_VECTOR, ISR_NAKED)
{
  SwitcherEntryImpl(SwitcherTick);
}

void YieldImpl()
{
  SwitcherEntryImpl(Yielded);
}

void TerminateTaskImpl()
{
  SwitcherEntryImpl(TerminatingTask);
}

/*
  Layout of full task state on the tasks stack in oder of saving:
    R17            (source)    
    R31 ... R30    (scratch and load switcher SP, Z)
    SREG
    [disable switcher IRQs]
    R29 ... R28    (scratch and SP, Y)
    (RAMPZ)
    (RAMPY)
    [switch to switcher stack]
    [enable IRQs]
    R27 ... R18
    R1..R0         (zero and temp reg available)
    (RAMPX)
    (RAMPD)
    (EIND)

    [call SwitcherCore]
    
  Only saved when actually switching to other task:
    R16 ... R2
    
    [call SwitcherStackCheck]
*/

// expects:
//          register r17 is saved on stack
//          r17 is loaded with switching source
//          interrupts are disabled
void SwitcherImpl()
{                                                
  asm volatile ("push r31         \n\t"  // save Z, do not use as pointer reg before RAMPZ is saved!
                "push r30         \n\t"
                
                "in r31,__SREG__  \n\t"  // saving SREG
                "push r31         \n\t"
                );

  SWITCHER_ASM_DISABLE_SWITCHING_IRQS();

  asm volatile ("push r29         \n\t"  // save Y, do not use as pointer reg before RAMPY is saved!
                "push r28         \n\t"
                );

#ifdef RAMPZ  // save RAMPZ and clear it before using Z, uses r31
  asm volatile ("in r31,%[rampz]  \n\t"
                "push r31         \n\t"
                "clr r31          \n\t"
                "out %[rampz],r31 \n\t"
                :: [rampz] "I"(_SFR_IO_ADDR(RAMPZ)));
#endif

#ifdef RAMPY  // save RAMPY and clear it before using Y, uses r31
  asm volatile ("in r31,%[rampy]  \n\t"
                "push r31         \n\t"
                "clr r31          \n\t"
                "out %[rampy],r31 \n\t"
                :: [rampy] "I"(_SFR_IO_ADDR(RAMPY)));
#endif

  asm volatile ("in r28,__SP_L__           \n\t"  // load SP into Y
                "in r29,__SP_H__           \n\t"

                "ldi r31,hi8(%[swrStack])  \n\t"  // load switcher stack address in Z
                "ldi r30,lo8(%[swrStack])  \n\t"

                "out __SP_H__,r31          \n\t"  // store switcher stack address from Z to SP and ...
                "sei                       \n\t"  // ... enable interrupts
                "out __SP_L__,r30          \n\t"
                :: [swrStack] "i"(g_SwitcherStackData + (sizeof(g_SwitcherStackData) - 1)));

  asm volatile (// save rest of registers
                "st y,r27                  \n\t"  // no pre-decrement, SP points to first free slot on stack
                "st -y,r26                 \n\t"
                "st -y,r25                 \n\t"
                "st -y,r24                 \n\t"
                "st -y,r23                 \n\t"                
                "st -y,r22                 \n\t"
                "st -y,r21                 \n\t"
                "st -y,r20                 \n\t"
                "st -y,r19                 \n\t"
                "st -y,r18                 \n\t"                
                "st -y,r1                  \n\t"
                "st -y,r0                  \n\t"

                "clr __zero_reg__          \n\t"  // clear zero reg
                );

                // save and clear extension registers RAMPX, RAMPD and EIND if present
#ifdef RAMPX
  asm volatile ("in r0,%[rampx]             \n\t"
                "st -y,r0                   \n\t"
                "out %[rampx], __zero_reg__ \n\t"
                :: [rampx] "I"(_SFR_IO_ADDR(RAMPX)));
#endif

#ifdef RAMPD
  asm volatile ("in r0,%[rampd]             \n\t"
                "st -y,r0                   \n\t"
                "out %[rampd], __zero_reg__ \n\t"
                :: [rampd] "I"(_SFR_IO_ADDR(RAMPD)));
#endif

#ifdef EIND
  asm volatile ("in r0,%[eind]              \n\t"
                "st -y,r0                   \n\t"
                "out %[eind], __zero_reg__  \n\t"
                :: [eind] "I"(_SFR_IO_ADDR(EIND)));
#endif

  asm volatile ("mov r24,r17            \n\t"  // setup parameters for SwitcherCore, source in r24 ...
                
#ifdef  __AVR_HAVE_MOVW__
                "movw r22,r28           \n\t"  // ... current task's stack pointer in r22/r23
#else
                "mov r22,r28            \n\t"
                "mov r23,r29            \n\t"
#endif

                "call SwitcherCore      \n\t"  // call SwitcherCore function

                
                "cp r28,r22             \n\t"  // check if old and new stack pointer are equal
                "cpc r29,r23            \n\t"
                "breq skipfullswitch    \n\t"  // skip saving and restoring the remaining registers
                
                // save remaining full switch only registers
                "st -y,r16              \n\t"
                "st -y,r15              \n\t"
                "st -y,r14              \n\t"
                "st -y,r13              \n\t"
                "st -y,r12              \n\t"
                "st -y,r11              \n\t"
                "st -y,r10              \n\t"
                "st -y,r9               \n\t"
                "st -y,r8               \n\t"
                "st -y,r7               \n\t"
                "st -y,r6               \n\t"
                "st -y,r5               \n\t"
                "st -y,r4               \n\t"
                "st -y,r3               \n\t"
                "st -y,r2               \n\t"

                // TODO: call SwitcherStackCheck if stack check is enabled! ptr to prev task in r24+r25
                 
#ifdef  __AVR_HAVE_MOVW__
                "movw r28,r22           \n\t"  // move returned new stack pointer into Y
#else
                "mov r28,r24            \n\t"
                "mov r29,r25            \n\t"
#endif
                
                // restore full switch only registers
                "ld r2,y+               \n\t"
                "ld r3,y+               \n\t"
                "ld r4,y+               \n\t"
                "ld r5,y+               \n\t"
                "ld r6,y+               \n\t"
                "ld r7,y+               \n\t"
                "ld r8,y+               \n\t"
                "ld r9,y+               \n\t"
                "ld r10,y+              \n\t"
                "ld r11,y+              \n\t"
                "ld r12,y+              \n\t"
                "ld r13,y+              \n\t"
                "ld r14,y+              \n\t"
                "ld r15,y+              \n\t"
                "ld r16,y+              \n\t"
                
                "skipfullswitch:        \n\t"

                );
                
                // restore extension registers
#ifdef EIND
  asm volatile ("ld r0,y+       \n\t"
                "out %[eind],r0 \n\t"
                :: [eind] "I"(_SFR_IO_ADDR(EIND)));
#endif

#ifdef RAMPD
  asm volatile ("ld r0,y+        \n\t"
                "out %[rampd],r0 \n\t"
                :: [rampd] "I"(_SFR_IO_ADDR(RAMPD)));
#endif

#ifdef RAMPX
  asm volatile ("ld r0,y+        \n\t"
                "out %[rampx],r0 \n\t"
                :: [rampx] "I"(_SFR_IO_ADDR(RAMPX)));
#endif

                // restore bulk of registers
  asm volatile ("ld r0,y+                      \n\t"
                "ld r1,y+                      \n\t"                
                "ld r18,y+                     \n\t"
                "ld r19,y+                     \n\t"
                "ld r20,y+                     \n\t"
                "ld r21,y+                     \n\t"
                "ld r22,y+                     \n\t"
                "ld r23,y+                     \n\t"
                "ld r24,y+                     \n\t"
                "ld r25,y+                     \n\t"
                "ld r26,y+                     \n\t"
                "ld r27,y                      \n\t"  // no post-increment, SP always points to first free slot on stack

                "lds r30,%[currentTask]        \n\t"  // load address of current task into Z
                "lds r31,%[currentTask] + 1    \n\t"

                "cli                           \n\t"  // disable interrupts
                
                "out __SP_H__,r29              \n\t"  // load stack pointer from Y into SP
                "out __SP_L__,r28              \n\t"


                "ldd r29,z + %[counterOffset]  \n\t"  // load m_PauseSwitchingCounter into r29
                                
                "cpi r29,0                     \n\t"  // if counter is not 0 ...
                "brne skipEnableSwitchingIRQs  \n\t"  // ... jump to skipEnableSwitchingIRQs
                
                :: [currentTask] "i"(&g_CurrentTask), 
                   [counterOffset] "I"(offsetof(Task, m_PauseSwitchingCounter)));

                
                SWITCHER_ASM_ENABLE_SWITCHING_IRQS();  // re-enable switcher IRQs

  asm volatile ("skipEnableSwitchingIRQs:");

#ifdef RAMPY  // restore RAMPY
  asm volatile ("pop r30          \n\t"
                "out %[rampy],r30 \n\t"
                :: [rampy] "I"(_SFR_IO_ADDR(RAMPY)));
#endif

#ifdef RAMPZ  // restore RAMPZ
  asm volatile ("pop r30          \n\t"
                "out %[rampz],r30 \n\t"
                :: [rampz] "I"(_SFR_IO_ADDR(RAMPZ))));                
#endif

  asm volatile ("pop r28                  \n\t"  // restore Y
                "pop r29                  \n\t"

                "pop r31                  \n\t"  // SREG in r31, SREG_I is 0!
                
                "pop r30                  \n\t"  // restore R30
                
                "cpi r17,%[yielded]       \n\t"  // test if source is Yielded
                "breq ret_return          \n\t"  // branch to ret_return if equal

                "cpi r17,%[terminateTask] \n\t"  // test if source is TerminatingTask
                "breq ret_return          \n\t"  // branch to ret_return if equal

                "out __SREG__,r31         \n\t"  // restore SREG, must be after the branch because the branch changes SREG
                
                "pop r31                  \n\t"  // restore R31
                "pop r17                  \n\t"  // restore R17

                "reti                     \n\t"

              "ret_return:                \n\t"

                "out __SREG__,r31         \n\t"  // restore SREG, must be after the branch because the branch changes SREG

                "pop r31                  \n\t"  // restore R31
                "pop r17                  \n\t"  // restore R17

                "sei                      \n\t"  // we always return with interrupts enabled!

                "ret                      \n\t"
                :: [yielded] "i"(Yielded), [terminateTask] "i"(TerminatingTask));
}

// <stackPointer> is 16 bytes off for full state switch, 
// we always store the SP pointing to the last saved byte not the first free slot!
// this avoids incrementing and/or decrementing the SP during saving and restoring registers
SwitcherResult SwitcherCore(SwitchingSource source, void* stackPointer)
{
  SWITCHER_ASSERT(g_CurrentTask != NULL);
  SWITCHER_ASSERT(g_CurrentTask->m_pTaskListNext != NULL);
  
  SwitcherResult result = {.m_PreviousTask = g_CurrentTask};
  Task* nextTask = NULL;
  bool done = false;

  while (!done)
  {    
    if (source == SwitcherTick)
    {      
      Task* nextTaskCandidate = SwitcherTickCore();
            
      if (nextTaskCandidate != g_CurrentTask)
      {        
        if ((nextTask == NULL) || (nextTask->m_Priority < nextTaskCandidate->m_Priority))
        {
          nextTask = nextTaskCandidate;
        }          
      }
      else
      {
        if (nextTask == NULL)
        {
          nextTask = g_CurrentTask;
        }
      }
      
      SWITCHER_ASSERT(nextTask != NULL);
    }
    else  // source is either Yielded or PreemptiveSwitch or ForcedSwitch or TerminatingTask
    {
      // reset counter timer for preemptive task switches to start a new CPU slice
      ResetPreemptiveSwitchTimer();
      
      if (nextTask == NULL)
      {
        nextTask = &g_IdleTask;
      }

      Task* taskListIter = g_CurrentTask->m_pTaskListNext;
    
      // find next task == find first next none sleeping task with the highest priority
      do
      {
        SWITCHER_ASSERT(taskListIter != NULL);        
        SWITCHER_ASSERT(taskListIter->m_Priority >= taskListIter->m_BasePriority);
        
        SWITCHER_DISABLE_INTERRUPTS();
      
        if (taskListIter->m_SleepCount == 0)
        {
          if (nextTask->m_Priority < taskListIter->m_Priority)
          {
            nextTask = taskListIter;
          }
        }
      
        SWITCHER_ENABLE_INTERRUPTS();
      
        taskListIter = taskListIter->m_pTaskListNext;
      } while (taskListIter != g_CurrentTask->m_pTaskListNext);
    }

    SWITCHER_ASSERT(nextTask != NULL);
    
    // we already found the next task to execute, we just need to remove the current task from the task list
    if (source == TerminatingTask)
    {
      SWITCHER_ASSERT(g_CurrentTask->m_SleepCount == TimeoutInfinite);
      SWITCHER_ASSERT(nextTask != g_CurrentTask);
      SWITCHER_ASSERT(g_CurrentTask->m_pAcquiredList == NULL);
      
      // remove task from task list
      Task* taskListIter = g_CurrentTask->m_pTaskListNext;
      
      while (taskListIter->m_pTaskListNext != g_CurrentTask)
      {
        taskListIter = taskListIter->m_pTaskListNext;
      }
      
      SWITCHER_ASSERT(taskListIter->m_pTaskListNext == g_CurrentTask)
      
      taskListIter->m_pTaskListNext = g_CurrentTask->m_pTaskListNext;
      
      SWITCHER_ASSERT(g_Tasks > 0);
      
      g_Tasks--;
      
      // we need to exit here, g_CurrentTask is still pointing to the terminated task! -> endless loop in SwitcherTickCore()
      done = true;
      continue;
    }
    
    // check for pending switcher IRQs, avoid exit and immediate re-entry of switcher implementation
    if (IsSwitcherTickPending())
    {
      ResetSwitcherTickIrqFlag();
      source = SwitcherTick;      
    }
    else if (IsPreemptiveSwitchPending())
    {
      ResetPreemptiveSwitchIrqFlag();
      source = PreemptiveSwitch;
    }
    else if (IsForcedSwitchPending())
    {
      ResetForcedSwitchIrqFlag();
      source = ForcedSwitch;
    }
    else
    {
      done = true; 
    }
  }  // while

  SWITCHER_ASSERT(nextTask != NULL);
  
  if (nextTask != g_CurrentTask)
  {
    g_CurrentTask->m_pStackPointer = ((uint8_t*)stackPointer) - 15;
        
    g_CurrentTask = nextTask;
    
    result.m_NewSP = nextTask->m_pStackPointer;
  }
  else
  {
    result.m_NewSP = stackPointer;
  }    
    
  return result;
}

// returns newly awoke task that has a higher priority than the current task, 
// returns the current task if there is none
Task* SwitcherTickCore()
{
  // 64 bit arithmetic produces horrible code (especially within an ISR!)
  // doing it manually produces denser AND faster code ...
  g_TickCount.m_TickCountLow++;
  
  if (g_TickCount.m_TickCountLow == 0)
  {
    g_TickCount.m_TickCountHigh++;
  }
  
  Task* taskListIter = g_CurrentTask->m_pTaskListNext;
  Task* nextTask = g_CurrentTask;
  
  while (taskListIter != g_CurrentTask)
  {
    SWITCHER_DISABLE_INTERRUPTS();
    
    if ((taskListIter->m_SleepCount > 0) && (taskListIter->m_SleepCount < TimeoutInfinite))
    {
      taskListIter->m_SleepCount--;
      
      if (taskListIter->m_SleepCount == 0)
      {
        g_ActiveTasks++;        
        
        if (taskListIter->m_Priority > nextTask->m_Priority)
        {
          nextTask = taskListIter;
        }          
      }
    }
    
    SWITCHER_ENABLE_INTERRUPTS();

    taskListIter = taskListIter->m_pTaskListNext;
  }
    
  return nextTask; 
}

void TerminateTask()
{  
  SWITCHER_DISABLE_INTERRUPTS();
    
  g_CurrentTask->m_SleepCount = TimeoutInfinite;
  g_ActiveTasks--;
  
  PauseSwitching();  // make sure we do not lose the CPU as we are setup for infinite sleeping ...
  
  SWITCHER_ENABLE_INTERRUPTS();
 
  // iterate over all tasks waiting for us to terminate
  Task* waitingList = g_CurrentTask->m_pTaskWaitingList;
  
  while (waitingList != NULL)
  {
    SWITCHER_DISABLE_INTERRUPTS();
    
    if (waitingList->m_SleepCount > 0)
    {
      waitingList->m_SleepCount = 0;
      g_ActiveTasks++;      
    }
    
    SWITCHER_ENABLE_INTERRUPTS();
    
    waitingList = waitingList->m_pWaitingListNext;
  }
  
  cli();
  
  TerminateTaskImpl();  // TODO: generates rcall, expected rjmp !?!?!?!?
}

void Sleep(Timeout timeout)
{
  if (timeout > TimeoutNone)
  {
    PauseSwitching();
    
    SWITCHER_DISABLE_INTERRUPTS();
  
    g_CurrentTask->m_SleepCount = timeout;
    
    SWITCHER_ENABLE_INTERRUPTS();
    
    Yield();    
    
    ResumeSwitching();
  }
  else
  {
    Yield();
  }    
}

void Yield()
{
  SWITCHER_DISABLE_INTERRUPTS();  
  
  if (g_CurrentTask != &g_IdleTask)
  {
    if (g_CurrentTask->m_SleepCount < TimeoutMaximum)
    {
      g_CurrentTask->m_SleepCount++;
    }
  
    SWITCHER_ASSERT(g_ActiveTasks > 0);
    
    g_ActiveTasks--;
  }
  else
  {
    g_CurrentTask->m_SleepCount = 0;  // the idle task never really sleeps and is not accounted for in g_ActiveTasks counter!
  }
    
  asm volatile ("call YieldImpl" ::: "memory");  // saves two instructions, push/pop
  //YieldImpl();

  SWITCHER_ENABLE_INTERRUPTS();
}

void PauseSwitchingImpl()
{
  asm volatile ("lds r30,%[currentTask]        \n\t"  // load address of current task into Z
                "lds r31,%[currentTask] + 1    \n\t"

                "in r18,__SREG__               \n\t"  // saving SREG
                "cli                           \n\t"  // disable interrupts
                                 
                "ldd r19,z + %[counterOffset]  \n\t"  // load m_PauseSwitchingCounter into r19

#ifdef DEBUG
                // pause switching counter must not be at max value (x0ff)
                "cpi r19, 0xff                 \n\t"
                "brne notmax                   \n\t"
                "break                         \n\t"
                "notmax:                       \n\t"

#endif

                "inc r19                       \n\t"  // increment m_PauseSwitchingCounter
                
                "std z + %[counterOffset],r19  \n\t"  // save m_PauseSwitchingCounter
                
                "cpi r19,1                     \n\t"  // if counter did not change from 0 to 1 ...
                "brne pauseDone                \n\t"  // ... jump to pauseDone
                :: [currentTask] "i"(&g_CurrentTask), 
                   [counterOffset] "I"(offsetof(Task, m_PauseSwitchingCounter)));
                
  SWITCHER_ASM_DISABLE_SWITCHING_IRQS();

  asm volatile ("pauseDone:        \n\t"
                
                "out __SREG__,r18  \n\t"              // restore SREG
                                
                "ret               \n\t"
                );
}

void ResumeSwitchingImpl()
{
  asm volatile ("lds r30,%[currentTask]        \n\t"  // load address of current task into Z
                "lds r31,%[currentTask] + 1    \n\t"

                "in r18,__SREG__               \n\t"  // saving SREG
                "cli                           \n\t"  // disable interrupts

                "ldd r19,z + %[counterOffset]  \n\t"  // load m_PauseSwitchingCounter into r19

#ifdef DEBUG
                // pause switching counter must not be zero
                "cp r19, __zero_reg__          \n\t"
                "brne notzero                  \n\t"
                "break                         \n\t"
                "notzero:                      \n\t"
#endif

                "dec r19                       \n\t"  // decrement m_PauseSwitchingCounter
                
                "std z + %[counterOffset],r19  \n\t"  // save m_PauseSwitchingCounter
                
                "cpi r19,0                     \n\t"  // if counter did not change from 1 to 0 ...
                "brne resumeDone               \n\t"  // ... jump to resumeDone
                :: [currentTask] "i"(&g_CurrentTask),
                   [counterOffset] "I"(offsetof(Task, m_PauseSwitchingCounter)));
  
  SWITCHER_ASM_ENABLE_SWITCHING_IRQS();
  
  asm volatile ("resumeDone:       \n\t"
  
                "out __SREG__,r18  \n\t"              // restore SREG

                "ret               \n\t"
                );
}

static void IdleTask(void* param)
{
  (void)param;
  
  SWITCHER_ASSERT(param == NULL);
  SWITCHER_ASSERT(g_CurrentTask->m_Priority == PriorityIdle);  
  SWITCHER_ASSERT(g_CurrentTask->m_BasePriority == PriorityIdle);
  
  while (true)
  {
    set_sleep_mode(SLEEP_MODE_IDLE);
    
    cli();
    
    if (g_ActiveTasks == 0)
    {
      sleep_enable();
      sei();
//      sleep_cpu();  // TODO: hmmm sleep seems to crash the simulator in Atmel Studio 7.0.2397 ...
      sleep_disable();
    }
    else
    {
      sei();
    }
    
    Yield();
  }
}

SwitcherError Initialize(Task* mainTask)
{
  if (mainTask == NULL)
  {
    return SwitcherInvalidParameter;
  }

  // add main task
  memset(mainTask, 0x00, sizeof(*mainTask));
  
  mainTask->m_BasePriority = PriorityNormal;
  mainTask->m_Priority = PriorityNormal;
  mainTask->m_pTaskListNext = mainTask;
  
  SWITCHER_DISABLE_INTERRUPTS();  
        
  g_CurrentTask = mainTask;
  
  g_Tasks = 1;
  g_ActiveTasks = 1;
  
  PauseSwitching();  // switcher IRQs should be disabled here anyways ...
  
  SWITCHER_ENABLE_INTERRUPTS();

  // add idle task
  AddTask(&g_IdleTask, g_IdleTaskStackBuffer, sizeof(g_IdleTaskStackBuffer), &IdleTask, NULL, PriorityIdle);

  ResumeSwitching();  // initially enables switcher IRQs
  
  return SwitcherNoError;
}

SwitcherError AddTask(Task* task, 
                      void* stackBuffer, size_t stackSize, 
                      TaskFunction taskFunction, void* taskParameter, 
                      Priority priority)
{
  // initial task state is setup to allow starting a new task by adding it to the task list and start it
  // by simply have the task switcher switch to it
  //
  // initial task state contains a full task state where the all registers are 0 and the return address points to
  // the TaskStartup function, additionally the task functions address and its parameter are on the stack
  typedef struct  
  {
    uint8_t                    m_Registers[SWITCHER_TASK_STATE_SIZE - SWITCHER_RETURN_ADDR_SIZE];

    static_assert(sizeof(TaskStartupFunctionPointer) == 2, "sizeof(TaskStartupFunctionPointer) must be 2");
    static_assert(sizeof(TaskStartupFunctionPointer) <= SWITCHER_RETURN_ADDR_SIZE, "sizeof(TaskStartupFunctionPointer) must be <= than SWITCHER_RETURN_ADDR_SIZE");
    
#if SWITCHER_RETURN_ADDR_SIZE > 2
    uint8_t                    m_ReturnAddressPadding[SWITCHER_RETURN_ADDR_SIZE - sizeof(TaskStartupFunctionPointer)];
#endif
    
    TaskStartupFunctionPointer m_ReturnAddress;  // return address from initial "task switch" to task startup function
  } InitialTaskState;
  
  SWITCHER_ASSERT(stackSize >= SWITCHER_TASK_STATE_MIN_STACK_SIZE);
  static_assert(sizeof(InitialTaskState) == SWITCHER_TASK_STATE_MIN_STACK_SIZE, "sizeof(InitialTaskState) must be equal to SWITCHER_TASK_STATE_MIN_STACK_SIZE");   

  if (g_CurrentTask == NULL)
  {
    return SwitcherNotInitialized;
  }
  
  const bool isIdleTask = (task == &g_IdleTask);
  
  if ((task == NULL) ||
      (stackBuffer == NULL) ||
      (taskFunction == NULL) ||
      ((priority < PriorityLowest) && !isIdleTask) ||
      (stackSize < sizeof(InitialTaskState)))  // TODO: check for better minimal stack size
  {
    return SwitcherInvalidParameter;
  }
  
  if (g_Tasks == MaxNumberOfTasks)
  {
    return SwitcherTooManyTasks;
  }
  
  InitialTaskState* pInitialTaskState = ((InitialTaskState*)((uint8_t*)stackBuffer) + ((stackSize - 1) - (sizeof(InitialTaskState) - 1)));
    
  memset(pInitialTaskState, 0x00, sizeof(InitialTaskState));
  
  // we need to byte swap the return address!
  pInitialTaskState->m_ReturnAddress = (TaskStartupFunctionPointer) ((((uintptr_t) &TaskStartup) >> 8) | (((uintptr_t) &TaskStartup) << 8));
  
  // place pointer to task function and task parameter in register according to ABI
  // -> task function in R24:R25, R24 is at offset (sizeof(m_Registers) - 1) - (9 + SWITCHER_RAMPZ_SIZE + SWITCHER_RAMPY_SIZE), R25 at offset (8 + SWITCHER_RAMPZ_SIZE + SWITCHER_RAMPY_SIZE)
  // -> task parameter in R22:R23, R22 is at offset (sizeof(m_Registers) - 1) - (11 + SWITCHER_RAMPZ_SIZE + SWITCHER_RAMPY_SIZE), R23 at offset (10 + SWITCHER_RAMPZ_SIZE + SWITCHER_RAMPY_SIZE)  
  pInitialTaskState->m_Registers[(sizeof(pInitialTaskState->m_Registers) - 1) - (9 + SWITCHER_RAMPZ_SIZE + SWITCHER_RAMPY_SIZE)] = (uint8_t)((uintptr_t)taskFunction);       // R24
  pInitialTaskState->m_Registers[(sizeof(pInitialTaskState->m_Registers) - 1) - (8 + SWITCHER_RAMPZ_SIZE + SWITCHER_RAMPY_SIZE)] = (uint8_t)((uintptr_t)taskFunction >> 8);  // R25
  
  pInitialTaskState->m_Registers[(sizeof(pInitialTaskState->m_Registers) - 1) - (11 + SWITCHER_RAMPZ_SIZE + SWITCHER_RAMPY_SIZE)] = (uint8_t)((uintptr_t)taskParameter);      // R22
  pInitialTaskState->m_Registers[(sizeof(pInitialTaskState->m_Registers) - 1) - (10 + SWITCHER_RAMPZ_SIZE + SWITCHER_RAMPY_SIZE)] = (uint8_t)((uintptr_t)taskParameter >> 8); // R23
  
  task->m_pStackPointer = pInitialTaskState;  // we always store the SP pointing to the last saved byte!
  task->m_BasePriority = priority;
  task->m_Priority = priority;
  
#if 0  // for stack check only 
  task->m_StackBuffer = stackBuffer;
  task->m_StackSize = stackSize;
#endif

  task->m_pWaitingListNext = NULL;
  task->m_pTaskWaitingList = NULL;
  task->m_pAcquiredList = NULL;
  task->m_pIsWaitingFor = NULL;
  task->m_SleepCount = 0;
  task->m_PauseSwitchingCounter = 0;

  PauseSwitching();
  
  task->m_pTaskListNext = g_CurrentTask->m_pTaskListNext;
  g_CurrentTask->m_pTaskListNext = task;
  
  g_Tasks++;
  
  if (!isIdleTask)  // idle task does not count as active task
  {
    SWITCHER_DISABLE_INTERRUPTS();
    g_ActiveTasks++;
    SWITCHER_ENABLE_INTERRUPTS();
  }

  // give away the CPU if the new task has higher priority than we have
  if (priority > g_CurrentTask->m_Priority)
  {
    Yield();
  }
  
  ResumeSwitching();

  return SwitcherNoError;
}

void TaskStartup(TaskFunction taskFunction, void* taskParameter)
{
  SWITCHER_ASSERT(g_CurrentTask->m_pIsWaitingFor == NULL);
  SWITCHER_ASSERT(g_CurrentTask->m_pAcquiredList == NULL);
  SWITCHER_ASSERT(g_CurrentTask->m_pWaitingListNext == NULL);
  SWITCHER_ASSERT(g_CurrentTask->m_SleepCount == 0);
  
  taskFunction(taskParameter);  // call task function

  SWITCHER_ASSERT(g_CurrentTask->m_pIsWaitingFor == NULL);
  SWITCHER_ASSERT(g_CurrentTask->m_pAcquiredList == NULL);
  SWITCHER_ASSERT(g_CurrentTask->m_pWaitingListNext == NULL);
  SWITCHER_ASSERT(g_CurrentTask->m_SleepCount == 0);
  
  TerminateTask();
    
  // WE SHOULD NEVER EVER REACH THIS POINT!
  SWITCHER_ASSERT(true == false);
}

SwitcherError JoinTask(Task* task, Timeout timeout)
{
  if (g_CurrentTask == NULL)
  {
    return SwitcherNotInitialized;
  }
  
  PauseSwitching();

  SWITCHER_ASSERT(g_CurrentTask->m_pIsWaitingFor == NULL);
     
  if (task == g_CurrentTask)
  {
    ResumeSwitching();
      
    return SwitcherInvalidParameter;
  }
    
  if (IsKnownTask(task))
  {    
    // add us to the tasks waiting list
    g_CurrentTask->m_pWaitingListNext = task->m_pTaskWaitingList;
    task->m_pTaskWaitingList = g_CurrentTask;
    
    Sleep(timeout);
    
    if (IsKnownTask(task))
    {
      ResumeSwitching();
      
      return SwitcherTimeout;
    }
  }
  
  ResumeSwitching();
  
  return SwitcherNoError;
}

TickCount GetSwitcherTickCount()
{
  TickCount count;
  
  SWITCHER_DISABLE_INTERRUPTS();
  
  count = g_TickCount;
  
  SWITCHER_ENABLE_INTERRUPTS();
  
  return count;
}

bool IsKnownTask(const Task* task)
{
  bool found = false;
  
  PauseSwitching();
  
  Task* taskListIter = g_CurrentTask;
  
  do 
  {
    if (taskListIter == task)
    {
      found = true;
      break;
    }
    
    taskListIter = taskListIter->m_pTaskListNext;
  } while (taskListIter != g_CurrentTask);
  
  ResumeSwitching();
  
  return found;
}
