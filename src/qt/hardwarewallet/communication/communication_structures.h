#ifndef COMMUNICATION_STRUCTURES_H
#define COMMUNICATION_STRUCTURES_H
#pragma pack(push)
#pragma pack(1)                 /* set alignment to 1 byte boundary */
/*--[ INCLUDE FILES ]--------------------------------------------------------*/
#include "hardwarewallet/BaseTypes.h"
/*--[ DEFINITIONS ]----------------------------------------------------------*/
#define MAX_PACKET_SIZE 1500
/*--[ TYPES ]----------------------------------------------------------------*/

typedef struct
{
  bool_t new_msg_recvd;
  bool_t are_known;
  int elapsed_ticks;
  char const *pReason;          /* reason why are_known is FALSE */
  int wCount_Rx;
  int wCount_Overflow;
} statistics_s;
/*--[ ENUMERATIONS ]---------------------------------------------------------*/


typedef enum
{
  ERROR_framing_unsupported_variant,
  ERROR_framing_unsupported_SID,
  ERROR_framing_unsupported_DID,
  ERROR_framing_framecount_mismatch,
  ERROR_framing_rx_msg_too_big,
  ERROR_framing_state_error,
  ERROR_framing_rx_crc_failed,
  ERROR_messaging_unsupported_MID,
  ERROR_messaging_MID_size_mismatch,
  ERROR_retrieve_data_unsupported_MID,
  ERROR_retrieve_data_cannot_tx,
  ERROR_retrieve_data_cannot_retrieve_data_from_dialog,
  ERROR_calibration_unsupported_MID
} ERROR_e;

#pragma pack(pop)
#endif 
