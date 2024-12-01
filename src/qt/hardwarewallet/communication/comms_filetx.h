#ifndef _COMMS_FILETX_H_
#define _COMMS_FILETX_H_

#ifdef _COMMS_FILETX_H_
  #define _COMMS_FILETX_H_EXTERN
#else
  #define _COMMS_FILETX_H_EXTERN extern
#endif

//--[ INCLUDE FILES ]----------------------------------------------------------
#include "hardwarewallet/BaseTypes.h"

//--[ MACROS ]-----------------------------------------------------------------
#define FILETX_ID_SYNC              0x55

#define FILETX_ID_ERROR		    0xFF  //ERROR
#define FILETX_ID_START_UPGRADE     0x60  //Upgrade mechanism
                                          //[0]:1=Start file transfer
                                          //    [1]:Communication version
                                          //    [2]:Application version
                                          //   :2=Status
                                          //   :3=Deploy upgrade
                                      
#define FILETX_ID_DATA              0x61  //Incoming data
#define FILETX_ID_END_OF_FILE       0x62  //End of transmission
                                      
#define FILETX_ID_END_OF_TRANSMISSION    0x63  //Tx complete


#define COMMS_BUFFER_SIZE 2000

//--[ GLOBALS]-----------------------------------------------------------------

//--[ FUNCTION PROTOTYPES ]----------------------------------------------------
int8_t  CommsFiletx_Init(char_t *pcaPort );
int8_t  CommsFiletx_Poll(void);
int8_t  CommsFiletx_Pack(uint8_t *pData, uint16_t wLength, uint8_t MsgID);
int16_t CommsFiletx_Unpack(uint8_t *pDataBuffer,uint16_t iDataBufferSize, uint8_t *pcMsgID);
int8_t  CommsFiletx_Flush(void);
int8_t  CommsFiletx_Close();
char *CommsFileTx_PrintMsgID( uint8_t cMsgID );
#endif
