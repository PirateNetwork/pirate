#ifndef _SERIALPORT_I_H_
#define _SERIALPORT_I_H_

#ifndef _SERIALPORT_CPP_
#error This file is private to SerialPort_posix.c
#endif
  
#ifdef __cplusplus
extern "C" 
{  
#endif

#include <signal.h>
#include <termios.h>
  
#define MAX_PORTS 10
  
typedef struct
{
  int hComPort;
  SerialSettings_s sPortSettings;
  bool_t bStatus_PortIsOpen;
  uint8_t bStatus_comms_error;
  bool_t bCloseThread;
} serial_T;
   
#define THREAD_SLEEP_DELAY 25
serial_T sSerialArray[MAX_PORTS];
int8_t cSerialArrayCount = 0;
void *my_thread_func (void *arg);
int8_t setBaudRate (BaudRateType_e baudRate,
                    struct termios *pPosix_CommConfig);
int8_t setDataBits (DataBitsType_e eDataBits, StopBitsType_e eStopBits,
                    struct termios *pPosix_CommConfig);
int8_t setStopBits (StopBitsType_e eStopBits,
                    struct termios *pPosix_CommConfig);
int8_t setParity (ParityType_e eParity,
                  struct termios *pPosix_CommConfig);
#ifdef __cplusplus
} 
#endif
 
#endif
 
