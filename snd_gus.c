//=============================================================================
// Routines for GUS support in QUAKE
//
// Author(s): Jayeson Lee-Steere
//=============================================================================

#include "quakedef.h"
#include "dosisms.h"

//=============================================================================
// Author(s): Jayeson Lee-Steere

#define INI_STRING_SIZE 0x100

FILE *ini_fopen(const char *filename, const char *modes);
int ini_fclose(FILE *f);
void ini_fgets(FILE *f, const char *section, const char *field, char *s);

// Routines for reading from .INI files
// The read routines are fairly efficient.
//
// Author(s): Jayeson Lee-Steere

#define MAX_SECTION_WIDTH 20
#define MAX_FIELD_WIDTH 20

#define NUM_SECTION_BUFFERS 10
#define NUM_FIELD_BUFFERS 20

struct section_buffer
{
   long offset;
   char name[MAX_SECTION_WIDTH+1];
};

struct field_buffer
{
   long offset;
   int  section;
   char name[MAX_FIELD_WIDTH+1];
};

static FILE *current_file=NULL;
static int   current_section;

static int current_section_buffer=0;
static int current_field_buffer=0;

static struct section_buffer section_buffers[NUM_SECTION_BUFFERS];
static struct field_buffer field_buffers[NUM_FIELD_BUFFERS];
//***************************************************************************
// Internal routines
//***************************************************************************
static char toupper(char c)
{
   if (c>='a' && c<='z')
      c-=('a'-'A');
   return(c);
}

static void reset_buffer(FILE *f)
{
   int i;

   for (i=0;i<NUM_SECTION_BUFFERS;i++)
      section_buffers[i].name[0]=0;
   for (i=0;i<NUM_FIELD_BUFFERS;i++)
      field_buffers[i].name[0]=0;

   current_file=f;
}

// Sees if the current string is section "name" (i.e. ["name"]).
// If "name"=="*", sees if the current string is any section
// (i.e. [....]). Returns 1 if true else 0 if false.
static int is_section(char *s,const char *name)
{
   int wild=0;

   // See if wild search
   if (strcmp("*",name)==0)
      wild=1;

   // Skip leading spaces
   while (s[0]==' ')
      s++;
   // Look for leading "["
   if (s[0]!='[')
      return(0);
   s++;
   // Make sure name matches
   while (s[0]!=']' && s[0]!=13 && s[0]!=10 && s[0]!=0 && name[0]!=0)
   {
      if (!wild)
         if (toupper(s[0])!=toupper(name[0]))
            return(0);
      s++;
      if (!wild)
         name++;
   }
   if (!wild)
      if (name[0]!=0)
         return(0);
   // Skip trailing spaces
   while (s[0]==' ')
      s++;
   // Make sure we have trailing "]"
   if (s[0]!=']')
      return(0);
   return(1);
}

// Sees if the current string is field "name" (i.e. "name"=...).
// If "name"=="*", sees if the current string is any field
// (i.e. ...=...). Returns 1 if true else 0 if false.
static int is_field(char *s,const char *name)
{
   int wild=0;

   // See if wild search
   if (strcmp("*",name)==0)
      wild=1;

   // Skip leading spaces
   while (s[0]==' ')
      s++;

   // Make sure name matches
   while (s[0]!='=' && s[0]!=13 && s[0]!=10 && s[0]!=0 && name[0]!=0)
   {
      if (!wild)
         if (toupper(s[0])!=toupper(name[0]))
            return(0);
      s++;
      if (!wild)
         name++;
   }
   if (!wild)
      if (name[0]!=0)
         return(0);
   // Skip trailing spaces
   while (s[0]==' ')
      s++;
   // Make sure we have an "="
   if (s[0]!='=')
      return(0);

   return(1);
}

// Extracts the section name from a section heading
// e.g. in="[hey man]" gives out="hey man"
static void get_section_name(char *out, char *in)
{
   int i=0;

   // Skip spaces before '['
   while (in[0]==' ')
      in++;
   // Make sure there is a '['
   if (in[0]!='[')
   {
      out[0]=0;
      return;
   }
   // Skip past '['
   in++;
   // Copy string if any to output string.
   while (in[0]!=']' && in[0]!=13 && in[0]!=10 && in[0]!=0)
   {
      if (i<MAX_SECTION_WIDTH)
      {
         out[i]=in[0];
         i++;
      }
      in++;
   }
   // Make sure string was terminated with ']'
   if (in[0]!=']')
   {
      out[0]=0;
      return;
   }
   // Remove trailing spaces
   while (i>0 && out[i-1]==' ')
      i--;
   // Null terminate the output string.
   out[i]=0;
}

// Extracts the field name from a field line
// e.g. in="sooty=life be in it" gives out="sooty"
static void get_field_name(char *out, char *in)
{
   int i=0;

   // Skip leading spaces
   while (in[0]==' ')
      in++;
   // Copy name to output string
   while (in[0]!='=' && in[0]!=13 && in[0]!=10 && in[0]!=0)
   {
      if (i<MAX_FIELD_WIDTH)
      {
         out[i]=in[0];
         i++;
      }
      in++;
   }
   // Make sure we stopped on "="
   if (in[0]!='=')
   {
      out[0]=0;
      return;
   }
   // Remove trailing spaces
   while (i>0 && out[i-1]==' ')
      i--;
   // Null terminate the output string.
   out[i]=0;
}

// Returns the field data from string s.
// e.g. in="wally = golly man" gives out="golly man"
static void get_field_string(char *out, char *in)
{
   int i=0;

   // Find '=' if it exists
   while (in[0]!='=' && in[0]!=13 && in[0]!=10 && in[0]!=0)
      in++;
   // If there is an '=', skip past it.
   if (in[0]=='=')
      in++;
   // Skip any spaces between the '=' and string.
   while (in[0]==' ' || in[0]=='[')
      in++;
   // Copy string, if there is one, to the output string.
   while (in[0]!=13 && in[0]!=10 && in[0]!=0 && i<(INI_STRING_SIZE-1))
   {
      out[i]=in[0];
      in++;
      i++;
   }
   // Null terminate the output string.
   out[i]=0;
}

// Adds a section to the buffer
static int add_section(char *instring, long offset)
{
   int i;
   char section[MAX_SECTION_WIDTH+1];

   // Extract section name
   get_section_name(section,instring);
   // See if section already exists.
   for (i=0;i<NUM_SECTION_BUFFERS;i++)
      if (stricmp(section,section_buffers[i].name)==0)
         return(i);
   // Increment current_section_buffer
   current_section_buffer++;
   if (current_section_buffer>NUM_SECTION_BUFFERS)
      current_section_buffer=0;
   // Delete any field buffers that correspond to this section
   for (i=0;i<NUM_FIELD_BUFFERS;i++)
      if (field_buffers[i].section==current_section_buffer)
         field_buffers[i].name[0]=0;
   // Set buffer information
   strcpy(section_buffers[current_section_buffer].name,section);
   section_buffers[current_section_buffer].offset=offset;
   return(current_section_buffer);
}

// Adds a field to the buffer
static void add_field(char *instring, int section, long offset)
{
   int i;
   char field[MAX_FIELD_WIDTH+1];

   // Extract field name
   get_field_name(field,instring);
   // See if field already exists
   for (i=0;i<NUM_FIELD_BUFFERS;i++)
      if (field_buffers[i].section==section)
         if (stricmp(field_buffers[i].name,field)==0)
            return;
   // Increment current_field_buffer
   current_field_buffer++;
   if (current_field_buffer>NUM_FIELD_BUFFERS)
      current_field_buffer=0;
   // Set buffer information
   strcpy(field_buffers[current_field_buffer].name,field);
   field_buffers[current_field_buffer].section=section;
   field_buffers[current_field_buffer].offset=offset;
}

// Identical to fgets except the string is trucated at the first ';',
// carriage return or line feed.
static char *stripped_fgets(char *s, int n, FILE *f)
{
   int i=0;

   if (fgets(s,n,f)==NULL)
      return(NULL);

   while (s[i]!=';' && s[i]!=13 && s[i]!=10 && s[i]!=0)
      i++;
   s[i]=0;

   return(s);
}

//***************************************************************************
// Externally accessable routines
//***************************************************************************
// Opens an .INI file. Works like fopen
FILE *ini_fopen(const char *filename, const char *modes)
{
   return(fopen(filename,modes));
}

// Closes a .INI file. Works like fclose
int ini_fclose(FILE *f)
{
   if (f==current_file)
      reset_buffer(NULL);
   return(fclose(f));
}

// Puts "field" from "section" from .ini file "f" into "s".
// If "section" does not exist or "field" does not exist in
// section then s="";
void ini_fgets(FILE *f, const char *section, const char *field, char *s)
{
   int i;
   long start_pos,string_start_pos;
   char ts[INI_STRING_SIZE*2];

   if (f!=current_file)
      reset_buffer(f);

   // Default to "Not found"
   s[0]=0;

   // See if section is in buffer
   for (i=0;i<NUM_SECTION_BUFFERS;i++)
      if (strnicmp(section_buffers[i].name,section,MAX_SECTION_WIDTH)==0)
         break;

   // If section is in buffer, seek to it if necessary
   if (i<NUM_SECTION_BUFFERS)
   {
      if (i!=current_section)
      {
         current_section=i;
         fseek(f,section_buffers[i].offset,SEEK_SET);
      }
   }
   // else look through .ini file for it.
   else
   {
      // Make sure we are not at eof or this will cause trouble.
      if (feof(f))
         rewind(f);
      start_pos=ftell(f);
      while (1)
      {
         stripped_fgets(ts,INI_STRING_SIZE*2,f);
         // If it is a section, add it to the section buffer
         if (is_section(ts,"*"))
            current_section=add_section(ts,ftell(f));
         // If it is the section we are looking for, break.
         if (is_section(ts,section))
            break;
         // If we reach the end of the file, rewind to the start.
         if (feof(f))
            rewind(f);
         if (ftell(f)==start_pos)
            return;
      }
   }

   // See if field is in buffer
   for (i=0;i<NUM_FIELD_BUFFERS;i++)
      if (field_buffers[i].section==current_section)
         if (strnicmp(field_buffers[i].name,field,MAX_FIELD_WIDTH)==0)
            break;

   // If field is in buffer, seek to it and read it
   if (i<NUM_FIELD_BUFFERS)
   {
      fseek(f,field_buffers[i].offset,SEEK_SET);
      stripped_fgets(ts,INI_STRING_SIZE*2,f);
      get_field_string(s,ts);
   }
   else
   // else search through section for field.
   {
      // Make sure we do not start at eof or this will cause problems.
      if (feof(f))
         fseek(f,section_buffers[current_section].offset,SEEK_SET);
      start_pos=ftell(f);
      while (1)
      {
         string_start_pos=ftell(f);
         stripped_fgets(ts,INI_STRING_SIZE*2,f);
         // If it is a field, add it to the buffer
         if (is_field(ts,"*"))
            add_field(ts,current_section,string_start_pos);
         // If it is the field we are looking for, save it
         if (is_field(ts,field))
         {
            get_field_string(s,ts);
            break;
         }
         // If we reach the end of the section, start over
         if (feof(f) || is_section(ts,"*"))
            fseek(f,section_buffers[current_section].offset,SEEK_SET);
         if (ftell(f)==start_pos)
            return;
      }
   }
}

//=============================================================================

#define BYTE unsigned char
#define WORD unsigned short
#define DWORD unsigned long

#define BUFFER_SIZE 4096

#define CODEC_ADC_INPUT_CONTROL_LEFT	0x00
#define CODEC_ADC_INPUT_CONTROL_RIGHT	0x01
#define CODEC_AUX1_INPUT_CONTROL_LEFT	0x02
#define CODEC_AUX1_INPUT_CONTROL_RIGHT	0x03
#define CODEC_AUX2_INPUT_CONTROL_LEFT	0x04
#define CODEC_AUX2_INPUT_CONTROL_RIGHT	0x05
#define CODEC_DAC_OUTPUT_CONTROL_LEFT	0x06
#define CODEC_DAC_OUTPUT_CONTROL_RIGHT	0x07
#define CODEC_FS_FORMAT			0x08
#define CODEC_INTERFACE_CONFIG		0x09
#define CODEC_PIN_CONTROL		0x0A
#define CODEC_ERROR_STATUS_AND_INIT	0x0B
#define CODEC_MODE_AND_ID		0x0C
#define CODEC_LOOPBACK_CONTROL		0x0D
#define CODEC_PLAYBACK_UPPER_BASE_COUNT	0x0E
#define CODEC_PLAYBACK_LOWER_BASE_COUNT	0x0F

struct CodecRateStruct
{
   WORD Rate;
   BYTE FSVal;
};

//=============================================================================
// Reference variables in SND_DOS.C
//=============================================================================
extern short *dma_buffer;
extern dma_t theshm;

//=============================================================================
// GUS-only variables
//=============================================================================
static WORD CodecRegisterSelect;
static WORD CodecData;
static WORD CodecStatus;

static BYTE DmaChannel;

static BYTE PageRegs[] = { 0x87, 0x83, 0x81, 0x82, 0x8f, 0x8b, 0x89, 0x8a };
static BYTE AddrRegs[] = { 0, 2, 4, 6, 0xc0, 0xc4, 0xc8, 0xcc };
static BYTE CountRegs[] = { 1, 3, 5, 7, 0xc2, 0xc6, 0xca, 0xce };

static WORD AddrReg;
static WORD CountReg;
static WORD ModeReg;
static WORD DisableReg;
static WORD ClearReg;

static struct CodecRateStruct CodecRates[]=
{
   { 5512,0x01},
   { 6620,0x0F},
   { 8000,0x00},
   { 9600,0x0E},
   {11025,0x03},
   {16000,0x02},
   {18900,0x05},
   {22050,0x07},
   {27420,0x04},
   {32000,0x06},
   {33075,0x0D},
   {37800,0x09},
   {44100,0x0B},
   {48000,0x0C},
   {    0,0x00} // End marker
};

//=============================================================================
// Get Interwave (UltraSound PnP) configuration if any
//=============================================================================
static qboolean GUS_GetIWData(void)
{
   char *Interwave,s[INI_STRING_SIZE];
   FILE *IwFile;
   int  CodecBase,CodecDma,i;

   Interwave=getenv("INTERWAVE");
   if (Interwave==NULL)
      return(false);

   // Open IW.INI
   IwFile=ini_fopen(Interwave,"rt");
   if (IwFile==NULL)
      return(false);

   // Read codec base and codec DMA
   ini_fgets(IwFile,"setup 0","CodecBase",s);
   sscanf(s,"%X",&CodecBase);
   ini_fgets(IwFile,"setup 0","DMA2",s);
   sscanf(s,"%i",&CodecDma);

   ini_fclose(IwFile);

   // Make sure numbers OK
   if (CodecBase==0 || CodecDma==0)
      return(false);

   CodecRegisterSelect=CodecBase;
   CodecData=CodecBase+1;
   CodecStatus=CodecBase+2;
   DmaChannel=CodecDma;

   // Make sure there is a CODEC at the CODEC base

   // Clear any pending IRQs
   dos_inportb(CodecStatus);
   dos_outportb(CodecStatus,0);

   // Wait for 'INIT' bit to clear
   for (i=0;i<0xFFFF;i++)
      if ((dos_inportb(CodecRegisterSelect) & 0x80) == 0)
         break;
   if (i==0xFFFF)
      return(false);

   // Get chip revision - can not be zero
   dos_outportb(CodecRegisterSelect,CODEC_MODE_AND_ID);
   if ((dos_inportb(CodecRegisterSelect) & 0x7F) != CODEC_MODE_AND_ID)
      return(false);
   if ((dos_inportb(CodecData) & 0x0F) == 0)
      return(false);

   return(true);
}

//=============================================================================
// Get UltraSound MAX configuration if any
//=============================================================================
static qboolean GUS_GetMAXData(void)
{
   char *Ultrasnd,*Ultra16;
   int  i;
   int  GusBase,Dma1,Dma2,Irq1,Irq2;
   int  CodecBase,CodecDma,CodecIrq,CodecType;
   BYTE MaxVal;

   Ultrasnd=getenv("ULTRASND");
   Ultra16=getenv("ULTRA16");
   if (Ultrasnd==NULL || Ultra16==NULL)
      return(false);

   sscanf(Ultrasnd,"%x,%i,%i,%i,%i",&GusBase,&Dma1,&Dma2,&Irq1,&Irq2);
   sscanf(Ultra16,"%x,%i,%i,%i",&CodecBase,&CodecDma,&CodecIrq,&CodecType);

   if (CodecType==0 && CodecDma!=0)
      DmaChannel=CodecDma & 0x07;
   else
      DmaChannel=Dma2 & 0x07;

   // Make sure there is a GUS at GUS base
   dos_outportb(GusBase+0x08,0x55);
   if (dos_inportb(GusBase+0x0A)!=0x55)
      return(false);
   dos_outportb(GusBase+0x08,0xAA);
   if (dos_inportb(GusBase+0x0A)!=0xAA)
      return(false);

   // Program CODEC control register
   MaxVal=((CodecBase & 0xF0)>>4) | 0x40;
   if (Dma1 > 3)
      MaxVal|=0x10;
   if (Dma2 > 3)
      MaxVal|=0x20;
   dos_outportb(GusBase+0x106,MaxVal);

   CodecRegisterSelect=CodecBase;
   CodecData=CodecBase+1;
   CodecStatus=CodecBase+2;

   // Make sure there is a CODEC at the CODEC base

   // Clear any pending IRQs
   dos_inportb(CodecStatus);
   dos_outportb(CodecStatus,0);

   // Wait for 'INIT' bit to clear
   for (i=0;i<0xFFFF;i++)
      if ((dos_inportb(CodecRegisterSelect) & 0x80) == 0)
         break;
   if (i==0xFFFF)
      return(false);

   // Get chip revision - can not be zero
   dos_outportb(CodecRegisterSelect,CODEC_MODE_AND_ID);
   if ((dos_inportb(CodecRegisterSelect) & 0x7F) != CODEC_MODE_AND_ID)
      return(false);
   if ((dos_inportb(CodecData) & 0x0F) == 0)
      return(false);

   return(true);
}

//=============================================================================
// Programs the DMA controller to start DMAing in Auto-init mode
//=============================================================================
static void GUS_StartDMA(BYTE DmaChannel,short *dma_buffer,int count)
{
   int mode;
   int RealAddr;

   RealAddr = ptr2real(dma_buffer);

   if (DmaChannel <= 3)
   {
      ModeReg = 0x0B;
      DisableReg = 0x0A;
      ClearReg = 0x0E;
   }
   else
   {
      ModeReg = 0xD6;
      DisableReg = 0xD4;
      ClearReg = 0xDC;
   }
   CountReg=CountRegs[DmaChannel];
   AddrReg=AddrRegs[DmaChannel];

   dos_outportb(DisableReg, DmaChannel | 4);	// disable channel

   // set mode- see "undocumented pc", p.876
   mode = (1<<6)	        // single-cycle
          +(0<<5)	        // address increment
	  +(1<<4)	        // auto-init dma
	  +(2<<2)	        // read
	  +(DmaChannel & 0x03);	// channel #
   dos_outportb(ModeReg, mode);

   // set page
   dos_outportb(PageRegs[DmaChannel], RealAddr >> 16);

   if (DmaChannel <= 3)
   {	// address is in bytes
      dos_outportb(0x0C, 0);		// prepare to send 16-bit value
      dos_outportb(AddrReg, RealAddr & 0xff);
      dos_outportb(AddrReg, (RealAddr>>8) & 0xff);

      dos_outportb(0x0C, 0);		// prepare to send 16-bit value
      dos_outportb(CountReg, (count-1) & 0xff);
      dos_outportb(CountReg, (count-1) >> 8);
   }
   else
   {	// address is in words
      dos_outportb(0xD8, 0);	        // prepare to send 16-bit value
      dos_outportb(AddrReg, (RealAddr>>1) & 0xff);
      dos_outportb(AddrReg, (RealAddr>>9) & 0xff);

      dos_outportb(0xD8, 0);		// prepare to send 16-bit value
      dos_outportb(CountReg, ((count>>1)-1) & 0xff);
      dos_outportb(CountReg, ((count>>1)-1) >> 8);
   }

   dos_outportb(ClearReg, 0);		// clear write mask
   dos_outportb(DisableReg, DmaChannel & ~4);
}

//=============================================================================
// Starts the CODEC playing
//=============================================================================
static void GUS_StartCODEC(int count,BYTE FSVal)
{
   int i,j;

   // Clear any pending IRQs
   dos_inportb(CodecStatus);
   dos_outportb(CodecStatus,0);

   // Set mode to 2
   dos_outportb(CodecRegisterSelect,CODEC_MODE_AND_ID);
   dos_outportb(CodecData,0xC0);

   // Stop any playback or capture which may be happening
   dos_outportb(CodecRegisterSelect,CODEC_INTERFACE_CONFIG);
   dos_outportb(CodecData,dos_inportb(CodecData) & 0xFC);

   // Set FS
   dos_outportb(CodecRegisterSelect,CODEC_FS_FORMAT | 0x40);
   dos_outportb(CodecData,FSVal | 0x50); // Or in stereo and 16 bit bits

   // Wait a bit
   for (i=0;i<10;i++)
      dos_inportb(CodecData);

   // Routine 1 to counter CODEC bug - wait for init bit to clear and then a
   // bit longer (i=min loop count, j=timeout
   for (i=0,j=0;i<1000 && j<0x7FFFF;j++)
      if ((dos_inportb(CodecRegisterSelect) & 0x80)==0)
         i++;

   // Routine 2 to counter CODEC bug - this is from Forte's code. For me it
   // does not seem to cure the problem, but is added security
   // Waits till we can modify index register
   for (j=0;j<0x7FFFF;j++)
   {
      dos_outportb(CodecRegisterSelect,CODEC_INTERFACE_CONFIG | 0x40);
      if (dos_inportb(CodecRegisterSelect)==(CODEC_INTERFACE_CONFIG | 0x40))
         break;
   }

   // Perform ACAL
   dos_outportb(CodecRegisterSelect,CODEC_INTERFACE_CONFIG | 0x40);
   dos_outportb(CodecData,0x08);

   // Clear MCE bit - this makes ACAL happen
   dos_outportb(CodecRegisterSelect,CODEC_INTERFACE_CONFIG);

   // Wait for ACAL to finish
   for (j=0;j<0x7FFFF;j++)
   {
      if ((dos_inportb(CodecRegisterSelect) & 0x80) != 0)
         continue;
      dos_outportb(CodecRegisterSelect,CODEC_ERROR_STATUS_AND_INIT);
      if ((dos_inportb(CodecData) & 0x20) == 0)
         break;
   }

   // Clear ACAL bit
   dos_outportb(CodecRegisterSelect,CODEC_INTERFACE_CONFIG | 0x40);
   dos_outportb(CodecData,0x00);
   dos_outportb(CodecRegisterSelect,CODEC_INTERFACE_CONFIG);

   // Set some other junk
   dos_outportb(CodecRegisterSelect,CODEC_LOOPBACK_CONTROL);
   dos_outportb(CodecData,0x00);
   dos_outportb(CodecRegisterSelect,CODEC_PIN_CONTROL);
   dos_outportb(CodecData,0x08); // IRQ is disabled in PIN control

   // Set count (it doesn't really matter what value we stuff in here
   dos_outportb(CodecRegisterSelect,CODEC_PLAYBACK_LOWER_BASE_COUNT);
   dos_outportb(CodecData,count & 0xFF);
   dos_outportb(CodecRegisterSelect,CODEC_PLAYBACK_UPPER_BASE_COUNT);
   dos_outportb(CodecData,count >> 8);

   // Start playback
   dos_outportb(CodecRegisterSelect,CODEC_INTERFACE_CONFIG);
   dos_outportb(CodecData,0x01);
}

//=============================================================================
// Figures out what kind of UltraSound we have, if any, and starts it playing
//=============================================================================
qboolean GUS_Init(void)
{
   int rc;
   int RealAddr;
   BYTE FSVal;
   struct CodecRateStruct *CodecRate;

   // See what kind of UltraSound we have, if any
   if (GUS_GetIWData()==false)
      if (GUS_GetMAXData()==false)
         return(false);

   shm = &theshm;

   // do 11khz sampling rate unless command line parameter wants different
   shm->speed = 11025;
   FSVal = 0x02;
   rc = COM_CheckParm("-sspeed");
   if (rc)
   {
      shm->speed = Q_atoi(com_argv[rc+1]);

      // Make sure rate not too high
      if (shm->speed>48000)
         shm->speed=48000;

      // Adjust speed to match one of the possible CODEC rates
      for (CodecRate=CodecRates;CodecRate->Rate!=0;CodecRate++)
      {
         if (shm->speed <= CodecRate->Rate)
         {
            shm->speed=CodecRate->Rate;
            FSVal=CodecRate->FSVal;
            break;
         }
      }
   }

   // Always do 16 bit stereo
   shm->channels = 2;
   shm->samplebits = 16;

   // allocate buffer twice the size we need so we can get aligned buffer
   dma_buffer = dos_getmemory(BUFFER_SIZE*2);
   if (dma_buffer==NULL)
   {
      Con_Printf("Couldn't allocate sound dma buffer");
      return false;
   }

   RealAddr = ptr2real(dma_buffer);
   RealAddr = (RealAddr + BUFFER_SIZE) & ~(BUFFER_SIZE-1);
   dma_buffer = (short *) real2ptr(RealAddr);

   // Zero off DMA buffer
   memset(dma_buffer, 0, BUFFER_SIZE);

   shm->soundalive = true;
   shm->splitbuffer = false;

   shm->samplepos = 0;
   shm->submission_chunk = 1;
   shm->buffer = (unsigned char *) dma_buffer;
   shm->samples = BUFFER_SIZE/(shm->samplebits/8);

   GUS_StartDMA(DmaChannel,dma_buffer,BUFFER_SIZE);
   GUS_StartCODEC(BUFFER_SIZE,FSVal);

   return(true);
}

//=============================================================================
// Returns the current playback position
//=============================================================================
int GUS_GetDMAPos(void)
{
   int count;

   // clear 16-bit reg flip-flop
   // load the current dma count register
   if (DmaChannel < 4)
   {
      dos_outportb(0x0C, 0);
      count = dos_inportb(CountReg);
      count += dos_inportb(CountReg) << 8;
      if (shm->samplebits == 16)
         count /= 2;
      count = shm->samples - (count+1);
   }
   else
   {
      dos_outportb(0xD8, 0);
      count = dos_inportb(CountReg);
      count += dos_inportb(CountReg) << 8;
      if (shm->samplebits == 8)
         count *= 2;
      count = shm->samples - (count+1);
   }

   shm->samplepos = count & (shm->samples-1);
   return(shm->samplepos);
}

//=============================================================================
// Stops the UltraSound playback
//=============================================================================
void GUS_Shutdown (void)
{
   // Stop CODEC
   dos_outportb(CodecRegisterSelect,CODEC_INTERFACE_CONFIG);
   dos_outportb(CodecData,0x01);

   dos_outportb(DisableReg, DmaChannel | 4); // disable dma channel
}
