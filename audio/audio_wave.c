 ///////////////////////////////////////////////
//
// **************************
//
// Project/Software name: X-Phone
// Author: "Van Gegel" <gegelcopy@ukr.net>
//
// THIS IS A FREE SOFTWARE  AND FOR TEST ONLY!!!
// Please do not use it in the case of life and death
// This software is released under GNU LGPL:
//
// * LGPL 3.0 <http://www.gnu.org/licenses/lgpl.html>
//
// You’re free to copy, distribute and make commercial use
// of this software under the following conditions:
//
// * You have to cite the author (and copyright owner): Van Gegel
// * You have to provide a link to the author’s Homepage: <http://torfone.org>
//
///////////////////////////////////////////////


//This file contains low-level procedure for wave audio
//Win32 system for X-Phone (PairPhone) project

//FOR WIN32 ONLY!

#include <basetsd.h>
#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ophmconsts.h"

//#include "cntrls.h"
#include "audio_wave.h"
#define UNREF(x) (void)(x)

#define DEFCONF "conf.txt"  //configuration filename
#define SampleRate 8000  //Samples Rate
#define BitsPerSample 16  //PCM16 mode
#define Channels 1     // mono
#define CHSIZE 320 //chunk size in bytes (2 * sample125us)
#define CHNUMS 16  //number of chunks for buffer size 2400 samples (300 mS) can be buffered
#define ROLLMASK (CHNUMS-1) //and-mask for roll buffers while pointers incremented

//=======================================================

HWAVEIN In=0;                   // Wave input device
HWAVEOUT Out=0;                 // Wave output device
WAVEFORMATEX Format;          // Audio format structure
UINT BytesPerSample;          // Bytes in sample (2 for PCM16)
HANDLE WorkerThreadHandle=0;    // Wave thread
DWORD WorkerThreadId=0;         // ID of wave thread

int IsSound=0;   //flag: sound OK
int IsGo=0;     //flag: input runs
int DevInN=0;    //system input number device specified in configure file
int DevOutN=0;   //system output number device specified in configure file
int ChSize=CHSIZE; //chunk size
int ChNums=CHNUMS; //number of chunks

//audio input
WAVEHDR *Hdr_in[2]; //headers for 2 chunks
unsigned char wave_in[CHNUMS][CHSIZE+40];  //output buffers for 16 frames 20 mS each
volatile unsigned char p_in=0; //pointer to chunk will be returned by input device
unsigned char g_in=0; //pointer to chunk will be read by application
int n_in=0; //number of bytes ready in this frame

//audio output
WAVEHDR *Hdr_out[CHNUMS]; //headers for each chunk
unsigned char wave_out[CHNUMS][CHSIZE+40]; //output buffers for 16 frames 20 mS each
unsigned char p_out=0;  //pointer to chunk will be passed to output device
int n_out=0;  //number of bytes already exist in this frame
volatile unsigned char g_out=0;  //pointer to chunk will be returned by output device

//Internal procedures
int OpenDevices (void); //open in & out devices
void StopDevices (void); //paused
void CloseDevices (void); //closed devised (finalize)
void ReleaseBuffers(void); //de - allocate memory

int rdcfg(void); //read configure file and select devices from list
void dlg_init(void); //ini audio
int dlg_start(void); //create tread

//Transcode cp866(ru) to latin
void r2tru(char* in, char* out)
{
 static char tbl_win[64]={
     -32, -31, -30, -29, -28, -27, -26, -25,
     -24, -23, -22, -21, -20, -19, -18, -17,
     -16, -15, -14, -13, -12, -11, -10, -9,
     -8, -7, -4, -5, -6, -3, -2, -1,
     -64, -63, -62, -61, -60, -59, -58, -57,
     -56, -55, -54, -53, -52, -51, -50, -49,
     -48, -47, -46, -45, -44, -43, -42, -41,
     -40, -39, -36, -37, -38, -35, -34, -33
 };
 static char* tbl_trn="abvgdezzijklmnoprstufhccss'y'ejjABVGDEZZIIKLMNOPRSTUFHCCSS'I'EJJ";
 int i,j;
 char*p=out;

 for(i=0;i<1+(int)strlen(in); i++)
 {
  if(in[i]>=0) (*p++)=in[i];
  else
  {
   for(j=0; j<64; j++)
   {
    if(in[i]==tbl_win[j])
    {
     (*p++)=tbl_trn[j];
     if((j==6)||(j==23)||(j==24)||(j==38)||(j==55)||(j==56)) (*p++)='h'; //zh, ch, sh
     if((j==25)||(j==57)) (*p++)='c'; //sc
     if((j==30)||(j==62)) (*p++)='u'; //ju
     if((j==31)||(j==63)) (*p++)='a'; //sc
     break;
    }
    if(j==64) (*p++)='?';
   }
  }
 }
}


//=============================Level 0==================

//*****************************************************************************
//Parse configure file for param and copy value to param, return length of value
//zero if not found and error code if no configure file
int  parseconfa(char* param)
{
 FILE *fpp;
 char buf[256];
 char* p=NULL;
 int i;

 //open configuration file
 fpp = fopen(DEFCONF, "rt");
 if (fpp == NULL)
 {
  perror("Cannot open configure file");
  return -1;
 }
  //read it sting-by-string
  while( fgets(buf, sizeof(buf), fpp) )
  {
   if((buf[0]=='#')||(buf[0]==0)) continue; //skip comments and empty stings
   p=strstr(buf, param); //search specified parameter in string
   if(!p) continue; //process next string up to eof
   p=strchr(buf, '='); //search separator
   if(!p) continue; //no value
   p++; //set pointer to value
   break;
  }
  fclose(fpp);
  param[0]=0; //clear input string
  if(p) //if parameter found
  {  //truncate value string to first space or end of string
   for(i=0;i<(int)strlen(p);i++)
   if( (p[i]=='\r')||(p[i]=='\n')||(p[i]==' ') ) break;
   p[i]=0;
   strncpy(param, p, 31); //replace input parameter by it's value
   param[31]=0;
  }
  return (strlen(param)); //length of value's string or null
}


//read configuration file and get specified input and output device numbers
int rdcfg(void)
{
 //FILE *fpp;
 char buf[256];
 char* p=0;
 //set default devices for use
 DevInN=0;
 DevOutN=0;
 //Load buffer parameters
 strcpy(buf, "AudioChunks");
 if(0>=parseconfa(buf)) strcpy(buf, "#");
 if(buf[0]!='#') p=strchr(buf, '*');  //pointer to ascii chunk numbers
 if(p)
 {
   ChNums=0;  //number of chunks
   ChSize=0; //chunk size in bytes
   p[0]=0;
   ChNums=atoi(++p); //to integer
   if(ChNums<2) ChNums=2; //defaults
   ChNums=ceil(log2(ChNums));
   ChNums=ceil(exp2(ChNums));
   ChSize=2*atoi(buf);
   if(ChSize<80) ChSize=80; //defaults
 }

 strcpy(buf, "NPP7");
 parseconfa(buf);
 if(buf[0]=='1') ChSize=360;
 if(ChSize>(CHSIZE+40)) ChSize=CHSIZE+40;
 printf("Period size %d, Buffer size %d\r\n", ChSize/2, ChNums*ChSize/2);

 strcpy(buf, "AudioInput"); //number of AudioIn in list
 if(0<parseconfa(buf))
 {
   p=strchr(buf, ':'); //search separator
   if(p) DevInN=atoi(++p); //string to integer from next char after it
 }
 strcpy(buf, "AudioOutput"); //number of AudioOut in list
 if(0<parseconfa(buf))
 {
   p=strchr(buf, ':');  //search separator
   if(p) DevOutN=atoi(++p); //string to integer from next char after it
 }
 printf("%d/%d in/out wave devices used\r\n", DevInN, DevOutN);
 return 0;
}


//*****************************************************************************
//discower wave input and output in system and print devices list
void dlg_init(void)
{
 int NumWaveDevs;
 int i;
 char str[256];
 
 printf("\r\n------------Initialise Headset audio----------\r\n");
 printf("\r\n------------------Audio input-----------------\r\n");
 //get number of wave input devices in system
 NumWaveDevs = waveInGetNumDevs ();
 for (i = 0; i < NumWaveDevs + 1; i++)
 {
  WAVEINCAPS DevCaps;
  waveInGetDevCaps (i - 1, &DevCaps, sizeof (DevCaps)); //get devices name
  r2tru(DevCaps.szPname, str);
  printf("WaveInDevice %d: %s\r\n", i, str);
 }
 printf("\r\n-----------------Audio output-----------------\r\n");
 //get number of wave output devices in system
 NumWaveDevs = waveOutGetNumDevs ();
 for (i = 0; i < NumWaveDevs + 1; i++)
 {
  WAVEOUTCAPS DevCaps;
  waveOutGetDevCaps (i - 1, &DevCaps, sizeof (DevCaps)); //get devices name
  r2tru(DevCaps.szPname, str);
  printf("WaveOutDevice %d: %s\r\n", i, str); //print it
 }
}

//*****************************************************************************
//Stop audio input/output
void StopDevices (void)
{
  waveInReset (In);
  waveOutReset (Out);
}

//*****************************************************************************
//Close audio input/output
void CloseDevices (void)
{
 if (In) waveInClose (In);
 In = NULL;
 if (Out) waveOutClose (Out);
 Out = NULL;
}

//*****************************************************************************
//release dynamically allocated wave header
void ReleaseBuffers(void)
{
 volatile int i;
 if(WorkerThreadId) PostThreadMessage (WorkerThreadId, WM_QUIT, 0, 0);
 for(i=0;i<CHNUMS; i++) if(Hdr_out[i]) LocalFree ((HLOCAL)Hdr_out[i]);
 for(i=0;i<CHNUMS; i++) if(Hdr_in[i]) LocalFree ((HLOCAL)Hdr_in[i]);
}

//*****************************************************************************
//Open and init wave input and output
int OpenDevices (void)
{
  MMRESULT Res;
  volatile int i;
  WAVEHDR *Hdr;

  InitCommonControls (); //init system controls
  In = NULL; //clear devices pointers
  Out = NULL;
  //set audio format
  BytesPerSample = (BitsPerSample + 7) / 8;
  Format.wFormatTag = WAVE_FORMAT_PCM;
  Format.nChannels = Channels;
  Format.wBitsPerSample = BitsPerSample;
  Format.nBlockAlign = (WORD)(Channels * BytesPerSample);
  Format.nSamplesPerSec = SampleRate;
  Format.nAvgBytesPerSec = Format.nSamplesPerSec * Format.nBlockAlign;
  Format.cbSize = 0;

  //---------------------Open Devices----------------
  //Output
  Res = waveOutOpen (
      (LPHWAVEOUT)&Out,
      DevOutN - 1,
      &Format,
      (DWORD)WorkerThreadId,
      0,
      CALLBACK_THREAD
    );
  if (Res != MMSYSERR_NOERROR) 
  {
   Out=0; //return FALSE;
   printf("Speaker not found\r\n");
  }
  else waveOutPause (Out); //stop output device
 
  //Input
  Res = waveInOpen (
      (LPHWAVEIN)&In,
      DevInN - 1,
      &Format,
      (DWORD)WorkerThreadId,
      0,
      CALLBACK_THREAD
    );
  if (Res != MMSYSERR_NOERROR) 
  {
   In=0; //return FALSE;
   printf("Mike not found\r\n");
  }

 //------------Allocates wave headers---------------- 
 if(Out)
 { 
  //For output device allocate header for each frame in buffer
  for(i=0; i<CHNUMS; i++)
  {
   Hdr = (WAVEHDR *)LocalAlloc (LMEM_FIXED, sizeof (*Hdr));
   if (Hdr)
   {
    Hdr->lpData = (char*)wave_out[i];
    Hdr->dwBufferLength = ChSize;
    Hdr->dwFlags = 0;
    Hdr->dwLoops = 0;
    Hdr->dwUser = 0;
    Hdr_out[i]=Hdr;
   }
   else
   {
    for(i=0;i<CHNUMS; i++) if(Hdr_out[i]) LocalFree ((HLOCAL)Hdr_out[i]);
    return 0;
   }
  }
  //init output pointers
   p_out=0;
   g_out=0;
   n_out=0;
 }
 
 //-------------------------init wave input---------------
 if(In)
 {
  //Allocates 2 headers
  for(i=0; i<2; i++)
  {
   Hdr = (WAVEHDR *)LocalAlloc (LMEM_FIXED, sizeof (*Hdr));
   if (Hdr)
   {
    Hdr->lpData = (char*)wave_in[i];
    Hdr->dwBufferLength = ChSize;
    Hdr->dwFlags = 0;
    Hdr->dwLoops = 0;
    Hdr->dwUser = 0;
    Hdr_in[i]=Hdr;
    waveInPrepareHeader (In, Hdr, sizeof (WAVEHDR));
    Res = waveInAddBuffer (In, Hdr, sizeof (WAVEHDR));
   }
   if( (!Hdr) || (Res != MMSYSERR_NOERROR) )
   {
    for(i=0;i<CHNUMS; i++) if(Hdr_out[i]) LocalFree ((HLOCAL)Hdr_out[i]);
    for(i=0;i<CHNUMS; i++) if(Hdr_in[i]) LocalFree ((HLOCAL)Hdr_in[i]);
    return 0;
   }
  }
  //init input pointers
  p_in=0;
  g_in=0;
  n_in=0;
 }
  return 1; //always returns true: software can be run without Mike/Speaker additional audio devices (test mode)
}

//========================Top level=================

//*****************************************************************************
//init wave devices
int soundinit(void)
{
  IsSound=0; //set sound flag to OK
  //dlg_init(); //print list of available sound devices
  printf("\r\n-------------Initialise Headset audio---------\r\n");
  rdcfg();  //read sound devices numbers from configure file
  IsSound=dlg_start(); //open wave input and wave output
  return IsSound;
}

//*****************************************************************************
//terminate wave devices
void soundterm(void)
{
 CloseHandle (WorkerThreadHandle); //stop audio thread
 WorkerThreadId = 0;
 WorkerThreadHandle = NULL;
 //StopDevices ();  //paused
 CloseDevices (); //close devices
 Sleep(500); //some time for close complete
 ReleaseBuffers(); //release memory
 //SetPriorityClass (GetCurrentProcess (), DefPrioClass); //restore priority of main procedure
 printf("Wave Thread stopped!\r\n");
 IsSound=0;
}

//*****************************************************************************
//Get number of samples in output queue
int getdelay(void)
{
 if(Out==0) return 0; //No Speaker
 int i=(int)g_out; //read volatile value: pointer to next buffer will be released
 i=-i;
 i=i+p_out; //buffers in queue now
 if(i<0) i=CHNUMS+i; //correct roll
 i=i-2; //skip two work buffers
 if(i<0) i=0; //correct
 i=i*ChSize; //bytes in queue
 i=i+n_out; //add tail in current buffer
 i=i/2; //samples in queue
 return i;
}

//*****************************************************************************
//get number of samples in chunk (frame)
int getchunksize(void)
{
 return ChSize/2;
}

//*****************************************************************************
//get total buffers size in samples
int getbufsize(void)
{
 return ((ChSize*(CHNUMS-1))/2);
}

//*****************************************************************************
//skip all samples grabbed before
void soundflush(void)
{
 //in: not released yet
}

//*****************************************************************************
//skip all unplayed samples
void soundflush1(void)
{
 //out: not released yet
}

//*****************************************************************************
//------------------------------------------------
//grab up to len samples from wave input device
//return number of actually got samples
int soundgrab(char *buf, int len)
{
 int i, l, d=0;
 unsigned char cpp, cp=p_in; //read volatile value
 //p_in pointes to first frame in work now
 //p_in+1 frame also in work
 //p_in-1 frame was last returned by input device

 //g_in pointes to the most oldest unread frame
 //n_in is number of unread bytes in it (tail)
 if(In==0) return 0; //no Mike audio HW
  
 if(IsSound && IsGo)//check for device opened
 {
  cpp=cp+1;  //pointer to buffer passed to input device
  cpp&=ROLLMASK;
  l=len*2; //length in bytes (for 16 bit audio samples)
  while(l>0) //process up to length
  { //check for pointed buffer not uses by input device now
   if((g_in==cp)||(g_in==cpp)) break; //2 chunks uses by input device at time
   i=ChSize-n_in; //ready bytes in this frame
   if(i>l) i=l;  //if we need less then exist
   memcpy(buf, &wave_in[g_in][n_in], i); //copy to output
   d+=i; //bytes outed
   l-=i; //bytes remains
   n_in+=i; //bytes processed in current frame
   if(n_in>=ChSize) //if all bytes of current frame processed
   {
    g_in++;   //pointer to next frame
    g_in=g_in&ROLLMASK; //roll mask (16 frames total)
    n_in=0;  //no byte of this frame has not yet been read
   }
  }
 }
 //if(!d) Sleep(20);
 return (d/2); //returns number of outputted frames
}


//*****************************************************************************
//pass up to len samples from buf to wave output device for playing
//returns number of samples actually passed to wave output device
int soundplay(int len, unsigned char *buf)
{
 #define STARTDELAY 2 //number of silence chunk passed to output before playing
 int d=0; //bytes passed
 int i, l;
 unsigned char cp, cg=g_out; //read volatile value
 //g_out pointes to last played (and empty now) frame
 //p_out points to frame for writing data now
 //at-least one frame must be played at time (normally two and more)
 //if no frames played at time (g_out==p_out) that is underrun occurred
 //and we must push some frames of silence (for prevention next underrun)
 //and restart playing

 if((!IsSound)||(Out==0)) return 0;  //check for device ready
 if(p_out==cg) //underrun occurred
 {
  for(i=0;i<STARTDELAY;i++) //pass to device some silence frames
  {
   memset(wave_out[i], 0, ChSize); //put silence to first frame
   waveOutPrepareHeader (Out, Hdr_out[i], sizeof (WAVEHDR)); //prepare header
   waveOutWrite (Out, Hdr_out[i], sizeof (WAVEHDR));    //pass it to wave output device
  }
  g_out=0; //reset pointer of returned headers
  p_out=i; //set pointer to next header to pass to device
  n_out=0; //it is empty
  waveOutRestart (Out); //restart wave output process
  return -1; //return error code
 }

 l=len*2; //length in bytes (for 16 bit audio samples)
 while(l) //process all input data
 {
  cp=p_out+1; //pointer to next frame
  cp=cp&ROLLMASK; //roll mask
  if(cp==cg) break; //if next frame not returned yet (buffer full)
  i=ChSize-n_out; //number of empty bytes in this frame
  if(i>l) i=l; //if we have less bytes then empty
  memcpy(&wave_out[p_out][n_out], buf+d, i); //copy input data to frame
  l-=i; //remain data bytes
  d+=i; //processed bytes
  n_out+=i; //empty bytes remains in current frame
  if(n_out>=ChSize) //if chunk full
  {
   waveOutPrepareHeader (Out, Hdr_out[p_out], sizeof (WAVEHDR)); //prepare header
   waveOutWrite (Out, Hdr_out[p_out], sizeof (WAVEHDR));    //pass it to wave output device
   p_out=cp; //pointer to next chunk
   n_out=0;  //all bytes in this frame are empty
  }
 } //while(l)
 d=d/2;
 return (d);  //returns bumber of accepted samples
}


//*****************************************************************************
//===================Wave task========================

DWORD WINAPI WorkerThreadProc (void *Arg)
{
  MSG Msg; //system message
  //Set hight priority
  SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_TIME_CRITICAL);
  //work cicle: wait for Message
  while (GetMessage (&Msg, NULL, 0, 0) == TRUE)
  {
   switch (Msg.message) //process system message from HW wave devices
   {
    case MM_WIM_DATA: //wave input buffer complete
    {
     WAVEHDR *Hdr = (WAVEHDR *)Msg.lParam;
     unsigned char bc;

     waveInUnprepareHeader (In, Hdr, sizeof (*Hdr)); //un-prepare returned wave header
     //p_in is a pointer to returned frame
     bc=p_in+1; //computes pointer to next frame (normally in use now)
     bc&=ROLLMASK;  //roll mask
     p_in=bc;  //set it as a pointer to next frame will be returns
     bc++;     //computes pointer to next frame for passes to input device
     bc&=ROLLMASK; //roll mask
     //returns header with next frame to input device
     Hdr->lpData=(char*)wave_in[bc]; //attach next buffer to this header
     waveInPrepareHeader (In, Hdr, sizeof (*Hdr)); //prepare header
     waveInAddBuffer (In, Hdr, sizeof (*Hdr)); //back to input device
     break;
    }

    case MM_WOM_DONE:  //wave output buffer played
    {
     WAVEHDR *Hdr = (WAVEHDR *)Msg.lParam;
     unsigned char bc;

     waveOutUnprepareHeader (Out, Hdr, sizeof (*Hdr)); //un-prepare returned wave header
     bc=g_out; //pointer to returned header
     bc++; //pointer to next frame: normally it was early passed to device
     g_out=bc&ROLLMASK; //roll mask
     break;
    }//if(f_out[g_out]==1)
   } //switch (Msg.message)
  }  //while (GetMessage (&Msg, NULL, 0, 0) == TRUE)
  UNREF (Arg);
  return 0;
}
//===============end of wave task=======================


//*****************************************************************************
//start audio
int dlg_start(void)
{
   int Success=0;
   //up priority of main task WARNING: there are some problems of Win32 priority!!!
   //SetPriorityClass (GetCurrentProcess (), HIGH_PRIORITY_CLASS);
   //creates wave thread
   WorkerThreadHandle = CreateThread (
              NULL,
              0,
              WorkerThreadProc,
              NULL,
              0,
              &WorkerThreadId
            );
   //open wave devices
   Success = OpenDevices ();
/*   if (Success)
   {  //start audio input
    if (waveInStart(In) != MMSYSERR_NOERROR)
    {  //if no inputs
     CloseDevices ();
     IsSound=0;
     printf("Starting audio input failed\r\n");
    }
   } */
   return Success;
}

//start/stop audio input
int soundrec(int on)
{
 if(on!=IsGo)
 {
  MMRESULT Res;

  if(on) Res = waveInStart(In); //start audio input
  else Res = waveInStop(In);   //stop audio input
  if(Res != MMSYSERR_NOERROR) IsGo=0; //error
  else IsGo=on;                       //set flag
  //printf("Rec=%d\r\n", IsGo);
 }
 return IsGo;                        //return status
}
