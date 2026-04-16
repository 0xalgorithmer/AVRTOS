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
#ifndef VM_CORE_H
#define VM_CORE_H

#include <stdint.h>
#include <stdbool.h>

#define FLAG_Z 0x01
#define FLAG_S 0x02
#define FLAG_C 0x04
#define FLAG_O 0x08

#define EPP_PAGE_SIZE  64
#define EPP_PAGE_MASK  0x3F
#define EPP_PAGE_SHIFT 6


#define MAX_PROCESSES     5
#define INC_STARVATION    1
#define INVALID_PAGE_ID   0xFFFF
#define NO_PIN_SLEEP      255
#define INITIAL_SP        127
#define SIGN_BIT          0x80
#define DEAD              0
#define RUNNING           1
#define SLEEPING          2
#define WAITING_FOR_PIN   3
typedef struct __attribute__ ((packed))
{
  uint32_t sleep_start_time;
  uint32_t sleep_duration;
  uint16_t ip;
  uint16_t starvation;
  uint16_t last_ip;
  uint16_t cached_page_id;
  uint16_t code_base_address;
  uint16_t addr_r[2];
  uint16_t eeprom_i;
  uint16_t eeprom_start_addr;
  uint16_t eeprom_length;
  uint16_t eeprom_addr;
  uint8_t  r[4];
  uint8_t  flags;
  uint8_t  stack[128];
  uint8_t  instruction_cache[64];
  uint8_t  status; //0 :dead,1 :running,2 :sleeping,3 :waiting for pin
  uint8_t  social_class;
  uint8_t  pinsleep;
  bool     epprom_first_time;
} process_t;

extern process_t  processes[MAX_PROCESSES];
extern process_t *cpu;

extern volatile uint8_t  slice;
extern volatile uint32_t pin_flags;
extern volatile uint32_t wanted;


/* Extract a specific bit field from an integer.
   Used to save bytecode space through bit packing.  */
static inline uint8_t
vm_bit_range (int number, int start_bit, int num_bits)
{
  return (number >> start_bit) & ((1 << num_bits) - 1);
}

uint8_t  vm_fetch_byte (void);
uint16_t vm_fetch_word (void);
uint8_t  vm_mem_read   (uint16_t addr);
bool     vm_mem_write  (uint16_t addr, uint8_t val);
bool     vm_mem_arith  (uint16_t dest_addr, uint8_t op_code, uint8_t val);
void     vm_push       (uint16_t val);
uint16_t vm_pop        (void);
void     vm_push_byte  (uint8_t val);
uint8_t  vm_pop_byte   (void);
void     vm_call       (uint16_t target_addr, uint8_t offset);
void     vm_ret        (void);
void     vm_execute    (void);

#endif /* VM_CORE_H */
