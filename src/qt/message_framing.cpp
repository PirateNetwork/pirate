/*--[ INCLUDE FILES ]--------------------------------------------------------*/
#include <QDebug>
#include <QList>
#include <QTimer>
#include "message_framing.h"
#include "hardwarewallet/libraries/crc16.h"

//--[ GLOBALS ]----------------------------------------------------------------


/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : MsgFrame_Widget()
*
* DESCRIPTION : Class constructor
*
* INPUT PARAMETERS : parent - reference to parent data
*
* RETURN PARAMETERS : None
*
* Note :
*
******************************************************************************/
MsgFrame_Widget::MsgFrame_Widget (QWidget * parent)
                :QWidget (parent)
{
  qSerialTimer = new QTimer(this);
  connect (qSerialTimer, SIGNAL(timeout()),
           this        , SLOT(qSerialTimer_timeout())
          );


  cInternalBufferState=RxNOSYNC;
  memset(&cInternalBuffer[0],0,COMMS_RX_BUFFER_SIZE+7);
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : ~MsgFrame_Widget()
*
*
* DESCRIPTION : Class destructor
*
* INPUT PARAMETERS : None
*
* RETURN PARAMETERS : None
*
* Note :
*
*
*
******************************************************************************/
MsgFrame_Widget::~MsgFrame_Widget ()
{
  cInternalBufferState=RxNOSYNC;
  memset(&cInternalBuffer[0],0,COMMS_RX_BUFFER_SIZE+7);
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : qSerialTimer_timeout()
*
*
* DESCRIPTION : A timer that reads the serial port. It simulates the
*               RX/emit Framing() function of the CAN, DIO and Ethernet
*               QT classes
*
* INPUT PARAMETERS : None
*
* RETURN PARAMETERS : int8_t -  1 - Open
*                              -1 - Error: Connection already open
*                              -2 - Error: Could not open the port
*                              -3 - Error: eLRUType unknown
*
* Note :
******************************************************************************/
void MsgFrame_Widget::qSerialTimer_timeout()
{
  Frameing();
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : OpenSerialConnection()
*
*
* DESCRIPTION : Open a Serial connection
*
* INPUT PARAMETERS : cDeviceName - Device string (i.e. /dev/ttyUSB0)
*                  : eSpecifiedLruType - Type of device that is communication on the link
*
* RETURN PARAMETERS : int8_t -  0 - Open
*                              -1 - Error: Connection already open
*                              -2 - Error: Could not open the port
*                              -3 - Error: eLRUType unknown
*                              -4 - Error: cDeviceName_length too big.
*
* Note :
******************************************************************************/
int8_t MsgFrame_Widget::OpenSerialConnection (char *pcDeviceName,
                                              uint8_t cDeviceName_length)
{
  int8_t cReturnCode;
  SerialSettings_s sSerialSettings;
  uint8_t cByte;

  if (cDeviceName_length >= sizeof(sSerialSettings.cPCComPortName))
  {
    fprintf(stderr,"MAIN : Supplied serial port name too long\n");
    return -4;
  }

  memcpy (&sSerialSettings.cPCComPortName[0], pcDeviceName, cDeviceName_length);
  sSerialSettings.cPCComPortName[cDeviceName_length]=0;
  sSerialSettings.cTimeout=0;
  sSerialSettings.eBaudRate=BAUD115200;
  sSerialSettings.eDataBits=DATA_8;
  sSerialSettings.eFLowType=FLOW_OFF;
  sSerialSettings.eParity=PAR_NONE;
  sSerialSettings.eStopBits=STOP_1;

  cSerialPortHandle=255;
  cReturnCode = SP_OpenPort (&sSerialSettings,&cSerialPortHandle);
  if (cReturnCode==0)
  {
    //Flush any data in the port that may have been accumulating while the application
    //was empty:
    while(1)
    {
      cReturnCode = SP_Read(&cSerialPortHandle,&cByte,1);
      if (cReturnCode != 1)
      {
        break;
      }
      usleep(1);
    }

    qSerialTimer->start(10); //10ms timer on the serial receiver.
    return 0;
  }
  else
  {
    fprintf(stderr,"MAIN : Failed to open the serial port: '%s'\n",&sSerialSettings.cPCComPortName[0]);
    return -2;
  }
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : CloseSerialConnection()
*
*
* DESCRIPTION : Close a Serial connection
*
* INPUT PARAMETERS : None
*
* OUTPUT PARAMETERS : None
*
* RETURN PARAMETERS : int8_t -  0 - Port closed
*                              -1 - Error: Could not close the port
*                              -2 - Error: Internal variables indicate that
*                                          a serial connection is not open.
*
* Note :
******************************************************************************/
int8_t MsgFrame_Widget::CloseSerialConnection ( )
{
  int8_t cReturnCode;
  qSerialTimer->stop();

  cInternalBufferState=RxNOSYNC;
  memset(&cInternalBuffer[0],0,COMMS_RX_BUFFER_SIZE+7);

  //Port open?
  //255==Uninitialised, closed
  if (cSerialPortHandle==255)
  {
    return 0;
  }

  cReturnCode = SP_ClosePort (&cSerialPortHandle);
  if (cReturnCode == 0)
  {
    cSerialPortHandle=255;
    return 0;
  }
  else
  {
    return -1;
  }

}


/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : Frameing()
*
*
* DESCRIPTION : Extract frames out of the raw data
*
* INPUT PARAMETERS : None
*
* RETURN PARAMETERS : None
*
* Note :
*
******************************************************************************/
void MsgFrame_Widget::Frameing (void)
{
  int16_t recv_len;
  //uint8_t cI;
  uint16_t iJ;
  uint16_t iCalcDataBuffCRC;
  uint8_t cData;
  int16_t iReturnCode;


  // Analyse every element in the data buffer, until a message is received
  while(1)
  {
    iReturnCode = iNwBufWritePos - iNwBufReadPos;
    if (iReturnCode<=0)
    {
      // printf("No remaining bytes to read. Blank variables\n");
      iNwBufWritePos=0;
      iNwBufReadPos=0;
    }

    if (iNwBufReadPos==0)
    {
      /*
      recv_len = recvfrom(socketfd, &cNwBuf[0], BUFLEN, 0, (struct sockaddr *) &si_other, &slen);
      if (recv_len == -1)
      {
        return -2; //No new data available
      }
      */
      recv_len = SP_Read (&cSerialPortHandle, (uint8_t *)&cNwBuf[0], BUFLEN);
      if (recv_len>0)
      {
        iNwBufWritePos=recv_len;
        printf("Read from uart: %d\n",recv_len);
      }
      else
      {
        //No more data.
        return;
      }
    }

    printf("%d-(%d+1)=%d bytes to process, internal position: %d, write-internal=%d\n",iNwBufWritePos, iNwBufReadPos, (iNwBufWritePos-iNwBufReadPos-1), iInternalBufferPos, (iNwBufWritePos - iInternalBufferPos) );
    cData=cNwBuf[iNwBufReadPos];
    iNwBufReadPos++;

    switch (cInternalBufferState)
    {
      case RxNOSYNC:
        if(cData == PROTOCOL_SOM)
        {
          printf("SOM\n");
          cInternalBufferState=RxMSGID;
        }
        else
        {
          printf("Expected SOM. Found 0x%02x\n",cData);
        }
        break;
      case RxMSGID:
        cInternalMsgID = cData;
        cInternalBufferState=RxLEN1;

        if (
           (cInternalMsgID==MSGID_HANDSHAKE)   ||
           (cInternalMsgID==MSGID_SESSIONKEY)  ||
           (cInternalMsgID==MSGID_PINGPONG)    ||
           (cInternalMsgID==MSGID_LOGIN_ACK)   ||
           (cInternalMsgID==MSGID_LOGOUT)      ||
           (cInternalMsgID==MSGID_SELECT_PROJECT_ACK) ||

           (cInternalMsgID==MSGID_GEN_MNEMONIC)||
           (cInternalMsgID==MSGID_GEN_MNEMONIC_PREVIOUS)||
           (cInternalMsgID==MSGID_GEN_MNEMONIC_NEXT)    ||

           (cInternalMsgID==MSGID_RESTORE_MNEMONIC)         ||
           (cInternalMsgID==MSGID_RESTORE_MNEMONIC_PREVIOUS)||
           (cInternalMsgID==MSGID_RESTORE_MNEMONIC_NEXT)    ||
           (cInternalMsgID==MSGID_RESTORE_MNEMONIC_CLEAR)   ||
           (cInternalMsgID==MSGID_RESTORE_MNEMONIC_FIRST_4_CHARS)||

           (cInternalMsgID==MSGID_GENERATE_PASSWORD)||
           (cInternalMsgID==MSGID_VERIFY_PASSWORD)  ||
           (cInternalMsgID==MSGID_UPGRADE_STATUS)   ||
           (cInternalMsgID==MSGID_APPLY_CONFIG)     ||
           (cInternalMsgID==MSGID_REG_STATUS)       ||

           (cInternalMsgID==MSGID_RETRIEVE_ADDRESS)     ||
           (cInternalMsgID==MSGID_RETRIEVE_ADDRESS_OTP) ||

           (cInternalMsgID==MSGID_REGISTRATION)      ||
           (cInternalMsgID==MSGID_REGISTRATION_OTP)  ||
           (cInternalMsgID==MSGID_REGISTRATION_ACK)  ||

           (cInternalMsgID==MSGID_SIGN_ACK)      ||
           (cInternalMsgID==MSGID_SIGN_NAVIGATE) ||
           (cInternalMsgID==MSGID_SIGN_OTP)      ||
           (cInternalMsgID==MSGID_DECRYPT_ACK)   ||
           (cInternalMsgID==MSGID_ERROR)
           )
        {
          printf("Detected MSGID:0x%02x\n\n",cInternalMsgID);
        }
        else
        {
          printf("Invalid MsgID: 0x%02x\n",cData);
          cInternalBufferState=PROTOCOL_SOM;
        }
        break;
      case RxLEN1:
        printf("len[0]: 0x%02x\n",cData);
        iInternalBufferLength = (uint16_t)cData; // Message length: Low byte
        cInternalBufferState=RxLEN2;
        break;
      case RxLEN2:                               // Message length: High byte
        iJ = cData;
        iJ = (iJ << 8) & 0xFF00;
        iInternalBufferLength |= iJ;

        if(iInternalBufferLength > COMMS_RX_BUFFER_SIZE)
        {
          printf("Rx msg size (%u) is larger than the supplied data buffer (%u)\n",iInternalBufferLength, COMMS_RX_BUFFER_SIZE);
          cInternalBufferState=RxNOSYNC;
          return;
        }
        if(iInternalBufferLength == 0)
        {
          cInternalBufferState=RxCRC1;
        }
        else
        {
          //TBD: Match message ID and length against the
          //     expected sizes to make sure that the MsgID/Length pair
          //     is valid.
          cInternalBufferState=RxDATA;

          //if (cInternalMsgID==MSGID_SIGN_ACK)
          //{
          //  printf("len[1]: 0x%02x Length:%u\n",cData, iInternalBufferLength);
          //}
        }
        break;
      case RxDATA:
        if(iInternalBufferPos < iInternalBufferLength)
        {
          cInternalBuffer[iInternalBufferPos] = cData;
          iInternalBufferPos++;
        }
        if(iInternalBufferPos == iInternalBufferLength)
        {
          cInternalBufferState=RxCRC1;
        }
        break;
      case RxCRC1:
        //if (cInternalMsgID==MSGID_SIGN_ACK)
        //{
        //  printf("CRC[0]: 0x%02x\n",cData);
        //}
        iInternalCRC = (uint16_t)cData; // CRC: Low byte
        cInternalBufferState=RxCRC2;
        break;
      case RxCRC2:
        // Message length: High byte
        iJ = cData;
        iJ = (iJ << 8) & 0xFF00;
        iInternalCRC |= iJ;

        //if (cInternalMsgID==MSGID_SIGN_ACK)
        //{
        //  printf("calc crc on internal buffer for %u bytes\n",iInternalBufferLength);
        //}
        iCalcDataBuffCRC =CalcCrc16(&cInternalBuffer[0],iInternalBufferLength,0);

        printf("CRC[1]: 0x%02x, RxCRC:0x%04x, CalcCRC:0x%04x\n",cData,iInternalCRC,iCalcDataBuffCRC);
        if (iCalcDataBuffCRC == iInternalCRC)
        {
          cInternalBufferState=RxEOM;
        }
        else
        {
          printf("CRC error\n");
          cInternalBufferState=RxNOSYNC;
          return; //CRC error
        }
        break;
      case RxEOM:
        cInternalBufferState=RxNOSYNC;
        iInternalBufferPos=0;

        if (cData == PROTOCOL_EOM)
        {
          printf("Received frame, msgid=%u\n",cInternalMsgID);

          printf("Copy %u bytes\n",iInternalBufferLength);
          //iInternalBufferLength;
          //memcpy(pDataBuffer,&cInternalBuffer[0],iInternalBufferLength);
          //*pcMsgID = cInternalMsgID;

          //Does the callback happen here at the emit?   
          printf("Emit signal_FrameDetected\n");       
          Q_EMIT signal_FrameDetected(cInternalMsgID, &cInternalBuffer[0], iInternalBufferLength);
          return;
        }
        else
        {
          printf("Expected EOM. Recieved 0x%02x\n",cData);
        }
        break;
      default:
        cInternalBufferState=RxNOSYNC;
        break;
    }//switch
  }//while

}
uint8_t cDataPack[COMMS_RX_BUFFER_SIZE+7];
/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : MsgFrame_pack()
*
* DESCRIPTION : Pack a message into a frame
*
* INPUT PARAMETERS : *pData  - Pointer to data buffer containing payload data
*                    iLength - Length of payload data to be transmitted
*
* RETURN PARAMETERS : int8_t 0  : Success
*                           -1  : Input error
*                           -2  : Could not book the message to send it
*
* Note :
*
******************************************************************************/
int8_t MsgFrame_Widget::Pack (uint8_t cMsgID, uint8_t *pcaData, uint16_t iLength)
{

  uint16_t iCRC;
  ssize_t iBytesSend;

  if (iLength > (COMMS_RX_BUFFER_SIZE))
  {
    //printf("Too much data\n");
    return -1;
  }

  if (iLength==0)
  {
    //Length must be larger than 0, even if no payload data is send
    iLength=1;
  }

  // SOM: Start of Message
  cDataPack[0]=PROTOCOL_SOM;

  // mID: Message ID
  //   1 : Connect/Login
  //   2 : Ping
  //0x55 : Sign transaction
  cDataPack[1]=cMsgID;
  printf("Sending msgid 0x%02x\n", cMsgID);

  // Length
  cDataPack[2]=  (uint8_t)((iLength >> 0) & 0xFF);
  cDataPack[3]=  (uint8_t)((iLength >> 8) & 0xFF);
  printf ("length[1][0]: 0x%02x 0x%02x\n", cDataPack[3], cDataPack[2]);

  //Assign data into tx buffer
  if (iLength>0)
  {
    memcpy(&cDataPack[4],pcaData,iLength);
    // CRC
    iCRC = CalcCrc16(pcaData, (uint32_t)iLength, 0);
  }
  else
  {
    iCRC=0;
  }
  cDataPack[4+iLength  ] = (uint8_t)((iCRC >> 0) & 0xFF);
  cDataPack[4+iLength+1] = (uint8_t)((iCRC >> 8) & 0xFF);
  printf ("CRC[1][0]:0x%02x 0x%02x, crc=0x%04x\n", cDataPack[4+iLength+1], cDataPack[4+iLength], iCRC);

  // EOM: End of Message
  cDataPack[4+iLength+2]=PROTOCOL_EOM;
  printf ("EOM: 0x%02x\n", cDataPack[4+iLength+2]);

  /*
  printf("Transaction: %u bytes\n",(iLength+7));
  for (int iI=0;iI<(4+iLength+3);iI++)
  {
    printf("%02x", cDataPack[iI]);
  }
  printf("\n\n");
  */
  //Send to uart:
  iBytesSend = SP_Write (&cSerialPortHandle, &cDataPack[0], (4+iLength+3));
  if (iBytesSend != (4+iLength+3))
  {
    return -2;
  }

  return 0;
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : MsgFrame_handshake()
*
* DESCRIPTION : See if the unit responds to a handshake prompt. Does 10 tests,
*               100ms appart
*
* INPUT PARAMETERS : None
*
* RETURN PARAMETERS : int8_t 1  : Got response
*                            0  : No response
*
* Note :
*
******************************************************************************/
int8_t MsgFrame_Widget::Handshake ( )
{
  uint8_t caData[50];
  int16_t iCount;
  uint8_t cI;

  for (cI=0;cI<5;cI++)
  {
    caData[0]='\n';
    iCount = SP_Write (&cSerialPortHandle, &caData[0], 1);
    //Delay for the unit to respond
    usleep(200000); //200ms

    iCount = SP_Read( &cSerialPortHandle, &caData[0], 50);
    if (iCount>=4)
    {
      if (
         (caData[0]=='A') &&
         (caData[1]=='r') &&
         (caData[2]=='r') &&
         (caData[3]=='r')
         )
      {
        return 1;
      }
    }
  }
  return 0;
}
