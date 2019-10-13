//
// vregset.c: video register-setting interpreter
//

#include <dos.h>
#include <conio.h>

#include "quakedef.h"
#include "vregset.h"

//#define outportb	loutportb

void loutportb (int port, int val)
{
	printf ("port, val: %x %x\n", port, val);
	getch ();
}

/*
================
VideoRegisterSet
================
*/
void VideoRegisterSet (int *pregset)
{
	int		port, temp0, temp1, temp2;

	for ( ;; )
	{
		switch (*pregset++)
		{
			case VRS_END:
				return;

			case VRS_BYTE_OUT:
				port = *pregset++;
				outportb (port, *pregset++);
				break;

			case VRS_BYTE_RMW:
				port = *pregset++;
				temp0 = *pregset++;
				temp1 = *pregset++;
				temp2 = inportb (port);
				temp2 &= temp0;
				temp2 |= temp1;
				outportb (port, temp2);
				break;

			case VRS_WORD_OUT:
				port = *pregset++;
				outportb (port, *pregset & 0xFF);
				outportb (port+1, *pregset >> 8);
				pregset++;
				break;

			default:
				Sys_Error ("VideoRegisterSet: Invalid command\n");
		}
	}
}

