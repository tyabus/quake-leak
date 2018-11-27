#include <libc.h>
#include <mach/mach.h>
#include <appkit/appkit.h>
#include <sys/types.h>
#include <sys/dir.h>
#include "quakedef.h"

cvar_t	sys_linerefresh = {"sys_linerefresh","0"};// set for entity display
cvar_t	sys_nostdout = {"sys_nostdout","0"};
cvar_t	sys_nosleep = {"sys_nosleep","0"};

static	char	*basedir = "/raid/quake";
static	char	*cachedir = "/qcache";

void Sys_SetLowFPPrecision (void);
void Sys_SetHighFPPrecision (void);
void Sys_DoSetFPCW (void);


/*
===============================================================================

				REQUIRED SYS FUNCTIONS

===============================================================================
*/

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
	if (mkdir (path, 0777) != -1)
		return;
	if (errno != EEXIST)
		Sys_Error ("mkdir %s: %s",path, strerror(errno)); 
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
		Sys_Error ("Error fstating %s", path);

	return fileinfo.st_size;
}

int Sys_FileOpenWrite (char *path)
{
	int     handle;

	umask (0);
	
	handle = open(path,O_RDWR | O_CREAT | O_TRUNC
	, 0666);

	if (handle == -1)
		Sys_Error ("Error opening %s: %s", path,strerror(errno));

	return handle;
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

int Sys_FileWrite (int handle, void *data, int count)
{
	return write (handle, data, count);
}


/*
============
Sys_SetFPCW
============
*/
void Sys_SetFPCW (void)
{
#if	id386
	Sys_DoSetFPCW ();
#endif
}


#if !id386

/*
================
Sys_LowFPPrecision
================
*/
void Sys_LowFPPrecision (void)
{
// causes weird problems on Nextstep
}


/*
================
Sys_HighFPPrecision
================
*/
void Sys_HighFPPrecision (void)
{
// causes weird problems on Nextstep
}

#endif	// !id386


void Sys_DebugLog(char *file, char *fmt, ...)
{
    va_list argptr; 
    static char data[1024];
    int fd;
    
    va_start(argptr, fmt);
    vsprintf(data, fmt, argptr);
    va_end(argptr);
    fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    write(fd, data, strlen(data));
    close(fd);
}


/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
	kern_return_t r;

	r = vm_protect(task_self(),
				   (vm_address_t)startaddr,
				   length,
				   FALSE,
				   VM_PROT_WRITE | VM_PROT_READ | VM_PROT_EXECUTE);

	if (r != KERN_SUCCESS)
    		Sys_Error("Protection change failed\n");
}


/*
================
Sys_FloatTime
================
*/
double Sys_FloatTime (void)
{
	struct timeval tp;
	struct timezone tzp;
	static int		secbase;

	gettimeofday(&tp, &tzp);
	
	if (!secbase)
	{
		secbase = tp.tv_sec;
		return tp.tv_usec/1000000.0;
	}
	
	return (tp.tv_sec - secbase) + tp.tv_usec/1000000.0;
}

/*
================
Sys_Error
================
*/
void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		string[1024];
	
// change stdin to non blocking
	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);

	va_start (argptr,error);
	vsprintf (string,error,argptr);
	va_end (argptr);
	printf ("Fatal error: %s\n",string);
	
	if (!NXApp)
	{	// appkit isn't running, so don't try to pop up a panel
		Host_Shutdown ();
		exit (1);
	}
	NXRunAlertPanel ("Error",string,NULL,NULL,NULL);
	[NXApp terminate: NULL];
}

/*
================
Sys_Printf
================
*/
void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024], *t_p;
	int			l, r;
	
	if (sys_nostdout.value)
		return;
		
	va_start (argptr,fmt);
	vsprintf (text,fmt,argptr);
	va_end (argptr);
	
	l = strlen(text);
	t_p = text;
	
// make sure everything goes through, even though we are non-blocking
	while (l)
	{
		r = write (1, text, l);
		if (r != l)
			sleep (0);
		if (r > 0)
		{
			t_p += r;
			l -= r;
		}
	}
}

/*
================
Sys_Quit
================
*/
void Sys_Quit (void)
{
	Host_Shutdown();

// change stdin to blocking
	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);

	if (!NXApp)
		exit (0);		// appkit isn't running

	[NXApp terminate:nil];
}


/*
================
Sys_Init
================
*/
void Sys_Init(void)
{

	Sys_SetFPCW ();
}


/*
================
Sys_Sleep
================
*/
void Sys_Sleep (void)
{
	usleep (10);
}


/*
================
Sys_SendKeyEvents

service any pending appkit events
================
*/
void Sys_SendKeyEvents (void)
{
	NXEvent	*event;

	while ( (event = [NXApp
		getNextEvent: 	0xffffffff 
		waitFor:		0 
		threshold:		NX_BASETHRESHOLD] ) )
	{
		[NXApp	sendEvent: event];
	}
}


/*
================
Sys_ConsoleInput

Checks for a complete line of text typed in at the console, then forwards
it to the host command processor
================
*/
char *Sys_ConsoleInput (void)
{
	static char	text[256];
	int		len;

	len = read (0, text, sizeof(text));
	if (len < 1)
		return NULL;
	text[len-1] = 0;	// rip off the /n and terminate
	
	return text;
}

/*
==============================================================================

graphic debugging tools

==============================================================================
*/

id		lineview_i, linewindow_i;
NXRect	linebounds;

int window=-1;

void SV_OpenLineWindow (void)
{
	float	w, h, max;
	float	scale;
	
	if (!NXApp)
	    [Application new];

	
	w = sv.worldmodel->maxs[0] - sv.worldmodel->mins[0] + 32;
	h = sv.worldmodel->maxs[1] - sv.worldmodel->mins[1] + 32;
	max = w>h ? w : h;
	scale = 512/max;
#if 0
	scale = 1.0;
	while (max > 512)
	{
		max /= 2;
		scale /= 2;
	}
#endif	
	NXSetRect (&linebounds, 0,0, w*scale, h*scale);
	
	linewindow_i =
	[[Window alloc]
		initContent:	&linebounds
		style:			NX_TITLEDSTYLE
		backing:		NX_BUFFERED
		buttonMask:		0
		defer:			NO
	];
	
	[linewindow_i display];
	[linewindow_i orderFront: nil];
	lineview_i = [linewindow_i contentView];
	
	[lineview_i setDrawSize: w : h];
		
	[lineview_i 	setDrawOrigin:	sv.worldmodel->mins[0] - 16 : sv.worldmodel->mins[1] - 16];
	
	[lineview_i getBounds: &linebounds];

}

void Sys_Clear (void)
{
	if (!lineview_i)
		return;

	[lineview_i lockFocus];
	NXEraseRect (&linebounds);
	[lineview_i unlockFocus];
}

void Sys_LineRefresh (void)
{
	int		i;
	edict_t	*ent;
	float	*org, *mins, *maxs;
	model_t	*mod;
	
	if (!sv.active)
		return;
		
	if (!lineview_i)
		SV_OpenLineWindow ();
		
	[lineview_i lockFocus];
	NXEraseRect (&linebounds);

	mod = sv.worldmodel;

	PSsetgray (0);
	PSmoveto (mod->mins[0], mod->mins[1]);
	PSlineto (mod->mins[0], mod->maxs[1]);
	PSlineto (mod->maxs[0], mod->maxs[1]);
	PSlineto (mod->maxs[0], mod->mins[1]);		
	PSlineto (mod->mins[0], mod->mins[1]);		

	
	for (i=1 ; i<sv.num_edicts ; i++)
	{
		ent = EDICT_NUM(i);
		if (ent->free)
			continue;
			
		switch ((int)ent->v.solid)
		{
		case SOLID_NOT:
			PSsetrgbcolor (0, 0, 1);
			break;
		
		case SOLID_TRIGGER:
			PSsetrgbcolor (0, 1, 0);
			break;
		
		default:
			PSsetgray (0);
			break;
		}
		
		org = ent->v.origin;
		mins = ent->v.mins;
		maxs = ent->v.maxs;
		
		if (maxs[0] - mins[0] == 0)
		{
			PSarc (org[0], org[1], 8, 0, 360);
		}
		else
		{
			PSmoveto (org[0] + mins[0], org[1] + mins[1]);
			PSlineto (org[0] + mins[0], org[1] + maxs[1]);
			PSlineto (org[0] + maxs[0], org[1] + maxs[1]);
			PSlineto (org[0] + maxs[0], org[1] + mins[1]);		
			PSlineto (org[0] + mins[0], org[1] + mins[1]);		
		}
		PSstroke ();
	}

	PSstroke ();
	
	[lineview_i unlockFocus];
	[linewindow_i flushWindow];
	NXPing ();
}


void SV_DrawLine (vec3_t v1, vec3_t v2)
{
	if (!lineview_i)
		return;
	[lineview_i lockFocus];
	PSmoveto (v1[0],v1[1]);
	PSlineto (v2[0],v2[1]);	
	PSstroke ();
	[lineview_i unlockFocus];
	[linewindow_i flushWindow];
	NXPing ();
}

//============================================================================


/*
=============
main
=============
*/
void main(int argc, char *argv[])
{
	double			time, oldtime, newtime;
	quakeparms_t	parms;
	extern	int		vcrFile;
	extern	int		recording;
	static int		frame;

	moncontrol(0);	// turn off profiling except during real Quake work

// change stdin to non blocking
	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);
	
	parms.memsize = 5861376;
//	parms.memsize = 12*1024*1024;
	parms.membase = malloc (parms.memsize);
	parms.basedir = basedir;
	parms.cachedir = cachedir;

	COM_InitArgv (NXArgc, NXArgv);

	parms.argc = com_argc;
	parms.argv = com_argv;

	Sys_Init ();
			
	Host_Init (&parms);

	Cvar_RegisterVariable (&sys_nostdout);
	Cvar_RegisterVariable (&sys_linerefresh);
	Cvar_RegisterVariable (&sys_nosleep);

//
// main loop
//
	oldtime = Sys_FloatTime () - 0.1;
	while (1)
	{
// find time spent rendering last frame
		newtime = Sys_FloatTime ();
		time = newtime - oldtime;
		
		if (cls.state == ca_dedicated)
		{	// play vcrfiles at max speed
			if (time < sys_ticrate.value && (vcrFile == -1 || recording) )
			{
				if (!sys_nosleep.value)
					usleep (1);
				continue;		// not time to run a server only tic yet
			}
			time = sys_ticrate.value;
		}

		if (time > sys_ticrate.value*2)
			oldtime = newtime;
		else
			oldtime += time;
		
		if (++frame > 10)
			moncontrol(1);		// profile only while we do each Quake frame
		Host_Frame (time);
		moncontrol(0);

// graphic debugging aids
		if (sys_linerefresh.value)
			Sys_LineRefresh ();
	}	
}

