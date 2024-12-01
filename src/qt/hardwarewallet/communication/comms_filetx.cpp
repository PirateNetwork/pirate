#define _COMMS_FILETX_C_

//https://www.binarytides.com/programming-udp-sockets-c-linux/

//--[ INCLUDE FILES ]----------------------------------------------------------
#include "hardwarewallet/BaseTypes.h"
#include "comms_filetx.h"
#include "comms_filetx_i.h"


//--[ MACROS ]-----------------------------------------------------------------
int socketfd;
char cNwBuf[COMMS_BUFFER_SIZE+7];
uint16_t iNwBufReadPos;
uint16_t iNwBufWritePos;

struct sockaddr_in si_me;
struct sockaddr_in si_other;
socklen_t slen;
//--[ FUNCTIONS ]--------------------------------------------------------------


//-----------------------------------------------------------------------------
// NAME        : Comms_Init
// DESCRIPTION : This function initialises the communication framing layer
//               as well as the underlying communication channel.
// INPUT       : pcaPort - Serial port to open
//
// OUTPUT      : None
//
// RETURN CODE : 0 : Success
//             :-1 : Serial port name too long
//             :-2 : Could not open the communication channel.
//-----------------------------------------------------------------------------
int8_t CommsFiletx_Init(char_t *pcaPort )
{
  int8_t cReturnCode;
  int16_t iReturnCode;
  
  //Internal variables:
  cPC_InternalBufferState=RxNOSYNC;
  iPC_InternalBufferPos=0;
  iPC_InternalBufferLength=0;
  iNwBufReadPos=0;
  iNwBufWritePos=0;

  //printf("Open serial port\n");
  SerialSettings_s sSerialSettings;
  uint8_t caData[100];

  if (strlen(pcaPort) >= sizeof(sSerialSettings.cPCComPortName))
  {
    return -1;
  }

  memset (&sSerialSettings.cPCComPortName[0], 0, sizeof(sSerialSettings.cPCComPortName)-1);

  memcpy (&sSerialSettings.cPCComPortName[0], pcaPort, strlen(pcaPort) );
  sSerialSettings.cTimeout=1;
  sSerialSettings.eBaudRate=BAUD460800;
  sSerialSettings.eDataBits=DATA_8;
  sSerialSettings.eFLowType=FLOW_OFF;
  sSerialSettings.eParity=PAR_ODD;
  sSerialSettings.eStopBits=STOP_1;

  cPC_SerialPortHandle=255;
  cReturnCode = SP_OpenPort (&sSerialSettings,&cPC_SerialPortHandle);
  uint32_t wCount=0;
  if (cReturnCode==0)
  {
    //Flush any data in the port that may have been accumulating while the application
    //was empty:
    while(1)
    {
      iReturnCode = SP_Read(&cPC_SerialPortHandle,&caData[0],100);
      if (iReturnCode != 100)
      {       
        break;
      }
      wCount+=iReturnCode;
      usleep(1);
    }
    //printf("Serial port opened successfully. Port handle = %d. Flushed %u bytes\n",cPC_SerialPortHandle, wCount);
    return 0;
  }
  else
  {
    printf("Failed to open the serial port. SP_OpenPort() ReturnCode=%d\n",cReturnCode);
    return -2;
  }
}

//-----------------------------------------------------------------------------
// NAME        : Comms_Close
// DESCRIPTION : Close the serial port
//
// INPUT       : None
//
// OUTPUT      : None
//
// RETURN CODE : 0 : Success
//              -1 : Illegal input/output parameters (NULL pointers)
//              -2 : Port is not open.
//              -3 : Could not terminate the thread in which the 
//                   serial port lives
//              -4 : Could not close the connection.
//-----------------------------------------------------------------------------
int8_t CommsFiletx_Close()
{
  int8_t cReturnCode;
  cReturnCode = SP_ClosePort (&cPC_SerialPortHandle);
  return cReturnCode;
}

//-----------------------------------------------------------------------------
// NAME        : CommsFiletx_Flush
// DESCRIPTION : Flush UART pending bytes. Reset unpack statemachine variables
//
// INPUT       : None
//
// OUTPUT      : None
//            
// RETURN CODE : 0 : Success
//-----------------------------------------------------------------------------
int8_t CommsFiletx_Flush()
{
  uint32_t wFlushedBytes=0;
  uint16_t iRecvLen=0;
  int8_t   cReturnCode=0;
//  uint8_t  cByte;
  uint32_t wCounter=0;
  
  //Reset Unpack variables:
  iPC_InternalBufferPos=0;          
  iNwBufWritePos=0;
  iNwBufReadPos=0;  
  cPC_InternalBufferState = RxNOSYNC;
  
  //Flush the buffer
  while(1)
  {
    iRecvLen = SP_Read (&cPC_SerialPortHandle, (uint8_t *)&cNwBuf[0], COMMS_BUFFER_SIZE+7);
    if (cReturnCode == 0)  
    {       
      wCounter++;
    }
    else
    {
      wFlushedBytes+=iRecvLen;
      wCounter=0;
    }
    usleep(250);
    
    if (wCounter>40) //10ms
    {
      break;
    }    
  }
  //printf("Flush() Flushed %u bytes\n",wFlushedBytes);
  return 0; 
}

//-----------------------------------------------------------------------------
// NAME        : CommsFiletx_Unpack
// DESCRIPTION : This function analyses the received data buffer
//               and attempt to extract a message frame from it.
// INPUT       : iDataBufferSize - Size of supplied pDataBuffer
//
// OUTPUT      : pDataBuffer - Contains the extracted message
//             : pcMsgID     - Message ID
//            
// RETURN CODE : >=0 : Valid message frame detected. > 0 indicate # of bytes
//             :-1   : Input error
//             :-2   : No data from the UART. No pending message
//             :-3   : No data from the UART. Partial message being decoded
//             :-4   : Supplied iDataBufferSize too small for received message
//             :-5   : Message detected, CRC Failed || no EOM
//-----------------------------------------------------------------------------
int16_t CommsFiletx_Unpack(uint8_t *pDataBuffer,uint16_t iDataBufferSize,
                            uint8_t *pcMsgID)
{  	
  int16_t iRecvLen;
//  uint8_t cI;
  uint16_t iJ;
  uint16_t iCalcDataBuffCRC;
  uint8_t cData;
  int16_t iReturnCode;
//  uint8_t cByte;
//  uint32_t wCount;
//  int8_t cReturnCode;

  *pcMsgID=0;
  
  //Check inputs
  if (
     (pDataBuffer == NULL) ||
     (pcMsgID == NULL)
     )
  {
    return -1;
  }


  // Analyse every element in the data buffer, until a message is received
  while(1)
  {
    iReturnCode = iNwBufWritePos - iNwBufReadPos;
    if (iReturnCode<=0)
    {
      //printf("No remaining bytes to read. Blank variables\n");
      iNwBufWritePos=0;
      iNwBufReadPos=0;
    }
    //try to receive some data, this is a blocking call
    
    if (iNwBufReadPos==0)
    {
      /*
      iRecvLen = recvfrom(socketfd, &cNwBuf[0], COMMS_BUFFER_SIZE, 0, (struct sockaddr *) &si_other, &slen);
      if (iRecvLen == -1)
      {
        if (iPC_InternalBufferPos==0)
        {
          return -2;
        }
        else
        {
          return -3;
        }
      }
      */
      iRecvLen = SP_Read (&cPC_SerialPortHandle, (uint8_t *)&cNwBuf[0], COMMS_BUFFER_SIZE+7);
      if (iRecvLen>0)
      {
        iNwBufWritePos=iRecvLen;
        //printf("Read from uart: %d, PC_Internal pos=%d, PC_Internal len=%d\n",iRecvLen, iPC_InternalBufferPos, iPC_InternalBufferLength);
        usleep(1000);//1ms to give time for any further incoming data
      }
      else
      {
        //printf("No more data. iPC_InternalBufferPos=%u\n",iPC_InternalBufferPos);
        return -2;
      }      
    }    
    
    //printf("%d-(%d+1)=%d bytes to process, internal position: %d, write-internal=%d\n",iNwBufWritePos, iNwBufReadPos, (iNwBufWritePos-iNwBufReadPos-1), iPC_InternalBufferPos, (iNwBufWritePos - iPC_InternalBufferPos) );
    cData=cNwBuf[iNwBufReadPos];
    iNwBufReadPos++;
    
    switch (cPC_InternalBufferState)
    {               
      case RxNOSYNC:    
        if(cData == PROTOCOL_SOM)
        {
          //printf("SOM\n");
          cPC_InternalBufferState=RxMSGID;
        }
        iPC_InternalBufferPos=0;
        break;                           
      case RxMSGID:
        cPC_InternalMsgID = cData;
        cPC_InternalBufferState=RxLEN1;
    
        if (
           (cPC_InternalMsgID==FILETX_ID_SYNC)          ||
           (cPC_InternalMsgID==FILETX_ID_ERROR)         ||
           (cPC_InternalMsgID==FILETX_ID_START_UPGRADE) ||
           (cPC_InternalMsgID==FILETX_ID_DATA)          ||
           (cPC_InternalMsgID==FILETX_ID_END_OF_FILE)   ||
           (cPC_InternalMsgID==FILETX_ID_END_OF_TRANSMISSION)
           )
        {
          //printf("Framing: Detected MSGID: 0x%02x\n",cPC_InternalMsgID);
        }
        else
        {
          printf("Framing: Invalid MsgID detected: 0x%02x. Resync\n", cPC_InternalMsgID);
          cPC_InternalBufferState=RxNOSYNC;
        }        
        
        break;                           
      case RxLEN1:   
        //printf("len[0]: 0x%02x\n",cData);   
        iPC_InternalBufferLength = (uint16_t)cData; // Message length: Low byte
        cPC_InternalBufferState=RxLEN2;
        break;                  
      case RxLEN2:                               // Message length: High byte                                                            
        iJ = cData;
        iJ = (iJ << 8) & 0xFF00;
        iPC_InternalBufferLength |= iJ;
        //printf("len[1]: 0x%02x, PC_InternalBufferLength=0x%04x\n", cData, iPC_InternalBufferLength);

        if (
           ( iPC_InternalBufferLength > iDataBufferSize       ) ||
           ( iPC_InternalBufferLength > (COMMS_BUFFER_SIZE+7) )
           )
        {
          printf("Rx msg size (%u) is larger than the supplied data buffer (%u)\n",iPC_InternalBufferLength, iDataBufferSize);
          cPC_InternalBufferState=RxNOSYNC;
          return -4;
        }
        if(iPC_InternalBufferLength == 0)
        {
          cPC_InternalBufferState=RxCRC1;
        }
        else
        {
          //TBD: Match message ID and length against the
          //     expected sizes to make sure that the MsgID/Length pair
          //     is valid.
          cPC_InternalBufferState=RxDATA;
//          printf("Msg length:%u\n",iPC_InternalBufferLength);   
        }
        break;   
      case RxDATA:
        if(iPC_InternalBufferPos < iPC_InternalBufferLength)  
        {   
          cPC_InternalBuffer[iPC_InternalBufferPos] = cData;  
          iPC_InternalBufferPos++; 
        }
        if(iPC_InternalBufferPos == iPC_InternalBufferLength)
        {
//          printf("Msg CRC\n");
          cPC_InternalBufferState=RxCRC1;   
        }                              
        break;     
      case RxCRC1:
        //printf("CRC[0]: 0x%02x\n",cData);
        iPC_InternalCRC = (uint16_t)cData; // CRC: Low byte
        cPC_InternalBufferState=RxCRC2;
        break;                  
      case RxCRC2:      
        // Message length: High byte                                                            
        iJ = cData;
        iJ = (iJ << 8) & 0xFF00;
        iPC_InternalCRC |= iJ;                                                
        
        //printf("calc crc on internal buffer for %u bytes\n",iPC_InternalBufferLength);
        iCalcDataBuffCRC =CalcCrc16(&cPC_InternalBuffer[0],iPC_InternalBufferLength,0);

        //printf("CRC[1]: 0x%02x, RxCRC:0x%04x, CalcCRC:0x%04x\n",cData,iPC_InternalCRC,iCalcDataBuffCRC);        
        if (iCalcDataBuffCRC == iPC_InternalCRC)
        {
          cPC_InternalBufferState=RxEOM;
        }
        else
        {
          printf("CRC error\n");
          cPC_InternalBufferState=RxNOSYNC;
          iPC_InternalBufferLength=0;
          return -5; //CRC error || EOM not detected
        }
        break;                               
      case RxEOM:
        //EOM should now get detected for this iteration. Exception from the rest of the other states.
        //printf("EOM\n");
        cPC_InternalBufferState=RxNOSYNC;
        if (cData == PROTOCOL_EOM)
        {       
          //printf("Copy %u bytes\n",iPC_InternalBufferLength);
          memcpy(pDataBuffer,&cPC_InternalBuffer[0],iPC_InternalBufferLength);
          /*
          //If EOM is valid , the values of pDataBuffer and pcMsgID will be set.  
          for (cI=0;cI<iPC_InternalBufferLength;cI++)
          {
            *pDataBuffer=cPC_InternalBuffer[cI];
            pDataBuffer++;
          }
          */
          *pcMsgID = cPC_InternalMsgID;
          iPC_InternalBufferPos=0;
          return iPC_InternalBufferLength;
        }  
        else
        {
          printf("EOM not detected\n");
          return -5; //CRC error || EOM not detected          
        }
        break;
      default:
        cPC_InternalBufferState=RxNOSYNC;
        break;
    }//switch
  }//while
  
  return 0;
}  

//-----------------------------------------------------------------------------
// NAME        : CommsFiletx_Poll
// DESCRIPTION : This function sends a '?' to the M4 unit, in order to prompt
//		 it to return a frame, if any, while the communication bridge
//               between the NXP & M4 is open
// INPUT       : None
// OUTPUT      : none
// RETURN CODE :  1  : Success
//-----------------------------------------------------------------------------
int8_t CommsFiletx_Poll(void)
{
  uint8_t cByte;
  ssize_t iBytesSend;
  
  cByte='?';  

  iBytesSend = SP_Write (&cPC_SerialPortHandle, &cByte, 1);
  if (iBytesSend != 1)
  {
    //#ifdef ARM
    //printf("Error: Send %d bytes, Expected %d\n", iBytesSend, (4+iLength+3));
    //#else
    //printf("Error: Send %ld bytes, Expected %d\n", iBytesSend, (4+iLength+3));    
    //#endif
    return 0;
  }
  //printf ("%d bytes booked to send\n", (4+iLength+3));

  return 1;
}


//-----------------------------------------------------------------------------
// NAME        : CommsFiletx_Pack
// DESCRIPTION : This function packs the payload data into the tx buffer
//               Note: The message layer must supply a valid message ID.
// INPUT       : *pcaData   - Pointer to char array containing the message data
//               iLength    - Length of message data to be transmitted 
//               MsgID      - Message ID
// OUTPUT      : none
// RETURN CODE :  1  : Success
//                0  : Not all the data was send
//               -1  : Internal errors

//-----------------------------------------------------------------------------
int8_t CommsFiletx_Pack(uint8_t *pcaData, uint16_t iLength, uint8_t MsgID)
{
  uint16_t iCRC;
//  uint8_t  cByte;
//  int8_t cReturnCode;
  uint8_t caData[COMMS_BUFFER_SIZE+7];
  ssize_t iBytesSend;

  if (pcaData == NULL)
  {
    printf("Must provide a valid data pointer\n");
    return -1;
  }
  if (iLength > COMMS_BUFFER_SIZE)
  {
    printf("Too much data\n");
    return -1;
  }

  // SOM: Start of Message
  caData[0]=PROTOCOL_SOM;

  // mID: Message ID
  // Shift value with 0x80 for M4
  caData[1]=MsgID;//+0x80;

  // Length
  caData[2]=  (uint8_t)((iLength >> 0) & 0xFF);
  caData[3]=  (uint8_t)((iLength >> 8) & 0xFF);
  //printf ("length[1][0]: 0x%02x 0x%02x\n", caData[3], caData[2]);

  //Assign data into tx buffer
  if (iLength>0)
  {
    memcpy(&caData[4],pcaData,iLength);
  }

  // CRC
  //printf("crc on data for length %u\n",iLength);
  iCRC = CalcCrc16(pcaData, (uint32_t)iLength, 0);
  caData[4+iLength  ] = (uint8_t)((iCRC >> 0) & 0xFF);
  caData[4+iLength+1] = (uint8_t)((iCRC >> 8) & 0xFF);  
  //printf ("CRC[1][0]:0x%02x 0x%02x, crc=0x%04x\n", caData[4+iLength+1], caData[4+iLength], iCRC);

  // EOM: End of Message
  caData[4+iLength+2]=PROTOCOL_EOM;
  //printf ("EOM: 0x%02x\n", caData[4+iLength+2]);

  
  //Transmit message over network:
  //send the message
  //printf ("Sending: ");
  for (int iI=0;iI<(4+iLength+3);iI++)
  {
    //printf("0x%02x ",caData[iI]);
  }
  //printf("\n");
  
  //iBytesSend = sendto(socketfd, &caData[0], (4+iLength+3) , 0 , (struct sockaddr *)&si_other, slen);
  iBytesSend = SP_Write (&cPC_SerialPortHandle, &caData[0], (4+iLength+3));
  if (iBytesSend != (4+iLength+3))
  {
    #ifdef ARM
    //printf("Error: Send %d bytes, Expected %d\n", iBytesSend, (4+iLength+3));
    #else
    printf("Error: Send %ld bytes, Expected %d\n", iBytesSend, (4+iLength+3));    
    #endif
    return 0;
  }
  //printf ("%d bytes booked to send\n", (4+iLength+3));

  return 1;
}

char caMsgIDName[20];
char *CommsFileTx_PrintMsgID( uint8_t cMsgID )
{
  switch (cMsgID)
  {
    case FILETX_ID_SYNC:
      sprintf(&caMsgIDName[0],"SYNC");
      break;
    case FILETX_ID_ERROR:
      sprintf(&caMsgIDName[0],"ERROR");
      break;
    case FILETX_ID_START_UPGRADE:
      sprintf(&caMsgIDName[0],"START UPGRADE");
      break;
    case FILETX_ID_DATA:
      sprintf(&caMsgIDName[0],"DATA");
      break;
    case FILETX_ID_END_OF_FILE:
      sprintf(&caMsgIDName[0],"EOF");
      break;
    case FILETX_ID_END_OF_TRANSMISSION:
      sprintf(&caMsgIDName[0],"EoTX");
      break;
    default:
      sprintf(&caMsgIDName[0],"Unknown");
  }
  
  return &caMsgIDName[0];
}

