#define _SERIALPORT_CPP_
/*--[ INCLUDE FILES ]--------------------------------------------------------*/
#include "SerialPort.h"
#include "SerialPort_posix_i.h"
#include "stdio.h"

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : setBaudRate()
*
*
* DESCRIPTION : Set the baud rate
*
* INPUT PARAMETERS : eBaudRate - Requested baud rate
*                    pPosix_CommConfig - configuration structure
*
* RETURN PARAMETERS : int8_t 0 - Success
*
* Note :
*
*
******************************************************************************/
int8_t setBaudRate (BaudRateType_e eBaudRate,
                    struct termios *pPosix_CommConfig)
{
  int8_t cReturnCode;
  cReturnCode = 0;
  switch (eBaudRate) {
    case (BAUD9600):
      //pPosix_CommConfig->c_cflag &= (~CBAUD);
      //pPosix_CommConfig->c_cflag |= B9600;
      cfsetspeed(pPosix_CommConfig, B9600);
      break;
    case (BAUD38400):
      //pPosix_CommConfig->c_cflag &= (~CBAUD);
      //pPosix_CommConfig->c_cflag |= B38400;
      cfsetspeed(pPosix_CommConfig, B38400);
      break;
    case (BAUD57600):
      //pPosix_CommConfig->c_cflag &= (~CBAUD);
      //pPosix_CommConfig->c_cflag |= B57600;
      cfsetspeed(pPosix_CommConfig, B57600);
      break;
    case (BAUD115200):
      //pPosix_CommConfig->c_cflag &= (~CBAUD);
      //pPosix_CommConfig->c_cflag |= B115200;
      cfsetspeed(pPosix_CommConfig, B115200);
      break;
    case (BAUD230400):
      //pPosix_CommConfig->c_cflag &= (~CBAUD);
      //pPosix_CommConfig->c_cflag |= B230400;
      cfsetspeed(pPosix_CommConfig, B230400);
      break;            
    case (BAUD460800):
      //pPosix_CommConfig->c_cflag &= (~CBAUD);
      //pPosix_CommConfig->c_cflag |= B460800;
      
      #if defined(__APPLE__)
      //#define B460800 0010004
      #define B460800 460800
      #endif
      cfsetspeed(pPosix_CommConfig, B460800);
      break;      
    case (BAUD921600):
      //pPosix_CommConfig->c_cflag &= (~CBAUD);
      //pPosix_CommConfig->c_cflag |= B921600;
      #if defined(__APPLE__)
      //#define B921600 0010007
      #define B921600 921600
      #endif
      cfsetspeed(pPosix_CommConfig, B921600);
      break;
    default:
      printf("Unsupported baudrate\n");
      exit(-1);
  }
  return cReturnCode;
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : setDataBits()
*
*
* DESCRIPTION : Set the data size
*
* INPUT PARAMETERS : eDataBits - Number of data bits
*                    eStopBits - Number of stop bits
*                    pPosix_CommConfig - configuration structure
*
* RETURN PARAMETERS : int8_t  0 - Success
*                            -1 - Unknown data size
*                            -2 - Illegal stop bit size,for the given data size
*
* Note :
*
*
******************************************************************************/
int8_t setDataBits (DataBitsType_e eDataBits, StopBitsType_e eStopBits,
                    struct termios * pPosix_CommConfig)
{
  int8_t cReturnCode;

  cReturnCode = 0;
  switch (eDataBits) {
    case (DATA_5):
      if (eStopBits == STOP_2) {
        return -2;
      }
      else {
        pPosix_CommConfig->c_cflag &= (~CSIZE);
        pPosix_CommConfig->c_cflag |= CS5;
      }
      break;
    case (DATA_6):
      if (eStopBits == STOP_1_5) {
        return -2;
      }
      else {
        pPosix_CommConfig->c_cflag &= (~CSIZE);
        pPosix_CommConfig->c_cflag |= CS6;
      }
      break;
    case (DATA_7):
      if (eStopBits == STOP_1_5) {
        return -2;
      }
      else {
        pPosix_CommConfig->c_cflag &= (~CSIZE);
        pPosix_CommConfig->c_cflag |= CS7;
      }
      break;
    case (DATA_8):
      if (eStopBits == STOP_1_5) {
        return -2;
      }
      else {
        pPosix_CommConfig->c_cflag &= (~CSIZE);
        pPosix_CommConfig->c_cflag |= CS8;
      }
      break;
    default:
      cReturnCode = -1;
  }
  return cReturnCode;
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : setStopBits()
*
*
* DESCRIPTION : Set the number of stop bits
*
* INPUT PARAMETERS : eStopBits - Number of stop bits
*                    pPosix_CommConfig - configuration structure
*
* RETURN PARAMETERS : int8_t  0 - Success
*                            -1 - Unknown stop bit size
*                            -2 - Illegal stop bit size
*
* Note :
*
*
******************************************************************************/
int8_t setStopBits (StopBitsType_e eStopBits,
                    struct termios * pPosix_CommConfig)
{
  int8_t cReturnCode;

  cReturnCode = 0;
  switch (eStopBits) {
    case (STOP_1):
      pPosix_CommConfig->c_cflag &= (~CSTOPB);
      break;
    case (STOP_1_5):
      cReturnCode = -2;
      break;
    case (STOP_2):
      pPosix_CommConfig->c_cflag |= CSTOPB;
      break;
    default:
      cReturnCode = -1;
  }
  return cReturnCode;
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : setParity()
*
*
* DESCRIPTION : Set the parity bits
*
* INPUT PARAMETERS : eParity - Requested parity
*                    pPosix_CommConfig - configuration structure
*
* RETURN PARAMETERS : int8_t  0 - Success
*                            -1 - Unknown parity type
*
* Note :
*
*
******************************************************************************/
int8_t setParity (ParityType_e eParity, struct termios * pPosix_CommConfig)
{
  int8_t cReturnCode;

  cReturnCode = 0;
  switch (eParity) {
    case (PAR_NONE):
      pPosix_CommConfig->c_cflag &= (~PARENB);
      break;
    case (PAR_EVEN):
      pPosix_CommConfig->c_cflag &= (~PARODD);
      pPosix_CommConfig->c_cflag |= PARENB;
      break;
    case (PAR_ODD):
      pPosix_CommConfig->c_cflag |= (PARENB | PARODD);
      break;
    default:
      cReturnCode = -1;
  }
  return cReturnCode;
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : setFlowControl()
*
*
* DESCRIPTION : Set the hardware flow control
*
* INPUT PARAMETERS : eFlow - Requested flow control
*                    pPosix_CommConfig - configuration structure
*
* RETURN PARAMETERS : int8_t  0 - Success
*                            -1 - Unknown flow type
*
* Note :
*
*
******************************************************************************/
int8_t setFlowControl (FlowType_e eFlow, struct termios * pPosix_CommConfig)
{
  int8_t cReturnCode;

  cReturnCode = 0;
  switch (eFlow) {
    case (FLOW_OFF):
      pPosix_CommConfig->c_cflag &= (~CRTSCTS);
      pPosix_CommConfig->c_iflag &= (~(IXON | IXOFF | IXANY));
      break;
    case (FLOW_XONXOFF):
      pPosix_CommConfig->c_cflag &= (~CRTSCTS);
      pPosix_CommConfig->c_iflag |= (IXON | IXOFF | IXANY);
      break;
    case (FLOW_HARDWARE):
      pPosix_CommConfig->c_cflag |= CRTSCTS;
      pPosix_CommConfig->c_iflag &= (~(IXON | IXOFF | IXANY));
      break;
    default:
      cReturnCode = -1;
  }
  return cReturnCode;
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : SP_OpenPort()
*
*
* DESCRIPTION :This functions open the specific serial port, with the
*             : required baudrate propertiesl
*
* INPUT PARAMETERS : spSerialSettings - Port parameters
*                    pcSerialPortHandle - Handle to the serial port
*
* RETURN PARAMETERS : int8_t  0 : Success
*                            -1 : There are no more ports available
*                            -2 : The port is already open
*                            -3 : The port could not be opened
*                            -4 : Could not apply the baudrate settings
* Note :
*
*
******************************************************************************/
int8_t SP_OpenPort (SerialSettings_s * spSerialSettings,
                    uint8_t * pcSerialPortHandle)
{
  int8_t cReturnCode = 0;
  struct termios Posix_CommConfig;
  int fd;
  uint8_t cI;
  bool bFound=FALSE;

  for (cI=0;cI<MAX_PORTS;cI++)
  {
    if (sSerialArray[cI].bStatus_PortIsOpen==true)
    {
      if ( strcmp(&sSerialArray[cI].sPortSettings.cPCComPortName[0], &spSerialSettings->cPCComPortName[0]) == 0 )
      {
        fprintf(stderr,"  Error: Serial port '%s' is already open in slot [%d]\n", &sSerialArray[cI].sPortSettings.cPCComPortName[0], cI);

        //Reuse the port thats already open
        *pcSerialPortHandle=cI;
        return -2;
      }
    }
  }

  for (cI=0;cI<MAX_PORTS;cI++)
  {
    if (sSerialArray[cI].bStatus_PortIsOpen == FALSE)
    {
      //fprintf(stderr,"  SP_OpenPort() Found available slot [%d]\n",cI);
      *pcSerialPortHandle = cI;
      bFound=TRUE;
      break;
    }
  }

  if (bFound==FALSE)
  {
    fprintf(stderr,"  Error: No serial port slots available\n");
    return -1;
  }

  
  if (spSerialSettings->eFLowType==FLOW_OFF)
  {
    fd = open(spSerialSettings->cPCComPortName, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK);
  }
  else
  {
    fd = open(spSerialSettings->cPCComPortName, O_RDWR | O_NOCTTY | O_SYNC);
  }
  if (fd == -1) {
    return -3;
  }
  
  tcgetattr (fd, &Posix_CommConfig);
  cfmakeraw (&Posix_CommConfig);
  Posix_CommConfig.c_cflag |= CREAD | CLOCAL;
  Posix_CommConfig.c_lflag &=
    (~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG));
  Posix_CommConfig.c_iflag &=
    (~(INPCK | IGNPAR | PARMRK | ISTRIP | ICRNL | IXANY));
  Posix_CommConfig.c_oflag &= (~OPOST);

#ifdef _POSIX_VDISABLE
  const long vdisable = fpathconf (fd, _PC_VDISABLE);
  Posix_CommConfig.c_cc[VINTR] = vdisable;
  Posix_CommConfig.c_cc[VQUIT] = vdisable;
  Posix_CommConfig.c_cc[VSTART] = vdisable;
  Posix_CommConfig.c_cc[VSTOP] = vdisable;
  Posix_CommConfig.c_cc[VSUSP] = vdisable;
#endif  
  cReturnCode |= setBaudRate (spSerialSettings->eBaudRate,&Posix_CommConfig);
  cReturnCode |= setDataBits (spSerialSettings->eDataBits,spSerialSettings->eStopBits,
                              &Posix_CommConfig);
  cReturnCode |= setStopBits (spSerialSettings->eStopBits,&Posix_CommConfig);
  cReturnCode |= setParity (spSerialSettings->eParity,&Posix_CommConfig);
  cReturnCode |= setFlowControl (spSerialSettings->eFLowType,&Posix_CommConfig);
  if (cReturnCode != 0)
  {
    fprintf(stderr, "  Error: Could not apply the baudrate settings to the serial port\n");
    close(fd);
    return -4;
  }

  if (spSerialSettings->eFLowType==FLOW_OFF)
  {
    Posix_CommConfig.c_cc[VMIN] = 0; //no block
  }
  else
  {
    Posix_CommConfig.c_cc[VMIN] = 1;  //block
  }
  Posix_CommConfig.c_cc[VTIME] = spSerialSettings->cTimeout; //VTIME in units of 100ms
  int iReturnCode = tcsetattr (fd, TCSAFLUSH, &Posix_CommConfig);
  if (iReturnCode != 0)
  {
    fprintf(stderr, "  Error: Could not apply the flow control settings to the serial port\n");
    close(fd);
    return -4;
  }

  
  memcpy (&sSerialArray[*pcSerialPortHandle].sPortSettings,
          spSerialSettings,
          sizeof (SerialSettings_s)
         );
  sSerialArray[*pcSerialPortHandle].hComPort = fd;
  sSerialArray[*pcSerialPortHandle].bStatus_PortIsOpen = TRUE;

  return 0;
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : SP_ClosePort()
*
*
* DESCRIPTION : This functions closes the open communication port
*
* INPUT PARAMETERS : hComPort - Handle to the opened ComPort.
*
* RETURN PARAMETERS : int8_t  0 : Success
*                            -1 : Illegal input/output parameters (NULL 
*                                 pointers)
*                            -2 : Port is not open.
*                            -3 : Could not terminate the thread in which the 
*                                 serial port lives
*                            -4 : Could not close the connection.
*
* Note :
*
*
******************************************************************************/
int8_t SP_ClosePort (uint8_t * pcSerialPortHandle)
{
  bool_t bReturnCode;
  if ((pcSerialPortHandle == NULL) ||
      (*pcSerialPortHandle >= (MAX_PORTS - 1))) 
  {
    fprintf(stderr,"  Error: Requested slot [%d] out of range\n", *pcSerialPortHandle);
    return -1;
  }
  
  if (sSerialArray[*pcSerialPortHandle].bStatus_PortIsOpen == FALSE) 
  {
    fprintf(stderr,"  Error: Slot [%d] is not an open port\n", *pcSerialPortHandle);
    return -2;
  }
  
  bReturnCode = close(sSerialArray[*pcSerialPortHandle].hComPort);
  if (bReturnCode != 0) 
  {
    fprintf(stderr,"  Error: Could not close '%s', slot [%d]\n", &sSerialArray[*pcSerialPortHandle].sPortSettings.cPCComPortName[0], *pcSerialPortHandle);
    return -4;
  }
  //fprintf(stderr,"  SP_ClosePort() Closed serial port '%s', slot [%d]\n", &sSerialArray[*pcSerialPortHandle].sPortSettings.cPCComPortName[0], *pcSerialPortHandle);

  sSerialArray[*pcSerialPortHandle].sPortSettings.cPCComPortName[0]=0;
  sSerialArray[*pcSerialPortHandle].bStatus_PortIsOpen = FALSE;
  *pcSerialPortHandle=255;

  //Give OS time to release the file descriptor. Immediate open() after close() fails otherwise
  sleep(1);   
  return 0;
}

/*--[ FUNCTION ]***************************************************************
*
* FUNCTION NAME : SP_IsOpen()
*
*
* DESCRIPTION : This function returns if the port is open or not.
*
* INPUT PARAMETERS : *pcSerialPortHandle   - Handle to the ComPort.
*                    *pwThreadCounter - (Unused)
*
* RETURN PARAMETERS : int8_t   1 : Port is open
*                              0 : Port is close
*                             -1 : Illegal input/output parameters (NULL 
*                                  pointers)
*
* Note :
*
*
******************************************************************************/
int8_t SP_IsOpen (uint8_t * pcSerialPortHandle, uint32_t *)
{
  bool_t bReturnCode;
  if ((pcSerialPortHandle == NULL) ||
      (*pcSerialPortHandle >= (MAX_PORTS - 1))) {
    return -1;
  }
  //*pwThreadCounter = sSerialArray[*pcSerialPortHandle].wThreadCounter;
  bReturnCode = sSerialArray[*pcSerialPortHandle].bStatus_PortIsOpen;
  return bReturnCode;
}


// Return codes: >= 0 - actual amount send
//               -1 - Invalid port handle
//               -2 - Specified port is not open
//               -3 - Requested amount of data to send and reported amount mismatch
int16_t SP_Write (uint8_t *pcSerialPortHandle, uint8_t *pcBuffer, int16_t iCount)
{
  #define BLOCKSIZE 100
  int16_t iReturnCode;

  if (*pcSerialPortHandle >= (MAX_PORTS - 1))
  {
    return -1;
  }
  if (sSerialArray[*pcSerialPortHandle].bStatus_PortIsOpen == FALSE)
  {
    return -2;
  }

  
  uint16_t iBlocks = iCount / BLOCKSIZE;
  uint16_t iRemainder = iCount % BLOCKSIZE;
  uint16_t iPosition = 0;
  uint16_t iTotal = 0;

  BaudRateType_e eBaudRate = sSerialArray[ *pcSerialPortHandle ].sPortSettings.eBaudRate;
  float_tt fByteRate;
  switch (eBaudRate)
  {
    case BAUD9600:
      fByteRate=960.0;
      break;
    case BAUD38400:
      fByteRate=3840.0;
      break;
    case  BAUD57600:
      fByteRate=5760.0;
      break;
    case  BAUD230400:
      fByteRate=23040.0;
      break;
    case  BAUD460800:
      fByteRate=46080.0;
      break;
    case  BAUD921600:
      fByteRate=92160.0;
      break;
    case  BAUD115200:
    default:
      //For command port, effectively wait twice as long
      //as the message transmit time when using the value
      //to calculate wSleep.
      fByteRate=5760.0;
      break;
  }

  uint32_t wSleep = (uint32_t)((float_tt)(BLOCKSIZE) / fByteRate * 1000000.0);
  
  for (int iI=0;iI<iBlocks;iI++)
  {
    //fprintf(stderr,"Sending from %d: 512 bytes\n",iPosition);
    iReturnCode = write (sSerialArray[*pcSerialPortHandle].hComPort, &pcBuffer[iPosition], BLOCKSIZE);
    iPosition+=BLOCKSIZE;
    iTotal+=iReturnCode;

    if (iReturnCode != BLOCKSIZE)
    {
      return -3;
    }

    //fprintf(stderr,"  [%d:%d] Sending %d, usleep %d\n",*pcSerialPortHandle, sSerialArray[*pcSerialPortHandle].hComPort,  BLOCKSIZE, wSleep);
    //usleep twice the transmission time as a safety margin for the O/S to clock out the data over the USB subsystem
    usleep( wSleep );
  }
  if (iRemainder!=0)
  {
    
    iReturnCode = write (sSerialArray[*pcSerialPortHandle].hComPort, &pcBuffer[iPosition], iRemainder);
    iTotal+=iReturnCode;
    if (iReturnCode != iRemainder)
    {
      return -3;
    }

    //usleep twice the transmission time as a safety margin for the O/S to clock out the data over the USB subsystem
    usleep( wSleep );
  }  

  return iTotal;
}


int16_t SP_Read (uint8_t * pcSerialPortHandle, uint8_t * pcBuffer, int16_t iCount)
{

  int16_t wBytesRead;

  if (*pcSerialPortHandle >= (MAX_PORTS - 1))
  {
    return -1;
  }
  if (sSerialArray[*pcSerialPortHandle].bStatus_PortIsOpen == FALSE) 
  {
    return -2;
  }

  wBytesRead = read (sSerialArray[*pcSerialPortHandle].hComPort, pcBuffer, iCount);
  return wBytesRead;
}


