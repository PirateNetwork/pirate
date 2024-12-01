#ifndef __MD5_H__
#define __MD5_H__

//--[ INCLUDE FILES ]---------------------------------------------------------
#ifdef ARM
   //Embedded platform includes
   #include "hardwarewallet/BaseTypes_ARM.h"
#else
   //PC platform includes   
   #include "hardwarewallet/BaseTypes.h"
#endif


#ifdef __cplusplus
   extern "C" {
#endif 

//--[ MACROS ]----------------------------------------------------------------
//--[ GLOBALS]----------------------------------------------------------------

//--[ FUNCTION PROTOYPES ]----------------------------------------------------

//Input: pcaFilename - File to calculate MD5sum of
//Output: pcaMD5 - Array of 32 bytes containing the MD5sum in hex format
//Return: 0 - Success
//       -1 - Could not read from the file
int8_t md5sum(char *pcaFilename, uint8_t *pcaMD5);

#ifdef __cplusplus
}
#endif

#endif
