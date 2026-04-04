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
#include <Arduino.h>
#include <avr/eeprom.h>
#include "PinWatch.h"

#define FLAG_Z 0x01
#define FLAG_S 0x02
#define FLAG_C 0x04
#define FLAG_O 0x08

#define EPP_PAGE_SIZE 64
#define EPP_PAGE_MASK 0x3F
#define EPP_PAGE_SHIFT 6

#define ELITE_PR 1
#define LOWLY_PR 0

#define MAX_PROCESSES 5
#define DEFAULT_TIME_SLICE 20
#define INC_STARVATION 20

#define INVALID_PAGE_ID 0xFFFF
#define NO_PIN_SLEEP 255
#define INITIAL_SP 127
#define SIGN_BIT 0x80
void make_new_process (uint8_t set, bool social_class, uint16_t addr);

/* Extract a specific bit field from an integer.
   Used to save bytecode space through bit packing.  */
inline uint8_t
get_bit_range (int number, int start_bit, int num_bits)
{
  return (number >> start_bit) & ((1 << num_bits) - 1);
}



volatile uint8_t slice = DEFAULT_TIME_SLICE;
volatile uint32_t pin_flags = 0;
volatile uint32_t wanted = 0;
bool elite_exists = 0;

typedef struct __attribute__((packed))
{
  uint8_t r[4];
  uint16_t ip;
  uint8_t flags;
  uint8_t stack[128];
  uint16_t addr_r[2];
  uint8_t instruction_cache[64];
  uint16_t cached_page_id;
  uint16_t code_base_address;
  bool active;
  bool social_class;
  uint8_t pinsleep;
  uint8_t starvation;
} process_t;

process_t processes[MAX_PROCESSES];
process_t *cpu;

inline uint8_t
fetch_byte ()
{
  uint16_t local_ip = cpu->ip;
  uint16_t physical_address = cpu->code_base_address + local_ip;
  uint16_t needed_page = physical_address >> EPP_PAGE_SHIFT;
  uint16_t offset = physical_address & EPP_PAGE_MASK;

  /* Page miss. Load the new 64-byte block from EEPROM to local cache.  */
  if (needed_page != cpu->cached_page_id)
    {
      uint16_t eeprom_addr = needed_page * EPP_PAGE_SIZE;
      eeprom_busy_wait ();
      eeprom_read_block ((void *) cpu->instruction_cache,
			 (const void *) eeprom_addr, (size_t) EPP_PAGE_SIZE);
      cpu->cached_page_id = needed_page;
    }

  cpu->ip = local_ip + 1;
  return cpu->instruction_cache[offset];
}

inline uint16_t
fetch_word ()
{
  uint8_t low = fetch_byte ();
  uint8_t high = fetch_byte ();
  return (high << 8) | low;
}

inline uint8_t
mem_read (uint16_t addr)
{
  return cpu->stack[addr];
}

inline bool
mem_write (uint16_t addr, uint8_t val)
{
  cpu->stack[addr] = val;
  return true;
}

bool
mem_arithmetic_op (uint16_t dest_addr, uint8_t op_code, uint8_t val)
{
  uint8_t current_val = mem_read (dest_addr);
  switch (op_code)
    {
    case 1:
      mem_write (dest_addr, current_val + val);
      return true;
    case 2:
      mem_write (dest_addr, current_val * val);
      return true;
    case 3:
      if (val == 0)
	return false;
      /* Optimize division by a power of two using right shift.  */
      if ((val & (val - 1)) == 0)
	{
	  mem_write (dest_addr, current_val >> (__builtin_ctz (val)));
	}
      else
	{
	  mem_write (dest_addr, current_val / val);
	}
      return true;
    default:
      return false;
    }
}

void
push (uint16_t val)
{
  uint16_t sp = cpu->addr_r[0];
  mem_write (sp, (uint8_t) (val >> 8));
  sp--;
  mem_write (sp, (uint8_t) (val & 0xFF));
  sp--;
  cpu->addr_r[0] = sp;
}

void
b8push (uint8_t val)
{
  uint16_t sp = cpu->addr_r[0];
  mem_write (sp, val);
  sp--;
  cpu->addr_r[0] = sp;
}

uint8_t
b8pop ()
{
  uint16_t sp = cpu->addr_r[0];
  sp++;
  uint8_t val = mem_read (sp);
  cpu->addr_r[0] = sp;
  return val;
}

uint16_t
pop ()
{
  uint16_t sp = cpu->addr_r[0];
  sp++;
  uint8_t low = mem_read (sp);
  sp++;
  uint8_t high = mem_read (sp);
  cpu->addr_r[0] = sp;
  return (uint16_t) ((high << 8) | low);
}

void
call (uint16_t target_addr, uint8_t offset = 1)
{
  uint16_t return_addr = cpu->ip + offset;
  push (return_addr);
  cpu->ip = target_addr;
}

inline void
ret ()
{
  cpu->ip = pop ();
}

void
handle_syscall ()
{
  switch (cpu->r[0])
    {
    case 0x0:
    case 0x1:
      {
	uint8_t local_selct = cpu->r[0];
	uint8_t pin = cpu->r[1];
	uint8_t oldReg = SREG;
	cli ();
	bool is_changed = bitRead (pin_flags, pin);

	if (is_changed)
	  {
	    bitClear (pin_flags, pin);
	    bitClear (wanted, pin);
	    SREG = oldReg;

	    /* Pin state changed. Check if it matches the requested state.  */
	    if (digitalRead (pin) == local_selct)
	      {
		// rising
		cpu->pinsleep = NO_PIN_SLEEP;
	      }
	    else
	      {
		// falling
		cpu->pinsleep = pin;
		slice = 0;
		cpu->ip--;
	      }
	  }
	else
	  {
	    bitSet (wanted, pin);
	    SREG = oldReg;
	    pinWatch_attach (pin, CHANGE);
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
	    pinMode (pin, INPUT);
	    cpu->r[2] = digitalRead (pin);
	  }
	else
	  {
	    pinMode (pin, OUTPUT);
	    digitalWrite (pin, val);
	  }
      }
      break;
    }
}

void
execute_instruction ()
{
  uint8_t opcode = fetch_byte ();
  switch (opcode)
    {
    case 0x00:
      {
	uint16_t dest = fetch_word ();
	uint8_t val = fetch_byte ();
	mem_write (dest, val);
	break;
      }
    case 0x01:
      {
	uint8_t dest = fetch_byte ();
	uint8_t mode = fetch_byte ();
	uint8_t val = fetch_byte ();
	mem_arithmetic_op (dest, mode, val);
	break;
      }
    case 0x02:
      {
	uint8_t reg = fetch_byte ();
	uint8_t addr = fetch_byte ();
	cpu->r[reg] = mem_read (addr);
	break;
      }
    case 0x03:
      {
	uint8_t source = cpu->r[fetch_byte ()];
	uint16_t dest_addr = fetch_word ();
	mem_write (dest_addr, source);
	break;
      }
    case 0x04:
      {
	uint8_t reg = fetch_byte ();
	uint8_t val = fetch_byte ();
	cpu->r[reg] = val;
	break;
      }
    case 0x05:
      {
	uint8_t reg_pos = fetch_byte ();
	int8_t val = (int8_t) fetch_byte ();
	uint8_t temp_reg = cpu->r[reg_pos];
	temp_reg += val;
	cpu->r[reg_pos] = temp_reg;

	break;
      }
    case 0x07:
      {
	uint8_t reg = fetch_byte ();
	uint8_t op2 = fetch_byte ();
	uint8_t op1 = cpu->r[reg];

	uint16_t full_res = (uint16_t) op1 - (uint16_t) op2;
	uint8_t res = (uint8_t) full_res;

	/* Evaluate the Zero flag by accumulated ORing.  */
	uint8_t z_check = res;
	z_check |= z_check >> 4;
	z_check |= z_check >> 2;
	z_check |= z_check >> 1;

	/* Compute x86-like flags: Zero, Sign, Carry, Overflow.  */
	uint8_t f_z = (~z_check & 1) * FLAG_Z;
	uint8_t f_s = (res & SIGN_BIT) >> 6;
	uint8_t f_c = ((full_res >> 8) & 1) * FLAG_C;
	uint8_t f_o = (((op1 ^ op2) & (op1 ^ res) & SIGN_BIT) >> 4);

	cpu->flags = f_z | f_s | f_c | f_o;
	break;
      }

    case 0x08:
      {
	uint16_t dest = fetch_word ();
	if (!(cpu->flags & FLAG_Z))
	  {
	    cpu->ip = dest;
	  }
	break;
      }

    case 0x09:
      {
	uint16_t addr = fetch_word ();
	//details = class & set 
	uint8_t details = fetch_byte ();
	/*this is temp var compiler will put them
	   direct to the api parm reg(no ram will be used)
	 */
	uint8_t set = get_bit_range (details, 0, 2);
	uint8_t social_class_val = bitRead (details, 3);
	make_new_process (set, social_class_val, addr);
	break;
      }

    case 0x0a:
      {
	uint16_t dest = fetch_word ();
	if (cpu->flags & FLAG_Z)
	  {
	    cpu->ip = dest;
	  }
	break;
      }

    case 0x0b:
      b8push (cpu->r[fetch_byte ()]);
      break;

    case 0x0c:
      {
	uint8_t mode = fetch_byte ();
	uint8_t dest = fetch_byte ();
	uint8_t src = fetch_byte ();
	uint8_t temp_dest = cpu->r[dest];
	uint8_t temp_src = cpu->r[src];

	if (mode == 0)
	  temp_dest += temp_src;
	else if (mode == 1)
	  temp_dest -= temp_src;
	else if (mode == 2)
	  temp_dest *= temp_src;

	cpu->r[dest] = temp_dest;
	break;
      }

    case 0x0d:
      {
	uint16_t dest = fetch_word ();
	cpu->ip = dest;
	break;
      }

    case 0x0e:
      {
	uint8_t reg = fetch_byte ();
	cpu->r[reg] = b8pop ();
	break;
      }

    case 0x0f:
      {
	uint16_t target = fetch_word();
	call (target,0);
	break;
      }

    case 0x10:
      ret ();
      break;

    case 0x11:
      {
	uint8_t mode = fetch_byte ();
	uint8_t dest = fetch_byte ();
	uint16_t temp_dest = cpu->addr_r[dest];

	if (mode == 0)
	  {
	    uint8_t src = fetch_byte ();
	    temp_dest = cpu->addr_r[src];
	  }
	else if (mode == 1)
	  {
	    uint8_t src = fetch_byte ();
	    temp_dest += cpu->addr_r[src];
	  }
	else if (mode == 2)
	  {
	    int8_t val = (int8_t) fetch_byte ();
	    temp_dest += val;
	  }

	cpu->addr_r[dest] = temp_dest;
	break;
      }
    case 0x12:
      break;
    case 0x14:
      {
	uint8_t dest = fetch_byte ();
	uint8_t val = fetch_byte ();
	cpu->addr_r[dest] = val;
	break;
      }
    case 0x15:
      {
	uint8_t dest = fetch_byte ();
	uint16_t addr = fetch_word ();
	uint8_t low = mem_read (addr);
	uint8_t high = mem_read (addr + 1);
	cpu->addr_r[dest] = (high << 8) | low;
	break;
      }
    case 0x16:
      {
	uint8_t src = fetch_byte ();
	uint16_t dest_addr = fetch_word ();
	uint16_t val_to_store = cpu->addr_r[src];
	mem_write (dest_addr, val_to_store & 0xFF);
	mem_write (dest_addr + 1, val_to_store >> 8);
	break;
      }
    case 0x17:
      {
	uint8_t dest = fetch_byte ();
	uint8_t src = fetch_byte ();
	cpu->addr_r[dest] = cpu->r[src];
	break;
      }
    case 0x18:
      {
	uint8_t dest = fetch_byte ();
	uint8_t src = fetch_byte ();
	cpu->r[dest] = cpu->addr_r[src];
	break;
      }
    case 0x1d:
      {
	uint8_t src_reg_idx = fetch_byte ();
	uint8_t mode = fetch_byte ();
	uint8_t offset = fetch_byte ();

	switch (mode)
	  {
	  case 0x0:
	    {
	      uint8_t val = fetch_byte ();
	      uint16_t base_addr = cpu->addr_r[src_reg_idx];
	      mem_write (base_addr + offset, val);
	      break;
	    }
	  case 0x1:
	    {
	      uint8_t dest_reg_idx = fetch_byte ();
	      uint16_t base_addr = cpu->addr_r[src_reg_idx];
	      cpu->addr_r[dest_reg_idx] = mem_read (base_addr + offset);
	      break;
	    }
	  case 0x2:
	    {
	      uint8_t dest_reg_idx = fetch_byte ();
	      uint16_t base_addr = cpu->addr_r[src_reg_idx];
	      cpu->r[dest_reg_idx] = mem_read (base_addr + offset);
	      break;
	    }
	  case 0x3:
	    {
	      uint16_t src_addr = cpu->addr_r[src_reg_idx] + offset;
	      uint8_t dest_reg_idx = fetch_byte ();
	      uint8_t dest_offset = fetch_byte ();
	      uint16_t dest_addr = cpu->addr_r[dest_reg_idx] + dest_offset;
	      mem_write (src_addr, mem_read (dest_addr));
	      break;
	    }
	  case 0x4:
	    {
	      uint8_t val = fetch_byte ();
	      uint8_t base_addr = cpu->r[src_reg_idx];
	      mem_write (base_addr + offset, val);
	      break;
	    }
	  case 0x5:
	    {
	      uint8_t dest_reg_idx = fetch_byte ();
	      uint8_t base_addr = cpu->r[src_reg_idx];
	      cpu->addr_r[dest_reg_idx] = mem_read (base_addr + offset);
	      break;
	    }
	  case 0x6:
	    {
	      uint8_t dest_reg_idx = fetch_byte ();
	      uint8_t base_addr = cpu->r[src_reg_idx];
	      cpu->r[dest_reg_idx] = mem_read (base_addr + offset);
	      break;
	    }
	  case 0x7:
	    {
	      uint8_t src_addr = cpu->r[src_reg_idx] + offset;
	      uint8_t dest_reg_idx = fetch_byte ();
	      uint8_t dest_offset = fetch_byte ();
	      uint8_t dest_addr = cpu->r[dest_reg_idx] + dest_offset;
	      mem_write (src_addr, mem_read (dest_addr));
	      break;
	    }
	  }
	break;
      }
    case 0x1e:
      {
	uint8_t mode = fetch_byte ();
	/* Perform varied bitwise and logic operations based on decoded addressing mode.  */
	uint8_t op1 = fetch_byte ();

	if (mode == 0x0)
	  {
	    uint16_t temp_addr = cpu->addr_r[op1];
	    cpu->addr_r[op1] = ~temp_addr;
	    break;
	  }
	if (mode == 0x1)
	  {
	    uint8_t temp_reg = cpu->r[op1];
	    cpu->r[op1] = ~temp_reg;
	    break;
	  }

	uint8_t op2 = fetch_byte ();
	uint16_t dest_val = 0;
	uint16_t src_val = 0;
	uint8_t alu_op = 0;
	uint8_t dest_is_16bit = 0;

	if (mode >= 0x2 && mode <= 0x6)
	  {
	    dest_val = cpu->addr_r[op1];
	    src_val = cpu->addr_r[op2];
	    alu_op = mode - 0x2;
	    dest_is_16bit = 1;
	  }
	else if (mode >= 0x7 && mode <= 0xb)
	  {
	    dest_val = cpu->addr_r[op1];
	    src_val = op2;
	    alu_op = mode - 0x7;
	    dest_is_16bit = 1;
	  }
	else if (mode >= 0xc && mode <= 0x10)
	  {
	    dest_val = cpu->r[op1];
	    src_val = cpu->r[op2];
	    alu_op = mode - 0xc;
	    dest_is_16bit = 0;
	  }
	else if (mode >= 0x11 && mode <= 0x15)
	  {
	    dest_val = cpu->r[op1];
	    src_val = op2;
	    alu_op = mode - 0x11;
	    dest_is_16bit = 0;
	  }
	else if (mode >= 0x16 && mode <= 0x1a)
	  {
	    dest_val = cpu->r[op1];
	    src_val = cpu->addr_r[op2];
	    alu_op = mode - 0x16;
	    dest_is_16bit = 0;
	  }
	else if (mode >= 0x1b && mode <= 0x1f)
	  {
	    dest_val = cpu->addr_r[op1];
	    src_val = cpu->r[op2];
	    alu_op = mode - 0x1b;
	    dest_is_16bit = 1;
	  }
	else
	  {
	    break;
	  }

	switch (alu_op)
	  {
	  case 0:
	    dest_val &= src_val;
	    break;
	  case 1:
	    dest_val |= src_val;
	    break;
	  case 2:
	    dest_val ^= src_val;
	    break;
	  case 3:
	    dest_val <<= src_val;
	    break;
	  case 4:
	    dest_val >>= src_val;
	    break;
	  }

	if (dest_is_16bit)
	  {
	    cpu->addr_r[op1] = dest_val;
	  }
	else
	  {
	    cpu->r[op1] = (uint8_t) dest_val;
	  }
	break;
      }
    case 0x1f:
      {
	uint16_t dest = fetch_word ();
	uint8_t local_flags = cpu->flags;
	if (local_flags & FLAG_C)
	  cpu->ip = dest;
	break;
      }
    case 0x20:
      {
	uint16_t dest = fetch_word ();
	uint8_t local_flags = cpu->flags;
	if (!(local_flags & FLAG_C))
	  cpu->ip = dest;
	break;
      }
    case 0x21:
      {
	uint16_t dest = fetch_word ();
	uint8_t local_flags = cpu->flags;
	if ((local_flags & FLAG_C) || (local_flags & FLAG_Z))
	  cpu->ip = dest;
	break;
      }
    case 0x22:
      {
	uint16_t dest = fetch_word ();
	uint8_t local_flags = cpu->flags;
	if (!((local_flags & FLAG_C) || (local_flags & FLAG_Z)))
	  cpu->ip = dest;
	break;
      }
    case 0x23:
      {
	uint16_t dest = fetch_word ();
	uint8_t local_flags = cpu->flags;
	bool s = (local_flags & FLAG_S);
	bool o = (local_flags & FLAG_O);
	if (!!s != !!o)
	  cpu->ip = dest;
	break;
      }
    case 0x24:
      {
	uint16_t dest = fetch_word ();
	uint8_t local_flags = cpu->flags;
	bool s = (local_flags & FLAG_S);
	bool o = (local_flags & FLAG_O);
	if (!!s == !!o)
	  cpu->ip = dest;
	break;
      }
    case 0x25:
      {
	uint16_t dest = fetch_word ();
	uint8_t local_flags = cpu->flags;
	bool s = (local_flags & FLAG_S);
	bool o = (local_flags & FLAG_O);
	if ((local_flags & FLAG_Z) || (!!s != !!o))
	  cpu->ip = dest;
	break;
      }
    case 0x26:
      {
	uint16_t dest = fetch_word ();
	uint8_t local_flags = cpu->flags;
	bool s = (local_flags & FLAG_S);
	bool o = (local_flags & FLAG_O);
	if (!(local_flags & FLAG_Z) && (!!s == !!o))
	  cpu->ip = dest;
	break;
      }
    case 0x27:
      push (cpu->addr_r[fetch_byte ()]);
      break;
    case 0x28:
      {
	uint8_t reg = fetch_byte ();
	cpu->addr_r[reg] = pop ();
	break;
      }
    case 0x29:
      {
	uint8_t reg = fetch_byte ();
	cpu->ip = cpu->addr_r[reg];
	break;
      }
    case 0x2a:
      {
	int16_t offset = (int16_t) fetch_word ();
	uint16_t temp_ip = cpu->ip;
	temp_ip += offset;
	cpu->ip = temp_ip;
	break;
      }
    default:
      Serial.print ("HALT: Unknown Opcode at ");
      Serial.println (cpu->ip - 1);
      break;
    }
}

void
make_new_process (uint8_t set, bool social_class, uint16_t addr)
{
  if (set >= MAX_PROCESSES)
    return;
  process_t *new_p = &processes[set];

  new_p->addr_r[0] = INITIAL_SP;
  new_p->addr_r[1] = INITIAL_SP;
  new_p->cached_page_id = INVALID_PAGE_ID;
  new_p->flags = 0;
  new_p->ip = 0;
  new_p->starvation = 0;
  new_p->r[0] = 0;
  new_p->r[1] = 0;
  new_p->r[2] = 0;
  new_p->r[3] = 0;
  new_p->code_base_address = addr;
  new_p->pinsleep = NO_PIN_SLEEP;
  new_p->active = 1;

  if (social_class)
    {
      if (social_class ^ elite_exists)
	{
	  new_p->social_class = 1;
	  elite_exists = 1;
	  return;
	}
    }
  new_p->social_class = 0;
}

void
sched_pick_next ()
{
  process_t *next_cpu = 0;
  uint8_t max_val = 0;

  /* Select the process with maximum starvation, or wake a process awaiting IO.  */
  for (uint8_t i = 0; i < MAX_PROCESSES; i++)
    {
      if (!processes[i].active)
	continue;

      if (processes[i].pinsleep != NO_PIN_SLEEP)
	{
	  bool wake_up = false;
	  uint8_t pin = processes[i].pinsleep;
	  uint32_t mask = (1UL << pin);
	  uint8_t oldSREG = SREG;
	  cli ();
	  if ((wanted & mask) && (pin_flags & mask))
	    {
	      //wanted &= ~mask;
	      //pin_flags &= ~mask;
	      wake_up = true;
	    }
	  SREG = oldSREG;
	  if (wake_up)
	    {
	      processes[i].pinsleep = NO_PIN_SLEEP;
	      cpu = &processes[i];
	      return;
	    }
	}

      if (processes[i].social_class)
	{
	  cpu = &processes[i];
	  return;
	}

      if (cpu != &processes[i])
	{
	  processes[i].starvation += INC_STARVATION;
	}

      if (!next_cpu || processes[i].starvation >= max_val)
	{
	  max_val = processes[i].starvation;
	  next_cpu = &processes[i];
	}
    }

  cpu = next_cpu;
  if (cpu)
    cpu->starvation = 0;
}

void
setup ()
{
}

void
loop ()
{
  if (cpu == NULL)
    {
      make_new_process (1, 0, 0);
    }
  execute_instruction ();
  slice--;
  if (!slice)
    {
      slice = DEFAULT_TIME_SLICE;
      if (!cpu->social_class)
	sched_pick_next ();
    }
}
