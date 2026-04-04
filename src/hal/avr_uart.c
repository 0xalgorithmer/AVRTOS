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
#include "avr_uart.h"
#include <avr/io.h>

void
uart_init (uint32_t baud)
{
  uint16_t ubrr_val = (uint16_t) ((F_CPU / (16UL * baud)) - 1);
  UBRR0H = (uint8_t) (ubrr_val >> 8);
  UBRR0L = (uint8_t) ubrr_val;

  UCSR0B = (1 << TXEN0) | (1 << RXEN0);
  /* 8N1.  */
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void
uart_putc (char c)
{
  while (!(UCSR0A & (1 << UDRE0)))
    ;
  UDR0 = c;
}

void
uart_print (const char *str)
{
  while (*str)
    {
      uart_putc (*str);
      str++;
    }
}

void
uart_println_u16 (uint16_t val)
{
  char buf[6];
  char *p = buf + 5;
  *p = '\0';

  if (val == 0)
    {
      *(--p) = '0';
    }
  else
    {
      while (val > 0)
        {
          *(--p) = '0' + (val % 10);
          val /= 10;
        }
    }

  uart_print (p);
  uart_putc ('\r');
  uart_putc ('\n');
}
