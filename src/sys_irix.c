#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>

#include "quakedef.h"

#define TICRATE (1.0/30)

FILE *debugfile=0;

// =======================================================================
// General routines
// =======================================================================

void I_DebugNumber(int y, int val)
{
}

/*
void I_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	
	va_start (argptr,fmt);
	vsprintf (text,fmt,argptr);
	va_end (argptr);
	fprintf(stderr, "%s", text);
	
	Con_Print (text);
}
*/

void I_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024], *t_p;
	int			l, r;
	
	va_start (argptr,fmt);
	vsprintf (text,fmt,argptr);
	va_end (argptr);
	
	l = strlen(text);
	t_p = text;
	
// make sure everything goes through, even though we are non-blocking
	while (l)
	{
		r = write (1, text, l);
		if (r == -1)
		{
			sleep (0);
			continue;
		}
		t_p += r;
		l -= r;
	}
}

void I_Quit (void)
{
    fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
	S_Shutdown();
	exit(0);
}

void I_Error (char *error, ...)
{ 
    va_list     argptr;
    char        string[1024];
    
    fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
    va_start (argptr,error);
    vsprintf (string,error,argptr);
    va_end (argptr);
	fprintf(stderr, "Error: %s\n", string);
	S_Shutdown();
	VID_Shutdown();
	exit(-1);
} 

void I_Warn (char *warning, ...)
{ 
    va_list     argptr;
    char        string[1024];
    
    va_start (argptr,warning);
    vsprintf (string,warning,argptr);
    va_end (argptr);
	fprintf(stderr, "Warning: %s", string);
} 

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int	Sys_FileTime (char *path)
{
	struct	stat	buf;
	
	if (stat (path,&buf) == -1)
		return -1;
	
	return buf.st_mtime;
}


void Sys_mkdir (char *path)
{
	mkdir (path, 0777);
}

int Sys_FileOpenRead (char *path, int *handle)
{
	int	h;
	struct stat	fileinfo;
    
	
	h = open (path, O_RDONLY, 0666);
	*handle = h;
	if (h == -1)
		return -1;
	
	if (fstat (h,&fileinfo) == -1)
		I_Error ("Error fstating %s", path);

	return fileinfo.st_size;
}

int Sys_FileOpenWrite (char *path)
{
	int     handle;

	umask (0);
	
	handle = open(path,O_RDWR | O_CREAT | O_TRUNC
	, 0666);

	if (handle == -1)
		I_Error ("Error opening %s: %s", path,strerror(errno));

	return handle;
}

int Sys_FileWrite (int handle, void *src, int count)
{
	return write (handle, src, count);
}

void Sys_FileClose (int handle)
{
	close (handle);
}

void Sys_FileSeek (int handle, int position)
{
	lseek (handle, position, SEEK_SET);
}

int Sys_FileRead (int handle, void *dest, int count)
{
	return read (handle, dest, count);
}

void I_DebugLog(char *file, char *fmt, ...)
{
    va_list argptr; 
    static char data[1024];
    int fd;
    
    va_start(argptr, fmt);
    vsprintf(data, fmt, argptr);
    va_end(argptr);
//    fd = open(file, O_WRONLY | O_BINARY | O_CREAT | O_APPEND, 0666);
    fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    write(fd, data, strlen(data));
    close(fd);
}

/*=============
StartMSRInterval
=============   
*/
void StartMSRInterval(int msreg)
{ 
}

/*
=============
EndMSRInterval
=============
*/ 
unsigned long EndMSRInterval()
{ 
    return 0;
}

cvar_t	sys_showfiles = {"showfiles","1"};


void Sys_EditFile(char *filename)
{

	char cmd[256];
	char *term;
	char *editor;

	term = getenv("TERM");
	if (term && !strcmp(term, "xterm"))
	{
		editor = getenv("VISUAL");
		if (!editor)
			editor = getenv("EDITOR");
		if (!editor)
			editor = getenv("EDIT");
		if (!editor)
			editor = "vi";
		sprintf(cmd, "xterm -e %s %s", editor, filename);
		system(cmd);
	}

}

double I_FloatTime (void)
{
    struct timeval tp;
    struct timezone tzp; 
    static int      secbase; 
    
    gettimeofday(&tp, &tzp);  

    if (!secbase)
    {
        secbase = tp.tv_sec;
        return tp.tv_usec/1000000.0;
    }

    return (tp.tv_sec - secbase) + tp.tv_usec/1000000.0;
}

// =======================================================================
// Sleeps for microseconds
// =======================================================================

static volatile int oktogo;

void alarm_handler(int x)
{
	oktogo=1;
}

void I_Sleep(int usecs)
{

	struct itimerval it;

	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = usecs / 1000000;
	it.it_value.tv_usec = usecs % 1000000;

	signal(SIGALRM, alarm_handler);

	oktogo=0;
	setitimer(ITIMER_REAL, &it, 0);
	while (!oktogo) sleep(0);

}

byte *I_ZoneBase (int *size)
{

	char *QUAKEOPT = getenv("QUAKEOPT");

	*size = 0xc00000;
	if (QUAKEOPT)
	{
		while (*QUAKEOPT)
			if (tolower(*QUAKEOPT++) == 'm')
			{
				*size = atof(QUAKEOPT) * 1024*1024;
				break;
			}
	}
	return malloc (*size);

}

void SV_LineRefresh(void)
{
}

void Sys_Sleep(void)
{
	sleep(0);
}

void I_GetMemory(quakeparms_t *parms)
{
    FILE *f;
    char buffer[256];
	char *procparse;
    int freemem, buffermem, totalmem;
    int rc, suggestion;

	parms->memsize = 8*1024*1024;

    f = fopen("/proc/meminfo", "r");
    if (f)
	{
		fgets(buffer, sizeof buffer, f);
		procparse = "%s %d %d %d %d %d";
		rc = fscanf(f, procparse, buffer, &totalmem, buffer, &freemem,
		  buffer, &buffermem);

		suggestion = (9*buffermem)/10 + freemem;
		if (suggestion > totalmem)
			I_Printf("[%s] did not properly parse /proc/meminfo\n", procparse);
		if (suggestion > parms->memsize && suggestion < totalmem)
			parms->memsize = suggestion;

		fclose(f);
	}
	else
		I_Printf("Did you know /proc breaks up painful gas bubbles?\n");

	parms->membase = malloc (parms->memsize);

}


void floating_point_exception_handler(int whatever)
{
//	I_Warn("floating point exception\n");
	signal(SIGFPE, floating_point_exception_handler);
}

int main (int c, char **v)
{

	extern qboolean pr_debugerrors;
	double		time, oldtime, newtime;
	quakeparms_t parms;

	pr_debugerrors = false;

	signal(SIGFPE, floating_point_exception_handler);

	fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);

	parms.memsize = 0x2000000;
	parms.membase = malloc (parms.memsize);
	parms.basedir = "/flem/quake";

	COM_InitArgv (c, v);

	parms.argc = com_argc;
	parms.argv = com_argv;

    Quake_Init(&parms);
		
	if (COM_CheckParm ("-debugfile"))
	{
		char	hostname[80];
		char	filename[80];
		
		gethostname(hostname, sizeof(hostname));
		sprintf (filename, "%s.dbg",hostname);
		debugfile = fopen (filename,"w");

		net_debug = true;
	}

    oldtime = I_FloatTime ();
    while (1)
    {
// find time spent rendering last frame
        newtime = I_FloatTime ();
        time = newtime - oldtime;

//        if ( !cl_connected && time < TICRATE)
        if ( host_dedicated && time < TICRATE)
        {
            I_Sleep (1);
            continue;       // not time to run a server only tic yet
        }

        Host_Frame(time);
//        CL_Frame(time);

        oldtime = newtime;
    }

    Quake_Shutdown();

}


/*
================
I_MakeCodeWriteable
================
*/
void I_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{

	int r;
	unsigned int addr;
	int psize = getpagesize();

	addr = startaddr & ~(psize-1);

	r = mprotect((char*)addr, length + startaddr - addr, 7);

	if (r < 0)
    		I_Error("Protection change failed\n");

}
