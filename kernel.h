#ifndef _KERNEL_H_
#define _KERNEL_H_

#define VGA_ADDRESS 0xB8000
#define WHITE_COLOR 15

typedef unsigned short UINT16;

/* VGA state (defined once in kernel.c) */
extern unsigned int VGA_INDEX;
extern UINT16* TERMINAL_BUFFER;

/* Optional: size constants */
#define BUFSIZE 2200

#endif