/*
 * SwitcherConfig.h
 *
 * Created: 07.03.2019 12:23:28
 *  Author: Berti
 */ 

#ifndef SWITCHERCONFIG_H_
#define SWITCHERCONFIG_H_

#include <avr/io.h>
#include <avr/interrupt.h>

#include "Switcher.h"

// software IRQ that can be used to trigger a task switch from an other IRQ 
#define SWITCHER_FORCED_SWITCH_VECTOR     PCINT3_vect

// switcher tick IRQ used for monotone switcher tick count and timeouts
#define SWITCHER_TICK_VECTOR              TIMER2_COMPA_vect

// IRQ for preemptive task switch
#define SWITCHER_PREEMPTIVE_SWITCH_VECTOR TIMER2_COMPB_vect


// Inline ASM block that disables all three switcher IRQs
//   may use r30, r31 and SREG
#define SWITCHER_ASM_DISABLE_SWITCHING_IRQS()                                                                 \
  asm volatile ("lds r30,%[timsk2]       \n\t"                                                                \
                "andi r30,%[timsk2_mask] \n\t"                                                                \
                "sts %[timsk2],r30       \n\t"                                                                \
                "                        \n\t"                                                                \
                "lds r30,%[pcmsk3]       \n\t"                                                                \
                "andi r30,%[pcmsk3_mask] \n\t"                                                                \
                "sts %[pcmsk3],r30"                                                                           \
                :: [timsk2] "i"(_SFR_MEM_ADDR(TIMSK2)), [timsk2_mask] "i"(~((1 << OCIE2A) | (1 << OCIE2B))),  \
                   [pcmsk3] "i"(_SFR_MEM_ADDR(PCMSK3)), [pcmsk3_mask]"i"(~(1 << PCINT25)))

// Inline ASM block that enables all three switcher IRQs
//   may use r30, r31 and SREG
#define SWITCHER_ASM_ENABLE_SWITCHING_IRQS()                                                               \
  asm volatile ("lds r30,%[timsk2]      \n\t"                                                              \
                "ori r30,%[timsk2_mask] \n\t"                                                              \
                "sts %[timsk2],r30      \n\t"                                                              \
                "                       \n\t"                                                              \
                "lds r30,%[pcmsk3]      \n\t"                                                              \
                "ori r30,%[pcmsk3_mask] \n\t"                                                              \
                "sts %[pcmsk3],r30"                                                                        \
                :: [timsk2] "i"(_SFR_MEM_ADDR(TIMSK2)), [timsk2_mask] "i"((1 << OCIE2A) | (1 << OCIE2B)),  \
                   [pcmsk3] "i"(_SFR_MEM_ADDR(PCMSK3)), [pcmsk3_mask] "i"(1 << PCINT25))

// Resets preemptive switching timer interval (set compare register to current counter value) and
// resets preemptive switching interrupt flag if set
__attribute__((always_inline))
static inline void ResetPreemptiveSwitchTimer()
{
    OCR2B = TCNT2;
    
    // reset interrupt flag if set
    if (bit_is_set(TIFR2, OCF2B))
    {
      TIFR2 |= (1 << OCF2B);  // This will only generate valid code IF TIFR2 is in the lower I/O register range!!!
      //TIFR2 = (1 << OCF2B);
    }
}

#endif /* SWITCHERCONFIG_H_ */
