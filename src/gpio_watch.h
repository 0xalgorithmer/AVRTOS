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
#ifndef GPIO_WATCH_H
#define GPIO_WATCH_H

#include <stdint.h>

#define GPIO_EDGE_RISE 1
#define GPIO_EDGE_FALL 2
#define GPIO_EDGE_BOTH 3 //changed

extern volatile uint32_t pin_flags;
extern volatile uint32_t wanted;
extern volatile uint8_t  slice;

void gpio_watch_attach (uint8_t pin, uint8_t edge_mode);
void gpio_watch_detach (uint8_t pin);

#endif /* GPIO_WATCH_H */
