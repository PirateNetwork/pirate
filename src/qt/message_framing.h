#ifndef _MsgFrame_Widget_H_
#define _MsgFrame_Widget_H_
/*--[ INCLUDE FILES ]--------------------------------------------------------*/
#include <QHash>
#include <QWidget>
#include <QTimer>
#include "hardwarewallet/BaseTypes.h"
#include "hardwarewallet/libraries/SerialPort.h"

QT_BEGIN_NAMESPACE 
  class Ui_MsgFrame_Form;
QT_END_NAMESPACE 

// PC:     0x00 .. 0x7F
// PC/APP: 0x80 .. 0xFF
#define MSGID_HANDSHAKE                       0x01
#define MSGID_SESSIONKEY                      0x02
#define MSGID_PINGPONG                        0x03
#define MSGID_ERROR                           0x04

#define MSGID_LOGIN 	                        0x11
#define MSGID_LOGIN_ACK                       0x12
#define MSGID_LOGOUT                          0x13
#define MSGID_SELECT_PROJECT                  0x14
#define MSGID_SELECT_PROJECT_ACK              0x15

#define MSGID_DECRYPT                         0x16 //Dero: balance decrypt
#define MSGID_DECRYPT_ACK                     0x17

#define MSGID_SIGN                            0x20
#define MSGID_SIGN_NAVIGATE                   0x21
#define MSGID_SIGN_OTP                        0x22
#define MSGID_SIGN_ACK                        0x23

#define MSGID_RETRIEVE_ADDRESS                0x25
#define MSGID_RETRIEVE_ADDRESS_OTP            0x26

#define MSGID_REGISTRATION                    0x27  //Dero: generate registration transaction
#define MSGID_REGISTRATION_OTP                0x28
#define MSGID_REGISTRATION_ACK                0x29

#define MSGID_GEN_MNEMONIC                    0x30  //Generate a new mnemonic
#define MSGID_GEN_MNEMONIC_PREVIOUS           0x31  //Show previous word
#define MSGID_GEN_MNEMONIC_NEXT               0x32  //Show next word

#define MSGID_VERIFY_MNEMONIC                 0x33  //Verify the new mnemonic
#define MSGID_VERIFY_MNEMONIC_PREVIOUS        0x34  //Verify new mnemonic: previous word
#define MSGID_VERIFY_MNEMONIC_NEXT            0x35  //Verify new mnemonic: next word

#define MSGID_RESTORE_MNEMONIC                0x36 //Restore a previously generated mnemonic
#define MSGID_RESTORE_MNEMONIC_PREVIOUS       0x37 //Previous word position in the list of 24 words
#define MSGID_RESTORE_MNEMONIC_NEXT           0x38 //Next word position in the list of 24 words
#define MSGID_RESTORE_MNEMONIC_CLEAR          0x39 //Clear the entered word
#define MSGID_RESTORE_MNEMONIC_FIRST_4_CHARS  0x3a //Supply first 4 chars of a mnemonic


#define MSGID_GENERATE_PASSWORD               0x42 //Generate new password
#define MSGID_VERIFY_PASSWORD                 0x43 //Verify the password
                                                   // [0]=0x55 [1]=0xAA : Reset the password grid.
                                                   // [0]=0xEE [1]=0xBB : Submit

                                                   // [0]=0x33 [1]=0x01 : Up
                                                   // [0]=0x33 [1]=0x02 : Down
                                                   // [0]=0x33 [1]=0x03 : Left
                                                   // [0]=0x33 [1]=0x04 : Right


#define MSGID_APPLY_CONFIG                    0x50 //Apply full configuration to the unit
#define MSGID_REG_STATUS                      0x51 //Registration response from unit


#define MSGID_UPGRADE                         0x60  //Upgrade mechanism
                                                    //[0]:1=Start file transfer
                                                    //    [1]:Communication version
                                                    //    [2]:Application version
                                                    //   :2=Status
                                                    //   :3=Deploy upgrade
#define MSGID_UPGRADE_STATUS                  0x61  //Response to the upgrade commands
                                                    //[0]:1=Transfer was started                            /control/receive_started
                                                    //   :2=Transfer in progress                                     /control/progress
                                                    //   :3=Transfer aborted                                         (/control/progress contents/)
                                                    //   :4=Idle, Transfer complete, waiting for verification        /control/receive_verify
                                                    //
                                                    //   :5=Idle, Transfer completed, archive missing                /control/receive_missing
                                                    //   :6=Idle, Transfer completed, file integrity failed          /control/receive_integrity_fail
                                                    //   :7=Idle, Transfer completed, version too low                /control/receive_version_fail
                                                    //   :8=Idle, Transfer complete,  archive available for upgrade  /control/receive_completed
                                                    //   :9=Idle, No valid archive available for upgrade             /control is empty
                                                    //   :10=Upgrade initiated


#define COMMS_RX_BUFFER_SIZE 10000

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

class MsgFrame_Widget:public QWidget
{
  Q_OBJECT
public:

  /*--[ FUNCTION PROTOTYPES ]--------------------------------------------------*/
  MsgFrame_Widget (QWidget * parent = nullptr);
  ~MsgFrame_Widget ();

  int8_t OpenSerialConnection (char *pcDeviceName,
                               uint8_t cDeviceName_length);
  int8_t CloseSerialConnection ();



  int8_t Pack (uint8_t cMsgID, uint8_t *pcaData, uint16_t iLength);
  int8_t GetFrame (uint16_t *piMsgID, uint8_t * pcBuf, uint16_t iBufSize,
                   uint16_t * piMsgLength);
  bool_t Status (void);

  int8_t Handshake(void);

public Q_SLOTS:
private Q_SLOTS:
  void qSerialTimer_timeout(void);
Q_SIGNALS:
  /*--[ FUNCTION PROTOTYPES ]--------------------------------------------------*/
  void signal_FrameDetected (uint8_t, uint8_t*, uint16_t); //cMessageID, cBuffer, iBufferLength
  //void signal_ConnectionStatus (uint16_t, bool_t);
  //void signal_Errors (ERROR_e);
private:
  /*--[ ENUMERATIONS ]---------------------------------------------------------*/
  /*--[ DEFINITIONS ]----------------------------------------------------------*/


  /*--[ TYPES ]----------------------------------------------------------------*/
  typedef struct
  {
    uint16_t iSuccess;
    uint16_t iBufferTooSmall;
    uint16_t iChecksFailed;
    uint16_t iCRCFailed;
  } MsgFrame_Rx_Counters_s;

  typedef struct
  {
    uint16_t iSuccess;
    uint16_t iBufferTooSmall;
    uint16_t iLowLevelError;
  } MsgFrame_Tx_Counters_s;

  /*--[ GLOBAL VARIABLES ]-----------------------------------------------------*/
  bool bConnectionType;
  uint8_t cSerialPortHandle=255;
  QTimer *qSerialTimer;
  uint16_t iPort;

  uint8_t    cInternalBufferState=RxNOSYNC;
  uint8_t    cInternalMsgID;
  uint16_t   iInternalCRC;
  uint8_t    cInternalBuffer[COMMS_RX_BUFFER_SIZE+7];
  uint16_t   iInternalBufferPos=0;
  uint16_t   iInternalBufferLength=0;


  #define BUFLEN COMMS_RX_BUFFER_SIZE
  uint8_t  cNwBuf[BUFLEN];
  uint16_t iNwBufReadPos;
  uint16_t iNwBufWritePos;

  /*--[ FUNCTION PROTOTYPES ]--------------------------------------------------*/
  void Frameing (void);

};
#endif
