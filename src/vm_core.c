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
#include "syscall.h"
#include "scheduler.h"
#include "gpio_watch.h"
#include "hal/avr_gpio.h"
#include "hal/avr_uart.h"

#include <avr/eeprom.h>
#include <avr/interrupt.h>
__attribute__ ((hot))
uint8_t
vm_fetch_byte (void)
{
  uint16_t local_ip = cpu->ip;
  uint16_t physical_address = cpu->code_base_address + local_ip;
  uint16_t needed_page = physical_address >> EPP_PAGE_SHIFT;
  uint16_t offset = physical_address & EPP_PAGE_MASK;

  /* Page miss.  Load the new 64-byte block from EEPROM to local cache.  */
  if (needed_page != cpu->cached_page_id)
    {
      uint16_t eeprom_addr = needed_page * EPP_PAGE_SIZE;
      eeprom_busy_wait ();
      uint8_t old_sreg = SREG;
      cli ();
      eeprom_read_block ((void *) cpu->instruction_cache,
                         (const void *) eeprom_addr,
                         (size_t) EPP_PAGE_SIZE);
      cpu->cached_page_id = needed_page;
      SREG = old_sreg;
    }

  cpu->ip = local_ip + 1;
  return cpu->instruction_cache[offset];
}

uint16_t
vm_fetch_word (void)
{
  uint8_t low  = vm_fetch_byte ();
  uint8_t high = vm_fetch_byte ();
  return (high << 8) | low;
}

uint8_t
vm_mem_read (uint16_t addr)
{
  return cpu->stack[addr];
}

bool
vm_mem_write (uint16_t addr, uint8_t val)
{
  cpu->stack[addr] = val;
  return true;
}

bool
vm_mem_arith (uint16_t dest_addr, uint8_t op_code, uint8_t val)
{
  uint8_t current_val = vm_mem_read (dest_addr);
  switch (op_code)
    {
    case 1:
      vm_mem_write (dest_addr, current_val + val);
      return true;
    case 2:
      vm_mem_write (dest_addr, current_val * val);
      return true;
    case 3:
      if (val == 0)
        return false;
      /* Optimize division by a power of two using right shift.  */
      if ((val & (val - 1)) == 0)
        vm_mem_write (dest_addr, current_val >> (__builtin_ctz (val)));
      else
        vm_mem_write (dest_addr, current_val / val);
      return true;
    default:
      return false;
    }
}

void
vm_push (uint16_t val)
{
  uint16_t sp = cpu->addr_r[0];
  vm_mem_write (sp, (uint8_t) (val >> 8));
  sp--;
  vm_mem_write (sp, (uint8_t) (val & 0xFF));
  sp--;
  cpu->addr_r[0] = sp;
}

uint16_t
vm_pop (void)
{
  uint16_t sp = cpu->addr_r[0];
  sp++;
  uint8_t low = vm_mem_read (sp);
  sp++;
  uint8_t high = vm_mem_read (sp);
  cpu->addr_r[0] = sp;
  return (uint16_t) ((high << 8) | low);
}

void
vm_push_byte (uint8_t val)
{
  uint16_t sp = cpu->addr_r[0];
  vm_mem_write (sp, val);
  sp--;
  cpu->addr_r[0] = sp;
}

uint8_t
vm_pop_byte (void)
{
  uint16_t sp = cpu->addr_r[0];
  sp++;
  uint8_t val = vm_mem_read (sp);
  cpu->addr_r[0] = sp;
  return val;
}

void
vm_call (uint16_t target_addr, uint8_t offset)
{
  uint16_t return_addr = cpu->ip + offset;
  vm_push (return_addr);
  cpu->ip = target_addr;
}

void
vm_ret (void)
{
  cpu->ip = vm_pop ();
}

/* flatten: inline every call inside this function.
   hot: optimize harder, place in hot section.  */
__attribute__ ((flatten, hot))
void
vm_execute (void)
{
  uint8_t old_sreg = SREG;
  cli ();
  cpu->last_ip = cpu->ip;
  SREG = old_sreg;
  uint8_t opcode = vm_fetch_byte ();
  switch (opcode)
    {
    case 0x00:
      {
        uint16_t dest = vm_fetch_word ();
        uint8_t val = vm_fetch_byte ();
        vm_mem_write (dest, val);
        break;
      }
    case 0x01:
      {
        uint8_t dest = vm_fetch_byte ();
        uint8_t mode = vm_fetch_byte ();
        uint8_t val = vm_fetch_byte ();
        uint8_t old_sreg = SREG;
        cli ();
        vm_mem_arith (dest, mode, val);
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x02:
      {
        uint8_t reg = vm_fetch_byte ();
        uint8_t addr = vm_fetch_byte ();
        cpu->r[reg] = vm_mem_read (addr);
        break;
      }
    case 0x03:
      {
        uint8_t source = cpu->r[vm_fetch_byte ()];
        uint16_t dest_addr = vm_fetch_word ();
        vm_mem_write (dest_addr, source);
        break;
      }
    case 0x04:
      {
        uint8_t reg = vm_fetch_byte ();
        uint8_t val = vm_fetch_byte ();
        cpu->r[reg] = val;
        break;
      }*
    case 0x05:
      {
        uint8_t reg_pos = vm_fetch_byte ();
        int8_t val = (int8_t) vm_fetch_byte ();
        uint8_t old_sreg = SREG;
        cli ();
        uint8_t temp_reg = cpu->r[reg_pos];
        temp_reg += val;
        cpu->r[reg_pos] = temp_reg;
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x07:
      {
        uint8_t reg = vm_fetch_byte ();
        uint8_t op2 = vm_fetch_byte ();
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
        uint16_t dest = vm_fetch_word ();
        if (!(cpu->flags & FLAG_Z))
          cpu->ip = dest;
        break;
      }
    case 0x09:
      {
        /*
        uint16_t addr = vm_fetch_word ();
        uint8_t details = vm_fetch_byte ();
        uint8_t set = vm_bit_range (details, 0, 2);
        uint8_t social_class_val = (details >> 3) & 1;
        sched_create_task (set, social_class_val, addr);
        */
        break;
      }
    case 0x0a:
      {
        uint16_t dest = vm_fetch_word ();
        if (cpu->flags & FLAG_Z)
          cpu->ip = dest;
        break;
      }
    case 0x0b:
      {
        uint8_t reg_idx = vm_fetch_byte ();
        uint8_t old_sreg = SREG;
        cli ();
        vm_push_byte (cpu->r[reg_idx]);
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x0c:
      {
        uint8_t mode = vm_fetch_byte ();
        uint8_t dest = vm_fetch_byte ();
        uint8_t src = vm_fetch_byte ();
        uint8_t old_sreg = SREG;
        cli ();
        uint8_t temp_dest = cpu->r[dest];
        uint8_t temp_src = cpu->r[src];

        if (mode == 0)
          temp_dest += temp_src;
        else if (mode == 1)
          temp_dest -= temp_src;
        else if (mode == 2)
          temp_dest *= temp_src;

        cpu->r[dest] = temp_dest;
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x0d:
      {
        uint16_t dest = vm_fetch_word ();
        cpu->ip = dest;
        break;
      }
    case 0x0e:
      {
        uint8_t reg = vm_fetch_byte ();
        uint8_t old_sreg = SREG;
        cli ();
        cpu->r[reg] = vm_pop_byte ();
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x0f:
      {
        uint16_t target = vm_fetch_byte ();
        uint8_t old_sreg = SREG;
        cli ();
        vm_call (target, 0);
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x10:
      {
        uint8_t old_sreg = SREG;
        cli ();
        vm_ret ();
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x11:
      {
        uint8_t mode = vm_fetch_byte ();
        uint8_t dest = vm_fetch_byte ();

        if (mode == 0)
          {
            uint8_t src = vm_fetch_byte ();
            cpu->addr_r[dest] = cpu->addr_r[src];
          }
        else if (mode == 1)
          {
            uint8_t src = vm_fetch_byte ();
            uint8_t old_sreg = SREG;
            cli ();
            cpu->addr_r[dest] += cpu->addr_r[src];
            cpu->last_ip = cpu->ip;
            SREG = old_sreg;
          }
        else if (mode == 2)
          {
            int8_t val = (int8_t) vm_fetch_byte ();
            uint8_t old_sreg = SREG;
            cli ();
            cpu->addr_r[dest] += val;
            cpu->last_ip = cpu->ip;
            SREG = old_sreg;
          }

        break;
      }
    case 0x12:
      break;
    case 0x13:
      syscall_dispatch ();
      {
        uint8_t old_sreg = SREG;
        cli ();
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
      }
      break;
    case 0x14:
      {
        uint8_t dest = vm_fetch_byte ();
        uint8_t val = vm_fetch_byte ();
        cpu->addr_r[dest] = val;
        break;
      }
    case 0x15:
      {
        uint8_t dest = vm_fetch_byte ();
        uint16_t addr = vm_fetch_word ();
        uint8_t low = vm_mem_read (addr);
        uint8_t high = vm_mem_read (addr + 1);
        cpu->addr_r[dest] = (high << 8) | low;
        break;
      }
    case 0x16:
      {
        uint8_t src = vm_fetch_byte ();
        uint16_t dest_addr = vm_fetch_word ();
        uint16_t val_to_store = cpu->addr_r[src];
        vm_mem_write (dest_addr, val_to_store & 0xFF);
        vm_mem_write (dest_addr + 1, val_to_store >> 8);
        break;
      }
    case 0x17:
      {
        uint8_t dest = vm_fetch_byte ();
        uint8_t src = vm_fetch_byte ();
        cpu->addr_r[dest] = cpu->r[src];
        break;
      }
    case 0x18:
      {
        uint8_t dest = vm_fetch_byte ();
        uint8_t src = vm_fetch_byte ();
        cpu->r[dest] = cpu->addr_r[src];
        break;
      }
    case 0x1d:
      {
        uint8_t src_reg_idx = vm_fetch_byte ();
        uint8_t mode = vm_fetch_byte ();
        uint8_t offset = vm_fetch_byte ();

        switch (mode)
          {
          case 0x0:
            {
              uint8_t val = vm_fetch_byte ();
              uint16_t base_addr = cpu->addr_r[src_reg_idx];
              vm_mem_write (base_addr + offset, val);
              break;
            }
          case 0x1:
            {
              uint8_t dest_reg_idx = vm_fetch_byte ();
              uint16_t base_addr = cpu->addr_r[src_reg_idx];
              cpu->addr_r[dest_reg_idx] = vm_mem_read (base_addr + offset);
              break;
            }
          case 0x2:
            {
              uint8_t dest_reg_idx = vm_fetch_byte ();
              uint16_t base_addr = cpu->addr_r[src_reg_idx];
              cpu->r[dest_reg_idx] = vm_mem_read (base_addr + offset);
              break;
            }
          case 0x3:
            {
              uint16_t src_addr = cpu->addr_r[src_reg_idx] + offset;
              uint8_t dest_reg_idx = vm_fetch_byte ();
              uint8_t dest_offset = vm_fetch_byte ();
              uint16_t dest_addr = cpu->addr_r[dest_reg_idx] + dest_offset;
              vm_mem_write (src_addr, vm_mem_read (dest_addr));
              break;
            }
          case 0x4:
            {
              uint8_t val = vm_fetch_byte ();
              uint8_t base_addr = cpu->r[src_reg_idx];
              vm_mem_write (base_addr + offset, val);
              break;
            }
          case 0x5:
            {
              uint8_t dest_reg_idx = vm_fetch_byte ();
              uint8_t base_addr = cpu->r[src_reg_idx];
              cpu->addr_r[dest_reg_idx] = vm_mem_read (base_addr + offset);
              break;
            }
          case 0x6:
            {
              uint8_t dest_reg_idx = vm_fetch_byte ();
              uint8_t base_addr = cpu->r[src_reg_idx];
              cpu->r[dest_reg_idx] = vm_mem_read (base_addr + offset);
              break;
            }
          case 0x7:
            {
              uint8_t src_addr = cpu->r[src_reg_idx] + offset;
              uint8_t dest_reg_idx = vm_fetch_byte ();
              uint8_t dest_offset = vm_fetch_byte ();
              uint8_t dest_addr = cpu->r[dest_reg_idx] + dest_offset;
              vm_mem_write (src_addr, vm_mem_read (dest_addr));
              break;
            }
          }
        break;
      }

    /* Perform varied bitwise and logic operations based on
       decoded addressing mode.  */
    case 0x1e:
      {
        uint8_t mode = vm_fetch_byte ();
        uint8_t op1 = vm_fetch_byte ();

        if (mode == 0x0)
          {
            uint8_t old_sreg = SREG;
            cli ();
            uint16_t temp_addr = cpu->addr_r[op1];
            cpu->addr_r[op1] = ~temp_addr;
            cpu->last_ip = cpu->ip;
            SREG = old_sreg;
            break;
          }
        if (mode == 0x1)
          {
            uint8_t old_sreg = SREG;
            cli ();
            uint8_t temp_reg = cpu->r[op1];
            cpu->r[op1] = ~temp_reg;
            cpu->last_ip = cpu->ip;
            SREG = old_sreg;
            break;
          }

        uint8_t op2 = vm_fetch_byte ();
        uint16_t dest_val = 0;
        uint16_t src_val = 0;
        uint8_t alu_op = 0;
        uint8_t dest_is_16bit = 0;

        uint8_t old_sreg = SREG;
        cli ();

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
            SREG = old_sreg;
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
          cpu->addr_r[op1] = dest_val;
        else
          cpu->r[op1] = (uint8_t) dest_val;

        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x1f:
      {
        uint16_t dest = vm_fetch_word ();
        uint8_t local_flags = cpu->flags;
        if (local_flags & FLAG_C)
          cpu->ip = dest;
        break;
      }
    case 0x20:
      {
        uint16_t dest = vm_fetch_word ();
        uint8_t local_flags = cpu->flags;
        if (!(local_flags & FLAG_C))
          cpu->ip = dest;
        break;
      }
    case 0x21:
      {
        uint16_t dest = vm_fetch_word ();
        uint8_t local_flags = cpu->flags;
        if ((local_flags & FLAG_C) || (local_flags & FLAG_Z))
          cpu->ip = dest;
        break;
      }
    case 0x22:
      {
        uint16_t dest = vm_fetch_word ();
        uint8_t local_flags = cpu->flags;
        if (!((local_flags & FLAG_C) || (local_flags & FLAG_Z)))
          cpu->ip = dest;
        break;
      }
    case 0x23:
      {
        uint16_t dest = vm_fetch_word ();
        uint8_t local_flags = cpu->flags;
        bool s = (local_flags & FLAG_S);
        bool o = (local_flags & FLAG_O);
        if (!!s != !!o)
          cpu->ip = dest;
        break;
      }
    case 0x24:
      {
        uint16_t dest = vm_fetch_word ();
        uint8_t local_flags = cpu->flags;
        bool s = (local_flags & FLAG_S);
        bool o = (local_flags & FLAG_O);
        if (!!s == !!o)
          cpu->ip = dest;
        break;
      }
    case 0x25:
      {
        uint16_t dest = vm_fetch_word ();
        uint8_t local_flags = cpu->flags;
        bool s = (local_flags & FLAG_S);
        bool o = (local_flags & FLAG_O);
        if ((local_flags & FLAG_Z) || (!!s != !!o))
          cpu->ip = dest;
        break;
      }
    case 0x26:
      {
        uint16_t dest = vm_fetch_word ();
        uint8_t local_flags = cpu->flags;
        bool s = (local_flags & FLAG_S);
        bool o = (local_flags & FLAG_O);
        if (!(local_flags & FLAG_Z) && (!!s == !!o))
          cpu->ip = dest;
        break;
      }
    case 0x27:
      {
        uint8_t reg_idx = vm_fetch_byte ();
        uint8_t old_sreg = SREG;
        cli ();
        vm_push (cpu->addr_r[reg_idx]);
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x28:
      {
        uint8_t reg = vm_fetch_byte ();
        uint8_t old_sreg = SREG;
        cli ();
        cpu->addr_r[reg] = vm_pop ();
        cpu->last_ip = cpu->ip;
        SREG = old_sreg;
        break;
      }
    case 0x29:
      {
        uint8_t reg = vm_fetch_byte ();
        cpu->ip = cpu->addr_r[reg];
        break;
      }
    case 0x2a:
      {
        int16_t offset = (int16_t) vm_fetch_word ();
        uint16_t temp_ip = cpu->ip;
        temp_ip += offset;
        cpu->ip = temp_ip;
        break;
      }
    default:
      uart_print ("HALT: Unknown Opcode at ");
      uart_println_u16 (cpu->ip - 1);
      break;
    }
  uint8_t end_sreg = SREG;
  cli ();
  cpu->last_ip = cpu->ip;
  SREG = end_sreg;
}
