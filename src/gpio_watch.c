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
#include "gpio_watch.h"
#include <avr/io.h>
#include <avr/interrupt.h>

static uint8_t last_d, last_b, last_c;
static uint8_t rise_d, rise_b, rise_c;
static uint8_t fall_d, fall_b, fall_c;

/* Compare current port value against the last snapshot, filter
   through rise/fall masks and set pin_flags for wanted pins.  */
#define GPIO_WATCH_ISR(pin_reg, last, rise, fall, SHIFT)             \
do {                                                                  \
    uint8_t curr    = (pin_reg);                                      \
    uint8_t changed = curr ^ (last);                                  \
    (last) = curr;                                                    \
    uint8_t trig = (changed &  curr & (rise))                         \
                 | (changed & ~curr & (fall));                         \
    if (__builtin_expect (trig != 0, 0))                              \
      {                                                               \
        uint32_t match = ((uint32_t) trig << (SHIFT)) & wanted;       \
        if (match)                                                    \
          {                                                           \
            pin_flags |= match;                                       \
            slice = 0;                                                \
          }                                                           \
      }                                                               \
} while (0)

ISR (PCINT2_vect) { GPIO_WATCH_ISR (PIND, last_d, rise_d, fall_d,  0); }
ISR (PCINT0_vect) { GPIO_WATCH_ISR (PINB, last_b, rise_b, fall_b,  8); }
ISR (PCINT1_vect) { GPIO_WATCH_ISR (PINC, last_c, rise_c, fall_c, 14); }

void
gpio_watch_attach (uint8_t pin, uint8_t edge_mode)
{
  if (pin > 19)
    return;

  uint8_t old_sreg = SREG;
  cli ();

  if (pin <= 7)
    {
      uint8_t bit = (1 << pin);
      if (edge_mode & GPIO_EDGE_RISE) rise_d |= bit;
      if (edge_mode & GPIO_EDGE_FALL) fall_d |= bit;
      PCMSK2 |= bit;
      PCICR  |= (1 << PCIE2);
      last_d  = PIND;
    }
  else if (pin <= 13)
    {
      uint8_t bit = (1 << (pin - 8));
      if (edge_mode & GPIO_EDGE_RISE) rise_b |= bit;
      if (edge_mode & GPIO_EDGE_FALL) fall_b |= bit;
      PCMSK0 |= bit;
      PCICR  |= (1 << PCIE0);
      last_b  = PINB;
    }
  else
    {
      uint8_t bit = (1 << (pin - 14));
      if (edge_mode & GPIO_EDGE_RISE) rise_c |= bit;
      if (edge_mode & GPIO_EDGE_FALL) fall_c |= bit;
      PCMSK1 |= bit;
      PCICR  |= (1 << PCIE1);
      last_c  = PINC;
    }

  SREG = old_sreg;
}

void
gpio_watch_detach (uint8_t pin)
{
  if (pin > 19)
    return;

  uint8_t old_sreg = SREG;
  cli ();

  if (pin <= 7)
    {
      uint8_t mask = ~(1 << pin);
      rise_d &= mask;
      fall_d &= mask;
      PCMSK2 &= mask;
      if (!PCMSK2)
        PCICR &= ~(1 << PCIE2);
    }
  else if (pin <= 13)
    {
      uint8_t mask = ~(1 << (pin - 8));
      rise_b &= mask;
      fall_b &= mask;
      PCMSK0 &= mask;
      if (!PCMSK0)
        PCICR &= ~(1 << PCIE0);
    }
  else
    {
      uint8_t mask = ~(1 << (pin - 14));
      rise_c &= mask;
      fall_c &= mask;
      PCMSK1 &= mask;
      if (!PCMSK1)
        PCICR &= ~(1 << PCIE1);
    }

  SREG = old_sreg;
}
