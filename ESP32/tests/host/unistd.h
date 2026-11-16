/* Author: R.E. van Eekelen. Host-only serial console interface. */
#pragma once
#include <stddef.h>
#define STDIN_FILENO 0
int read(int fd, void *buffer, size_t size);
