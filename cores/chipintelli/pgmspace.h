/*
  Unified-memory pgmspace compatibility for ChipIntelli CI13XX.

  Based on the Arduino-ESP32 pgmspace compatibility header.
  Copyright (c) 2015 Hristo Gochkov. All rights reserved.

  This library is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or (at your
  option) any later version.
*/
#ifndef PGMSPACE_INCLUDE
#define PGMSPACE_INCLUDE

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void prog_void;
typedef char prog_char;
typedef unsigned char prog_uchar;
typedef char prog_int8_t;
typedef unsigned char prog_uint8_t;
typedef short prog_int16_t;
typedef unsigned short prog_uint16_t;
typedef long prog_int32_t;
typedef unsigned long prog_uint32_t;

// CI13XX uses a unified address space for sketch constants, so program-memory
// reads are ordinary const loads. These definitions keep AVR-style libraries
// source compatible without pretending there is a separate flash address API.
#define PROGMEM
#define PGM_P const char *
#define PGM_VOID_P const void *
#ifndef PSTR
#define PSTR(s) (s)
#endif
#define _SFR_BYTE(n) (n)

#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#define pgm_read_float(addr) (*(const float *)(addr))
#define pgm_read_ptr(addr) (*(void *const *)(addr))

#define pgm_get_far_address(value) ((uint32_t)(&(value)))

#define pgm_read_byte_near(addr) pgm_read_byte(addr)
#define pgm_read_word_near(addr) pgm_read_word(addr)
#define pgm_read_dword_near(addr) pgm_read_dword(addr)
#define pgm_read_float_near(addr) pgm_read_float(addr)
#define pgm_read_ptr_near(addr) pgm_read_ptr(addr)
#define pgm_read_byte_far(addr) pgm_read_byte(addr)
#define pgm_read_word_far(addr) pgm_read_word(addr)
#define pgm_read_dword_far(addr) pgm_read_dword(addr)
#define pgm_read_float_far(addr) pgm_read_float(addr)
#define pgm_read_ptr_far(addr) pgm_read_ptr(addr)

#define memcmp_P memcmp
#define memccpy_P memccpy
#define memmem_P memmem
#define memcpy_P memcpy
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcat_P strcat
#define strncat_P strncat
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcasecmp_P strcasecmp
#define strncasecmp_P strncasecmp
#define strlen_P strlen
#define strnlen_P strnlen
#define strstr_P strstr
#define printf_P printf
#define sprintf_P sprintf
#define snprintf_P snprintf
#define vsnprintf_P vsnprintf

#endif
