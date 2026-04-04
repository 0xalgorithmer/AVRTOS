# ==============================================================================
# AVRTOS - Advanced AVR Real-Time Operating System & x86-like Virtual Machine
# ==============================================================================
# 
# Copyright (C) 2026 Mohammed Faisal
#
# AVRTOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# AVRTOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with AVRTOS. If not, see <https://www.gnu.org/licenses/>.
# Makefile — GNU-style build for the AVR micro-RTOS.
# Target: ATmega328P @ 16 MHz.
#
# Usage:
#   make          Build the firmware (.hex)
#   make clean    Remove build artefacts
#   make flash    Upload via avrdude (Arduino-Uno bootloader)
#   make size     Show firmware size report

CC      = avr-gcc
OBJCOPY = avr-objcopy
SIZE    = avr-size
AVRDUDE = avrdude

MCU     = atmega328p
F_CPU   = 16000000UL

TARGET  = firmware
SRCDIR  = src

SOURCES = $(SRCDIR)/main.c          \
          $(SRCDIR)/vm_core.c       \
          $(SRCDIR)/scheduler.c     \
          $(SRCDIR)/syscall.c       \
          $(SRCDIR)/gpio_watch.c    \
          $(SRCDIR)/hal/avr_uart.c

OBJECTS = $(SOURCES:.c=.o)

#
#  -O3                          all speed optimizations
#  -flto -fno-fat-lto-objects   link-time optimization (cross-module inlining)
#  -fwhole-program              everything visible to LTO, nothing escapes
#  -funroll-all-loops           unroll every single loop
#  -finline-functions           inline aggressively
#  -finline-limit=10000         raise inline threshold to absurd levels
#  -fno-split-wide-types        keep 16-bit values in register pairs on 8-bit AVR
#  -fno-tree-scev-cprop         avoid AVR-specific pessimization
#  -fgcse-sm / -fgcse-las      global CSE: store motion + load after store
#  -fipa-pta                    interprocedural pointer analysis
#  -fmodulo-sched               software pipelining of inner loops
#  -fmodulo-sched-allow-regmoves  allow reg moves during modulo scheduling
#  -fgraphite-identity          polyhedral loop identity transform
#  -floop-nest-optimize         polyhedral loop nest optimizer (Graphite/ISL)
#  -ftree-loop-distribution     split loops for better scheduling
#  -ftree-partial-pre           partial redundancy elimination on trees
#  -fsched-pressure             register pressure-aware instruction scheduling
#  -freschedule-modulo-scheduled-loops  resched after modulo scheduling
#  -freorder-blocks-algorithm=stc  software trace cache block reordering
#  -fno-semantic-interposition  assume no symbol interposition (stronger inlining)
#  -fmerge-all-constants        merge identical constants across translation units
#  -fomit-frame-pointer         free the Y register for general use
#  -fno-common                  put globals in BSS explicitly (better LTO)
#  -fno-asynchronous-unwind-tables  remove .eh_frame overhead
#  -fno-ident                   strip compiler identification string
#  -fira-loop-pressure          IRA: consider register pressure in loops
#  -fira-hoist-pressure         IRA: factor hoist pressure into decisions

CFLAGS  = -mmcu=$(MCU)                         \
          -DF_CPU=$(F_CPU)                     \
          -O3                                  \
          -std=c11                             \
          -Wall -Wextra -Wpedantic             \
          -funsigned-char                      \
          -funsigned-bitfields                 \
          -fshort-enums                        \
          -flto                                \
          -fno-fat-lto-objects                 \
          -funroll-loops                       \
          -funroll-all-loops                   \
          -finline-functions                   \
          -finline-limit=10000                 \
          -fno-split-wide-types                \
          -fno-tree-scev-cprop                 \
          -fgcse-sm                            \
          -fgcse-las                           \
          -fipa-pta                            \
          -fmodulo-sched                       \
          -fmodulo-sched-allow-regmoves        \
          -ftree-loop-distribution             \
          -ftree-partial-pre                   \
          -fsched-pressure                     \
          -freschedule-modulo-scheduled-loops   \
          -freorder-blocks-algorithm=stc       \
          -fno-semantic-interposition           \
          -fmerge-all-constants                \
          -fomit-frame-pointer                 \
          -fno-common                          \
          -fno-asynchronous-unwind-tables       \
          -fno-ident                           \
          -fira-loop-pressure                  \
          -fira-hoist-pressure                 \
          -ffunction-sections                  \
          -fdata-sections                      \
          -I$(SRCDIR)

# LTO re-optimizes at link time so pass the key flags again.
LDFLAGS = -mmcu=$(MCU)                         \
          -O3                                  \
          -flto                                \
          -fuse-linker-plugin                  \
          -fwhole-program                      \
          -funroll-all-loops                   \
          -finline-limit=10000                 \
          -fno-split-wide-types                \
          -fomit-frame-pointer                 \
          -Wl,--gc-sections                    \
          -Wl,--relax

PROGRAMMER = arduino
PORT       = /dev/ttyACM0
BAUD_RATE  = 115200

.PHONY: all clean flash size

all: $(TARGET).hex size

$(TARGET).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET).elf $(TARGET).hex

flash: $(TARGET).hex
	$(AVRDUDE) -c $(PROGRAMMER) -p $(MCU) -P $(PORT) -b $(BAUD_RATE) -U flash:w:$<:i

size: $(TARGET).elf
	$(SIZE) --mcu=$(MCU) --format=avr $<
