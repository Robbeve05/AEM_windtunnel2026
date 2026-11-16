/* Author: R.E. van Eekelen. Host-only heap diagnostic interface. */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_8BIT 2u
size_t heap_caps_get_free_size(unsigned caps);
size_t heap_caps_get_largest_free_block(unsigned caps);
bool heap_caps_check_integrity_all(bool print_errors);
