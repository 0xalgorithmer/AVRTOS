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
/* Pin mapping (ATmega328P):
     0..7  → PORTD,  8..13 → PORTB,  14..19 → PORTC.  */

#ifndef AVR_GPIO_H
#define AVR_GPIO_H

#include <avr/io.h>
#include <stdint.h>

#define GPIO_INPUT         0
#define GPIO_OUTPUT        1
#define GPIO_INPUT_PULLUP  2
static inline void 
gpio_pin_mode (uint8_t pin, uint8_t mode)
{
  if (pin <= 7)
    {
      uint8_t bit = (1 << pin); 
      
      if (mode == GPIO_OUTPUT) {
        DDRD |= bit;           
      } else {
        DDRD &= ~bit;         
        if (mode == GPIO_INPUT_PULLUP)
          PORTD |= bit;         
        else
          PORTD &= ~bit;        
      }
    }
  else if (pin <= 13)
    {
      uint8_t bit = (1 << (pin - 8));
      
      if (mode == GPIO_OUTPUT) {
        DDRB |= bit;
      } else {
        DDRB &= ~bit;
        if (mode == GPIO_INPUT_PULLUP)
          PORTB |= bit;
        else
          PORTB &= ~bit;
      }
    }
  else if (pin <= 19)
    {
      uint8_t bit = (1 << (pin - 14));
      
      if (mode == GPIO_OUTPUT) {
        DDRC |= bit;
      } else {
        DDRC &= ~bit;
        if (mode == GPIO_INPUT_PULLUP)
          PORTC |= bit;
        else
          PORTC &= ~bit;
      }
    }
}

static inline void
gpio_write (uint8_t pin, uint8_t val)
{
  if (pin <= 7)
    {
      if (val)
        PORTD |= (1 << pin);
      else
        PORTD &= ~(1 << pin);
    }
  else if (pin <= 13)
    {
      uint8_t bit = pin - 8;
      if (val)
        PORTB |= (1 << bit);
      else
        PORTB &= ~(1 << bit);
    }
  else if (pin <= 19)
    {
      uint8_t bit = pin - 14;
      if (val)
        PORTC |= (1 << bit);
      else
        PORTC &= ~(1 << bit);
    }
}

/* Returns 0 or 1.  */
static inline uint8_t
gpio_read (uint8_t pin)
{
  if (pin <= 7)
    return (PIND >> pin) & 1;
  else if (pin <= 13)
    return (PINB >> (pin - 8)) & 1;
  else if (pin <= 19)
    return (PINC >> (pin - 14)) & 1;
  return 0;
}

#endif /* AVR_GPIO_H */
