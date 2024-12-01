#ifndef __CRC16_H__
#define __CRC16_H__

//--[ INCLUDE FILES ]---------------------------------------------------------
#include "hardwarewallet/BaseTypes.h"

/*
#ifdef WIN32
   //PC platform includes
   #include "BaseTypes_WIN32.h"
#else
   //Embedded platform includes   
   #include "hardwarewallet/BaseTypes.h"   
#endif

#ifdef WIN32
 // This file can now be included in C or C++ without changing
 // anything to the H or C file ;)
*/ 
 #ifdef __cplusplus
   extern "C" {
 #endif 
//#endif

//--[ MACROS ]----------------------------------------------------------------
//--[ GLOBALS]----------------------------------------------------------------

//--[ FUNCTION PROTOYPES ]----------------------------------------------------

// compute CRC checksum
uint16_t	CalcCrc16(void* pBuffer, uint32_t lSize, uint16_t wSeed);
uint16_t	AppendCrc16(void* pBuffer, uint32_t lSize, uint16_t wSeed);


//#ifdef WIN32
  #ifdef __cplusplus
  }
  #endif
//#endif

#endif

