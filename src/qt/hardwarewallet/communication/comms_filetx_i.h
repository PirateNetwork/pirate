// comms_i.h
#ifndef _COMMS_FILETX_I_H_
#define _COMMS_FILETX_I_H_

#ifndef _COMMS_FILETX_C_
   #error This file is private to comms_filetx.c
#endif

//--[ INCLUDES ]---------------------------------------------------------------
#ifdef ARM
   //Embedded platform includes
   #include "../BaseTypes_ARM.h"
#else
   //PC platform includes   
   #include "hardwarewallet/BaseTypes.h"
#endif

#include "hardwarewallet/libraries/crc16.h"

//network
#include<stdio.h>       //printf
#include<string.h>      //memset
#include<stdlib.h>      //exit(0);
#include<arpa/inet.h>
#include<sys/socket.h>

#include "hardwarewallet/libraries/SerialPort.h"



//--[ MACROS ]-----------------------------------------------------------------
#define PROTOCOL_SOM 0xFE
#define PROTOCOL_EOM 0xEF

#define RxNOSYNC 0
#define RxMSGID 1
#define RxLEN1 2
#define RxLEN2 3
#define RxDATA 4
#define RxCRC1 5
#define RxCRC2 6
#define RxEOM 7

//--[ GLOBALS ]----------------------------------------------------------------
uint8_t    cPC_InternalBufferState=RxNOSYNC;
uint8_t    cPC_InternalMsgID;
uint16_t   iPC_InternalCRC;
uint8_t    cPC_InternalBuffer[ COMMS_BUFFER_SIZE+7 ];
uint16_t   iPC_InternalBufferPos=0;
uint16_t   iPC_InternalBufferLength=0;

uint8_t    cPC_SerialPortHandle=255;

//--[ Function Prototypes ]----------------------------------------------------
   
#endif
