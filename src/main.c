/*
 * ==============================================================================
 * AVRTOS - Advanced AVR Real-Time Operating System & x86-like Virtual Machine
 * ==============================================================================
 * 
 * Copyright (C) 2026 Mohammed Faisal
 *
 * AVRTOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * AVRTOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with AVRTOS. If not, see <https://www.gnu.org/licenses/>.
 */
#include "vm_core.h"
#include "scheduler.h"
#include "syscall.h"
#include "hal/avr_uart.h"

#include <avr/interrupt.h>
#include <stddef.h>
#include "hal/avr_timer.h"
#include <setjmp.h>
#include "init.h" 
volatile uint32_t tiks = 0;
jmp_buf jump_buffer;

ISR(TIMER0_COMPA_vect) {
    tiks++;
    sched_pick_next();
    longjmp(jump_buffer, 1);
  }




int
main (void)
{
  uart_init (9600);
  init_pins();
  init_timer();
  sei ();
  uint8_t jmp_code = setjmp(jump_buffer);
  if(!jmp_code)
    {
      //first time jmp_code is 0
      //after isr escape is 1
      
    }else{
      //sreg restore do this but o be more safe
      sei();
    }
  for (;;) vm_execute ();
  return 0;
}
