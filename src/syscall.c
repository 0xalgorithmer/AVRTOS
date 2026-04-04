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
                slice = 0;
                cpu->ip--;
              }
          }
        else
          {
            wanted |= (1UL << pin);
            SREG = old_sreg;
            //gpio_watch_attach (pin, GPIO_EDGE_BOTH);
            cpu->pinsleep = pin;
            slice = 0;
            cpu->ip--;
          }
        break;
      }
    case 0x02:
    case 0x03:
      {
        uint8_t local_selct = cpu->r[0];
        uint8_t pin = cpu->r[1];
        bool val = cpu->r[2];
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
      }
      break;
    }
}
