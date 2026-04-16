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
#include "scheduler.h"
#include "gpio_watch.h"

#include <avr/io.h>
#include <avr/interrupt.h>

process_t  processes[MAX_PROCESSES];
process_t *cpu = 0;
extern volatile uint32_t tiks;
volatile uint32_t pin_flags = 0;
volatile uint32_t wanted = 0;


void
sched_create_task (uint8_t set, uint8_t social_class, uint16_t addr)
{
  if (set >= MAX_PROCESSES)
    return;

  process_t *new_p = &processes[set];

  new_p->addr_r[0]          = INITIAL_SP;
  new_p->addr_r[1]          = INITIAL_SP;
  new_p->cached_page_id     = INVALID_PAGE_ID;
  new_p->flags              = 0;
  new_p->ip                 = 0;
  new_p->starvation         = 0;
  new_p->r[0]               = 0;
  new_p->r[1]               = 0;
  new_p->r[2]               = 0;
  new_p->r[3]               = 0;
  new_p->code_base_address  = addr;
  new_p->pinsleep           = NO_PIN_SLEEP;
  new_p->social_class       = social_class;
  new_p->status             = 1;
}

/* Select the process with maximum starvation, or wake a process
   awaiting IO.  */
__attribute__ ((hot))
void
sched_pick_next (void)
{
  process_t *next_cpu = 0;
  uint16_t max_val = 0;
  for (uint8_t i = 0; i < MAX_PROCESSES; i++)
    {
      if (!processes[i].status)
        continue;

      if (processes[i].pinsleep != NO_PIN_SLEEP||processes[i].status==WAITING_FOR_PIN)
        {
          bool wake_up = false;
          uint8_t pin = processes[i].pinsleep;
          uint32_t mask = (1UL << pin);
          uint8_t old_sreg = SREG;
          cli ();
          if ((wanted & mask) && (pin_flags & mask))
            wake_up = true;
          SREG = old_sreg;

          if (wake_up)
            {
              processes[i].pinsleep = NO_PIN_SLEEP;
              processes[i].status = RUNNING;
            }
        }
      if(processes[i].status==SLEEPING) {
        uint32_t elapsed = tiks - processes[i].sleep_start_time;
        if(elapsed>=processes[i].sleep_duration) {
          processes[i].status = RUNNING;
        }
      }
      if (cpu != &processes[i]&&processes[i].status==RUNNING)
        processes[i].starvation += (INC_STARVATION+processes[i].social_class);

      if((!next_cpu || processes[i].starvation >= max_val)&&processes[i].status==RUNNING)
        {
          max_val = processes[i].starvation;
          next_cpu = &processes[i];
        }
    }

  cpu = next_cpu;
  if (cpu)
    {
      cpu->starvation = 0;
      cpu->ip = cpu->last_ip;
    }
}
