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
#include "syscall.h"
#include "vm_core.h"
#include "gpio_watch.h"
#include "hal/avr_gpio.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include "scheduler.h"
extern volatile uint32_t tiks;
void

syscall_dispatch (void)
{
  switch (cpu->r[0])
    {
    case 0x00:
    case 0x01:
      {
        uint8_t local_selct = cpu->r[0];
        uint8_t pin = cpu->r[1];
        uint8_t old_sreg = SREG;
        cli ();
        bool is_changed = (pin_flags >> pin) & 1;

        if (is_changed)
          {
            pin_flags &= ~(1UL << pin);
            wanted    &= ~(1UL << pin);
            SREG = old_sreg;

            /* Pin state changed.  Check if it matches the requested state.  */
            if (gpio_read (pin) == local_selct)
              {
                cpu->pinsleep = NO_PIN_SLEEP;
              }
            else
              {
                cpu->pinsleep = pin;
                cpu->ip--;
                /*
            ok this is an ugly way to do what ~slice =0~ was doing,
            but it works for now,TODO: add yield function
            */
                sched_pick_next();
              }
          }
        else
          {
            wanted |= (1UL << pin);
            SREG = old_sreg;
            //gpio_watch_attach (pin, GPIO_EDGE_BOTH);
            cpu->pinsleep = pin;
            /*
            ok this is an ugly way to do what ~slice =0~ was doing,
            but it works for now,TODO: add yield function
            */
            cpu->ip--;
            cpu->status = WAITING_FOR_PIN;
            sched_pick_next();
          }
        break;
      }
    case 0x02:
    case 0x03:
      {
        uint8_t local_selct = cpu->r[0];
        uint8_t pin = cpu->r[1];
        bool val = cpu->r[2];
        uint8_t old_sreg = SREG;
        cli ();
        if (local_selct == 0x02)
          {
            gpio_pin_mode (pin, GPIO_DIR_IN);
            cpu->r[2] = gpio_read (pin);
          }
        else
          {
            gpio_pin_mode (pin, GPIO_DIR_OUT);
            gpio_write (pin, val);
          }
        SREG = old_sreg;
      }
      break;
    case 0x04:
      {
        uint8_t old_sreg1 = SREG;
        cli();
        uint16_t addr = vm_pop();
        SREG = old_sreg1;
        uint8_t details = cpu->r[1];
        uint8_t set = vm_bit_range (details, 0, 2);
        uint8_t social_class_val = vm_bit_range(details,2,5);
        sched_create_task (set, social_class_val, addr);
      }
      break;
    case 0x05:
      {
        if(cpu->epprom_first_time) {
          uint8_t old_sreg1 = SREG;
          cli();
          cpu->eeprom_start_addr = vm_pop();
          cpu->eeprom_length = vm_pop();
          cpu->eeprom_addr = vm_pop();
          SREG = old_sreg1;
          cpu->eeprom_i = 0;
          cpu->epprom_first_time = false;
        }
        if(eeprom_is_ready())
        {
          if(cpu->eeprom_i<cpu->eeprom_length) {
            uint8_t val = vm_read_mem(cpu->eeprom_start_addr+cpu->eeprom_i);
            eeprom_update_byte((uint8_t*)(cpu->eeprom_addr+cpu->eeprom_i), val);
            cpu->eeprom_i++;
            cpu->ip--;
            sched_pick_next();
          }else{
            cpu->epprom_first_time = true;
          }
        }else{
          cpu->ip--;
          sched_pick_next();
        }
      }
      break;
    case 0x06:
      {
        uint8_t old_sreg1 = SREG;
        cli();
        uint16_t sleep_duration_high = vm_pop();
        uint16_t sleep_duration_low = vm_pop();
        uint32_t sleep_duration = ((uint32_t)sleep_duration_high << 16) | (uint32_t)sleep_duration_low;
        SREG = old_sreg1;
        cpu->sleep_duration = sleep_duration;
        uint8_t old_sreg2 = SREG;
        cli();
        cpu->sleep_start_time = tiks;
        SREG = old_sreg2;
        cpu->status = SLEEPING;
        sched_pick_next();
      }
      break;
    }
}
