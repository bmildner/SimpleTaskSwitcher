/*
 * SwitcherTest.c
 *
 * Created: 16.03.2019 13:23:11
 *  Author: Berti
 */ 

#include "SwitcherTest.h"

#include "SwitcherConfig.h"


#define __STRINGIFY(x) #x
#define TO_STRING(x) __STRINGIFY(x)


static uint8_t testStack[SWITCHER_TASK_STATE_MIN_STACK_SIZE * 2];  // TODO: correctly size test stack

typedef void (*TrampolinFunction)();


__attribute__((naked))
static void PreservesFullState(TrampolinFunction function);

__attribute__((naked))
static void AdheresToABI(TrampolinFunction function);

static void PreservesSREG_I(TrampolinFunction function);

static void EnablesSREG_I(TrampolinFunction function);

static Bool AreSwitcherIRQsEnabled();

#define ASM_BREAK_LOOP_IF_NOT_EQUAL           \
                "breq .+4              \n\t"  \
                "break                 \n\t"  \
                "rjmp .-4              \n\t"

// Checks that the called function does not alter any CPU registers or
// SREG, only SREG_I is ignored to allow testing of ISRs.
void PreservesFullState(TrampolinFunction function)
{
  asm volatile (/* save registers according to ABI */
                "push r1               \n\t"
                "push r2               \n\t"
                "push r3               \n\t"
                "push r4               \n\t"
                "push r5               \n\t"
                "push r6               \n\t"
                "push r7               \n\t"
                "push r8               \n\t"
                "push r9               \n\t"
                "push r10              \n\t"
                "push r11              \n\t"
                "push r12              \n\t"
                "push r13              \n\t"
                "push r14              \n\t"
                "push r15              \n\t"
                "push r16              \n\t"
                "push r17              \n\t"
                "push r28              \n\t"
                "push r29              \n\t"

                /* save SREG */
                "in r16, __SREG__      \n\t"
                "push r16              \n\t"

                "movw r30,r24          \n\t"  // move function ptr argument into Z
                
                "push r30              \n\t"  // save function ptr on stack
                "push r31              \n\t"

                /* setup SREG */
                "ldi r16,%[sreg_value] \n\t"
                "out __SREG__,r16      \n\t"

                /* setup registers */
                "ldi r16,0xab          \n\t"
                "ldi r17,0xcd          \n\t"
                "ldi r18,2             \n\t"
                "ldi r19,3             \n\t"
                "ldi r20,4             \n\t"
                "ldi r21,5             \n\t"
                "ldi r22,6             \n\t"
                "ldi r23,7             \n\t"
                "ldi r24,8             \n\t"
                "ldi r25,9             \n\t"
                "ldi r26,10            \n\t"
                "ldi r27,11            \n\t"
                "ldi r28,12            \n\t"
                "ldi r29,13            \n\t"
                
                "movw r0,r16           \n\t"
                "movw r2,r18           \n\t"
                "movw r4,r20           \n\t"
                "movw r6,r22           \n\t"
                "movw r8,r24           \n\t"
                "movw r10,r26          \n\t"
                "movw r12,r28          \n\t"
                
                "ldi r16,14            \n\t"
                "ldi r17,15            \n\t"
                
                "movw r14,r16          \n\t"
                
                "ldi r16,16            \n\t"
                "ldi r17,17            \n\t"
                "ldi r18,18            \n\t"
                "ldi r19,19            \n\t"
                "ldi r20,20            \n\t"
                "ldi r21,21            \n\t"
                "ldi r22,22            \n\t"
                "ldi r23,23            \n\t"
                "ldi r24,24            \n\t"
                "ldi r25,25            \n\t"
                "ldi r26,26            \n\t"
                "ldi r27,27            \n\t"
                "ldi r28,28            \n\t"
                "ldi r29,29            \n\t"
                
                "icall                 \n\t"  // call function under test

                "cli                   \n\t"  // disable IRQs

                /* save r16 and SREG on stack for later check */
                "push r16              \n\t"
                "in r16,__SREG__       \n\t"
                "push r16              \n\t"
                                            
                "cpi r17,17            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL                
                "cpi r18,18            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL                
                "cpi r19,19            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r20,20            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r21,21            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r22,22            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r23,23            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r24,24            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r25,25            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r26,26            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r27,27            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r28,28            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r29,29            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                "movw r16,r0           \n\t"
                "movw r18,r2           \n\t"
                "movw r20,r4           \n\t"
                "movw r22,r6           \n\t"
                "movw r24,r8           \n\t"
                "movw r26,r10          \n\t"
                "movw r28,r12          \n\t"

                "cpi r16,0xab          \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r17,0xcd          \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r18,2             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r19,3             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r20,4             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r21,5             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r22,6             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r23,7             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r24,8             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r25,9             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r26,10            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r27,11            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r28,12            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r29,13            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                "movw r16,r14          \n\t"
                
                "cpi r16,14            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r17,15            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                /* restore new SREG in r17 and r16 in r16 */
                "pop r17               \n\t"
                "pop r16               \n\t"
                
                /* check r16 */
                "cpi r16,16            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                /* check SREG */
                "cpi r17,%[sreg_value] \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                // restore function ptr in r16,r17
                "pop r17               \n\t"
                "pop r16               \n\t"
                
                // compare Z with function ptr in r16,r17
                "cp r30,r16            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cp r31,r17            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                /* restore SREG */
                "pop r16               \n\t"
                "out __SREG__, r16     \n\t"

                /* restore registers according to ABI */
                "pop r29               \n\t"
                "pop r28               \n\t"
                "pop r17               \n\t"
                "pop r16               \n\t"
                "pop r15               \n\t"
                "pop r14               \n\t"
                "pop r13               \n\t"
                "pop r12               \n\t"
                "pop r11               \n\t"
                "pop r10               \n\t"
                "pop r9                \n\t"
                "pop r8                \n\t"
                "pop r7                \n\t"
                "pop r6                \n\t"
                "pop r5                \n\t"
                "pop r4                \n\t"
                "pop r3                \n\t"
                "pop r2                \n\t"
                "pop r1                \n\t"

                "ret                   \n\t"
                ::[sreg_value] "i"(~(1 << SREG_I)));    
}

void AdheresToABI(TrampolinFunction function)
{
  asm volatile (// save registers according to ABI
                "push r1               \n\t"
                "push r2               \n\t"
                "push r3               \n\t"
                "push r4               \n\t"
                "push r5               \n\t"
                "push r6               \n\t"
                "push r7               \n\t"
                "push r8               \n\t"
                "push r9               \n\t"
                "push r10              \n\t"
                "push r11              \n\t"
                "push r12              \n\t"
                "push r13              \n\t"
                "push r14              \n\t"
                "push r15              \n\t"
                "push r16              \n\t"
                "push r17              \n\t"
                "push r28              \n\t"
                "push r29              \n\t"
                
                // fill call saved registers with test data
                "ldi r28,19            \n\t"
                "ldi r29,17            \n\t"
                
                "mov r1, r28           \n\t"
                "subi r28,1            \n\t"
                
                "movw r2, r28          \n\t"
                "subi r28,2            \n\t"
                "subi r29,2            \n\t"
                "movw r4, r28          \n\t"
                "subi r28,2            \n\t"
                "subi r29,2            \n\t"
                "movw r6, r28          \n\t"
                "subi r28,2            \n\t"
                "subi r29,2            \n\t"
                "movw r8, r28          \n\t"
                "subi r28,2            \n\t"
                "subi r29,2            \n\t"
                "movw r10, r28         \n\t"
                "subi r28,2            \n\t"
                "subi r29,2            \n\t"
                "movw r12, r28         \n\t"
                "subi r28,2            \n\t"
                "subi r29,2            \n\t"
                "movw r14, r28         \n\t"
                "ldi r16,4             \n\t"
                "ldi r17,3             \n\t"
                "ldi r28,2             \n\t"
                "ldi r29,1             \n\t"
                
                "movw r30,r24           \n\t"  // copy function param into Z
                
                "icall                 \n\t"  // call function
                
                // check register
                "cpi r29,1             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r28,2             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r17,3             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r16,4             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
  
                "movw r16,r14          \n\t"
                "cpi r17,5             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r16,6             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                "movw r16,r12          \n\t"
                "cpi r17,7             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r16,8             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                
                "movw r16,r10          \n\t"
                "cpi r17,9             \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r16,10            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                "movw r16,r8           \n\t"
                "cpi r17,11            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r16,12            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                "movw r16,r6           \n\t"
                "cpi r17,13            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r16,14            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                "movw r16,r4           \n\t"
                "cpi r17,15            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r16,16            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                "movw r16,r2           \n\t"
                "cpi r17,17            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                "cpi r16,18            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL

                "mov r17,r1            \n\t"
                "cpi r17,19            \n\t"
                ASM_BREAK_LOOP_IF_NOT_EQUAL
                
                // restore registers according to ABI
                "pop r29               \n\t"
                "pop r28               \n\t"
                "pop r17               \n\t"
                "pop r16               \n\t"
                "pop r15               \n\t"
                "pop r14               \n\t"
                "pop r13               \n\t"
                "pop r12               \n\t"
                "pop r11               \n\t"
                "pop r10               \n\t"
                "pop r9                \n\t"
                "pop r8                \n\t"
                "pop r7                \n\t"
                "pop r6                \n\t"
                "pop r5                \n\t"
                "pop r4                \n\t"
                "pop r3                \n\t"
                "pop r2                \n\t"
                "pop r1                \n\t"
                  
                "ret                   \n\t"
                );
}

void PreservesSREG_I(TrampolinFunction function)
{
  uint8_t sreg = SREG;
    
  sei();
  
  function();

  if (bit_is_clear(SREG, SREG_I))
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }

  cli();
  
  function();
  
  if (bit_is_set(SREG, SREG_I))
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }

  SREG = sreg;
}

void EnablesSREG_I(TrampolinFunction function)
{
  uint8_t sreg = SREG;
  
  sei();
  
  function();

  if (bit_is_clear(SREG, SREG_I))
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }

  cli();
  
  function();
  
  if (bit_is_clear(SREG, SREG_I))
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }

  SREG = sreg;
}

Bool AreSwitcherIRQsEnabled()
{
  if (((TIMSK2 & ((1 << OCIE2A) | (1 << OCIE2B))) != ((1 << OCIE2A) | (1 << OCIE2B))) ||
      ((PCMSK3 & (1 << PCINT25)) != (1 << PCINT25)))
  {
    return FALSE;
  } 
  
  return TRUE;
}  

// ********************** Tests for Tests **********************

static void DummyFunction()
{}

static void PreservesFullStateCallsDummyFkt()
{
  PreservesFullState(&DummyFunction);
}

static void AdheresToABICallsDummyFkt()
{
  AdheresToABI(&DummyFunction);
}

static void TestTestSupport()
{
  AdheresToABI(&PreservesFullStateCallsDummyFkt);
  AdheresToABI(&AdheresToABICallsDummyFkt);
}

// ********************** Yield **********************

static void YieldTest()
{
  AdheresToABI(&Yield);

  PreservesSREG_I(&Yield);
}

// ********************** SWITCHER_PREEMPTIVE_SWITCH_VECTOR **********************

__attribute__((naked))
static void SwitcherPreemptiveSwitchVectorTrampolin()
{
  asm volatile ("jmp " TO_STRING(SWITCHER_PREEMPTIVE_SWITCH_VECTOR));
}

static void PreemptiveSwitchISRTest()
{
  PauseSwitching();  // avoid parallel vector entry -> trashed switcher stack!
  
  PreservesFullState(&SwitcherPreemptiveSwitchVectorTrampolin);
  EnablesSREG_I(&SwitcherPreemptiveSwitchVectorTrampolin);
  
  ResumeSwitching();
}

// ********************** SWITCHER_FORCED_SWITCH_VECTOR **********************

__attribute__((naked))
static void SwitcherForcedSwitchVectorTrampolin()
{
  asm volatile ("jmp " TO_STRING(SWITCHER_FORCED_SWITCH_VECTOR));
}

void ForcedSwitchISRTest()
{
  PauseSwitching();  // avoid parallel vector entry -> trashed switcher stack!
  
  PreservesFullState(&SwitcherForcedSwitchVectorTrampolin);
  EnablesSREG_I(&SwitcherForcedSwitchVectorTrampolin);
  
  ResumeSwitching();
}

// ********************** SWITCHER_TICK_VECTOR **********************

__attribute__((naked))
static void SwitcherTickVectorTrampolin()
{
  asm volatile ("jmp " TO_STRING(SWITCHER_TICK_VECTOR));
}

static void SwitcherTickISRTest()
{
  PauseSwitching();  // avoid parallel vector entry -> trashed switcher stack!
  
  PreservesFullState(&SwitcherTickVectorTrampolin);
  EnablesSREG_I(&SwitcherTickVectorTrampolin);
  
  ResumeSwitching();
}

// ********************** PauseSwitching **********************

__attribute__((naked))
static void PauseSwitchingTrampolin()
{
  PauseSwitching();
  
  asm volatile ("ret");
}

// TODO: combine Pause/ResumeSwitching test ...
static void PauseSwitchingTest_OK()
{
  // force switcher IRQs to on
  //TIMSK2 |= (1 << OCIE2A) | (1 << OCIE2B);
  //PCMSK3 |= (1 << PCINT25);
  
  // we expect the switcher IRQs to be enabled when called, otherwise test will not work
  if (!AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }
  
  PauseSwitching();
  
  if (AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }
  
  PauseSwitching();

  if (AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }
  
  ResumeSwitching();  

  if (AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }

  ResumeSwitching();
  
  if (!AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }  
}

static void PauseSwitchingTest()
{
  PauseSwitchingTest_OK();
  
  PreservesSREG_I(&PauseSwitchingTrampolin);
  ResumeSwitching();
  ResumeSwitching();
}

// ********************** ResumeSwitching **********************

__attribute__((naked))
static void ResumeSwitchingTrampolin()
{
  ResumeSwitching();
  
  asm volatile ("ret");
}

static void ResumeSwitchingTest_OK()
{
  // we expect switcher IRQs to be enabled initially, otherwise test will fail
  if (!AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }
  
  PauseSwitching();
  
  // force switcher IRQs to disable
  //TIMSK2 &= ~((1 << OCIE2A) | (1 << OCIE2B));
  //PCMSK3 &= ~(1 << PCINT25);

  if (AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }
  
  ResumeSwitching();

  if (!AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }
  
  PauseSwitching();
  
  if (AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }
  
  PauseSwitching();

  if (AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }
  
  ResumeSwitching();
  
  if (AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }

  ResumeSwitching();

  if (!AreSwitcherIRQsEnabled())
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }  
}

static void ResumeSwitchingTest()
{
  ResumeSwitchingTest_OK();
  
  PauseSwitching();
  PauseSwitching();
  PreservesSREG_I(&ResumeSwitchingTrampolin);
}

// ********************** AddTask **********************

static Task g_TestTask;

static void EmptyTestTaskFunction(void* param)
{
  (void)param;
}

static void AddTaskTrumpolin()
{  
  if (AddTask(&g_TestTask, testStack, sizeof(testStack), &EmptyTestTaskFunction, NULL, PriorityNormal) != SwitcherNoError)
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }
  
  if (JoinTask(&g_TestTask, TimeoutInfinite) != SwitcherNoError)
  {
    while (TRUE)
    {
      asm volatile ("break");
    }
  }  
}

static void AddTaskTest()
{
  if (AddTask(&g_TestTask, testStack, sizeof(testStack), &EmptyTestTaskFunction, NULL, PriorityNormal) != SwitcherNoError)
  {
    while (TRUE)
    {
      asm volatile ("break"); 
    }
  }
  
  if (JoinTask(&g_TestTask, TimeoutInfinite) != SwitcherNoError)
  {
    while (TRUE)
    {
      asm volatile ("break");
    }    
  }
  
  PreservesSREG_I(&AddTaskTrumpolin);
  
  AdheresToABI(&AddTaskTrumpolin);
}

// ********************** SwitcherTestSuite **********************

void SwitcherTestSuite()
{
  TestTestSupport();
  
  SwitcherTickISRTest();
  PreemptiveSwitchISRTest();
  ForcedSwitchISRTest();
      
  YieldTest();
      
  PauseSwitchingTest();
  ResumeSwitchingTest();
      
  AddTaskTest();
}