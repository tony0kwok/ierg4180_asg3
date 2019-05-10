/*
 * File         : es_timer.h
 * Module       : Timer Class
 * System       : Generic System
 * Project      : Foundation Classes Construction
 * Start Date   : 15th April 1994
 * Version      : 02.00 - Sep 2015 - Updated implementation based on Microsoft's latest recommendations.
 *              : 01.10 - Removed second counter and changed class name to ES_Timer.
 *              : 01.00 - Initial version.
 * Version Date : 09-Sep-2015
 * Designer     : Jack Lee
 * Programmer   : Jack Lee
 * Copyright    : Jack Lee 1993-2015 All Rights Reserved
 * Descriptions : Header file.
 * Remarks      : - Portable version which supports WIN32 and Linux Timers
 *                  For Linux need to add '-lrt' to linker to link with the RT timer lib
 *                - Max elapsed time in us is limited by long return type.
 * References   :
 *
 */

#ifndef     __ES_TIMER_H
#define     __ES_TIMER_H

///////////////////////////////////
////////// Include Files //////////
///////////////////////////////////

#ifdef WIN32 // Windows
#  include <windows.h>
#  include <sys/types.h>
#  include <sys/timeb.h>
#  include <dos.h>
#  include <time.h>
#else // Assume Linux
#  include <sys/types.h>
//#  include <sys/timeb.h>
#  include <time.h>
#endif

///////////////////////////////////
////// Constants Definition ///////
///////////////////////////////////

#define INTEGER_64_AVAILABLE

///////////////////////////////////
/// Global Variables Definition ///
///////////////////////////////////

///////////////////////////////////
/// Class/Function Declarations ///
///////////////////////////////////


/* Class Name     : ES_Timer
 * Descriptions   : An accurate time-keeping class, supporting stop-watch like
 *                  features.
 * Version        : 2.0 - Sep 2015 - Updated implementation based on Microsoft's latest recommendations.
 *
 * Notes          : This class is OS dependent.
 * Remarks        : All functions are in-lined.
 *                  
 * Limitations    : Windows Platform see https://msdn.microsoft.com/en-us/library/windows/desktop/dn553408(v=vs.85).aspx
 */

#ifdef WIN32
class ES_Timer
{
public:

   ES_Timer()
   {
      pStartingTime  = new LARGE_INTEGER;
      pEndingTime = new LARGE_INTEGER;
      pElapsedMicroseconds = new LARGE_INTEGER;
      pFrequency = new LARGE_INTEGER;;

      QueryPerformanceFrequency(pFrequency);

      Start();
   }

   ~ES_Timer()
   {
      delete pStartingTime;
      delete pEndingTime;
      delete pElapsedMicroseconds;
      delete pFrequency;
   }

   void Start()
   {
      QueryPerformanceCounter(pStartingTime);
   }

   long Elapsed()
   {
      QueryPerformanceCounter(pEndingTime);
      pElapsedMicroseconds->QuadPart = pEndingTime->QuadPart - pStartingTime->QuadPart;
      pElapsedMicroseconds->QuadPart *= 1000;
      pElapsedMicroseconds->QuadPart /= pFrequency->QuadPart;
      return pElapsedMicroseconds->QuadPart;
   }

   long ElapseduSec()
   {
      QueryPerformanceCounter(pEndingTime);
      pElapsedMicroseconds->QuadPart = pEndingTime->QuadPart - pStartingTime->QuadPart;
      pElapsedMicroseconds->QuadPart *= 1000000;
      pElapsedMicroseconds->QuadPart /= pFrequency->QuadPart;
      return pElapsedMicroseconds->QuadPart;
   }

protected:

   LARGE_INTEGER *pStartingTime, *pEndingTime, *pElapsedMicroseconds;
   LARGE_INTEGER *pFrequency;
};

#else // Linux

class ES_Timer
{
public:

   ES_Timer()
   {
	   StartMs = new struct timespec;
	   pResolution = new struct timespec;
	   pNow = new struct timespec;
	   QueryPerformanceFrequency();
	   Start();
   }

   ~ES_Timer()
   {
      delete StartMs;
      delete pResolution;
      delete pNow;
   }

   void Start()
   {
      Refresh();
      StartMs->tv_sec = pNow->tv_sec;
      StartMs->tv_nsec = pNow->tv_nsec;
      return;
   }

   long Elapsed()
   {
      Refresh();
      long retval = (pNow->tv_sec - StartMs->tv_sec)*1000;
      retval += (pNow->tv_nsec - StartMs->tv_nsec)/1000000;
      return retval;
   }

   long ElapseduSec()
   {
      Refresh();
      long retval = (pNow->tv_sec - StartMs->tv_sec)*1000000;
      retval += (pNow->tv_nsec - StartMs->tv_nsec)/1000;
      return retval;
   }

   void QueryPerformanceFrequency()
   {
	   clock_getres(CLOCK_REALTIME, pResolution);
   }

protected:

   void Refresh()
   {
      clock_gettime(CLOCK_REALTIME, pNow);
   }

   struct timespec  *StartMs, *StartXMs;
   struct timespec  *pResolution, *pNow;
};

#endif


///////////////////////////////////
///// Inline Member Functions /////
///////////////////////////////////


///////////////////////////////////
////////// End of File ////////////
///////////////////////////////////

#endif
