/*
 * Test_ATMEGA328PB.c
 *
 * Created: 15.02.2019 11:36:55
 * Author : Berti
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>

#include "Switcher.h"
#include "SwitcherTest.h"

static void InitHardware()
{
  // set power reduction registers
  // disable TWI0, USART1, SPI0 and ADC
  PRR0 = (1 << PRTWI0) | (1 << PRUSART1) | (1 << PRSPI0) | (1 << PRADC);
  // disable TWI1, PTC and SPI1
  PRR1 = (1 << PRTWI1) | (1 << PRPTC) | (1 << PRSPI1);
  
  // init timer TC2 for switcher tick and preemptive task switch
  // CTC mode, systemCLK/256, TOP = 39, enable OCIE2A and OCIE2B IRQs
  // OCR2A has a fixed value to generate a monotone switcher tick IRQ
  // OCR2B has a variable value (0 <= OCR2B <= OCR2A), 
  //   is used for preemptive task switching, 
  //   worst case CPU slice length is (1 / (20Mhz / 256)) * 38 = 486,4 us (12,8 us short of switcher tick)
  // @20Mhz => 499,2 us between switcher ticks (OCR2A)
  TCCR2A = (1 << WGM21);  
  OCR2A = 39;
  OCR2B = 39;
  TCNT2 = 0;
  //TIMSK2 = (1 << OCIE2A) | (1 << OCIE2B);  enable IRQs during switcher initialization
  TCCR2B = (1 << CS21) | (1 << CS22);
  
  // setup PCI3 as forced switching IRQ, pin PE1
  DDRE = (1 << DDRE1);
  PCICR = (1 << PCIE3);
  //PCMSK3 = (1 << PCINT25);  enable IRQs during switcher initialization
}

Task g_MainTask;

int main(void)
{
    sei();    
    InitHardware();
    
    Initialize(&g_MainTask);
    
    while (TRUE) 
    {      
      SwitcherTestSuite();
      Sleep(1);
    }
}


