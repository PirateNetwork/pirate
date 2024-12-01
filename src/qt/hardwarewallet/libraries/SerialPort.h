#ifndef _SERIALPORT_H_
#define _SERIALPORT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef _SERIALPORT_CPP_
  #define _SERIAL_PORT_extern
#else
  #define _SERIAL_PORT_extern extern
#endif

/*--[ INCLUDE FILES ]--------------------------------------------------------*/
#include <stdio.h> 
#include <string.h>
#include <unistd.h>
#include <fcntl.h> 
#include <errno.h> 

#include "hardwarewallet/BaseTypes.h"
#include <stdio.h>
#include <stdlib.h>
/*--[ ENUMERATIONS ]---------------------------------------------------------*/
  typedef enum
  {
    BAUD9600,
    BAUD38400,
    BAUD57600,
    BAUD115200,
    BAUD230400,
    BAUD460800,
    BAUD921600
  } BaudRateType_e;

  typedef enum
  {
    DATA_5,                     
    DATA_6,                     
    DATA_7,                     
    DATA_8                      
  } DataBitsType_e;

  typedef enum
  {
    STOP_1,                     
    STOP_1_5,                   
    STOP_2                      
  } StopBitsType_e;

  typedef enum
  {
    PAR_NONE,                   
    PAR_EVEN,                   
    PAR_ODD                     
  } ParityType_e;

  typedef enum
  {
    FLOW_OFF,                   
    FLOW_HARDWARE,              
    FLOW_XONXOFF                
  } FlowType_e;
/*--[ TYPES ]----------------------------------------------------------------*/ 
  typedef struct
  {
    char cPCComPortName[30];

    BaudRateType_e eBaudRate;
    DataBitsType_e eDataBits;
    StopBitsType_e eStopBits;
    ParityType_e eParity;
    FlowType_e eFLowType;
    uint8_t cTimeout;
  } SerialSettings_s;

/*--[ FUNCTION PROTOTYPES ]--------------------------------------------------*/
  int8_t SP_OpenPort (SerialSettings_s * spSerialSettings,
                      uint8_t * pcSerialPortHandle);
  int8_t SP_ClosePort (uint8_t * pcSerialPortHandle);
  int8_t SP_IsOpen (uint8_t * pcSerialPortHandle, uint32_t *);
  int16_t SP_Write (uint8_t * pcSerialPortHandle, uint8_t * pBuffer,
                     int16_t iCount);
  int16_t SP_Read (uint8_t * pcSerialPortHandle, uint8_t * pBuffer,
                    int16_t iCount);

#ifdef __cplusplus
}
#endif

#endif
