#include "checksums.h"

#define UTL_CRC32_INITIAL_SEED    0xFFFFFFFFu

/*--[ FUNCTION ]-------------------------------------------------------------------------------------------------------
 *
 * FUNCTION NAME        : CalcCrc8()
 *
 * DESCRIPTION          : This function uses the predefined table to calculate an 8-bit CRC.
 *
 * INPUT PARAMETERS     : classUtils* this                      - Object Pointer
 *                        L1_UINT8 *pbData                       - Pointer to data of which CRC is to be calculated
 *                        L1_UINT32 lSize                        - Size (in bytes) of data
 *
 * OUTPUT PARAMETERS    : L1_UINT8 *pbCRC                        - Pointer to variable to contain the calculated CRC
 *
 * RETURN PARAMETERS    : 0 - Success
 *
 * REMARKS              :
 *
 *
 *-------------------------------------------------------------------------------------------------------------------*/
L1_INT8 CalcCrc8( const L1_UINT8 *pbData, L1_UINT32 lSize, L1_UINT8 *pbCRC )
{
  /*-------------------------------------------------------------------------------------------------------------------
   * Variable Declaration
   *-----------------------------------------------------------------------------------------------------------------*/
  L1_INT8  eRet;
  L1_UINT8  bCRC;
  L1_UINT8  bTblIdx;
  L1_UINT32 lDataIdx;
  L1_UINT8  bTmp;

  /*-------------------------------------------------------------------------------------------------------------------
   * Variable Initialisation
   *-----------------------------------------------------------------------------------------------------------------*/
  eRet     = 0;
  bCRC     = 0u;
  bTblIdx  = 0u;
  lDataIdx = 0u;
  /*---------------------------------------------------------------------------------------------------------------
   * Calculate CRC of given data
   *-------------------------------------------------------------------------------------------------------------*/
  for( lDataIdx = 0u; lDataIdx < lSize; lDataIdx++ )
  {
    bTmp = pbData[ lDataIdx ];
    bTblIdx = ( bCRC ^ bTmp );
    bCRC    = abCrc8Table[ bTblIdx ];
  }
  /*---------------------------------------------------------------------------------------------------------------
   * Copy CRC to output variable
   *-------------------------------------------------------------------------------------------------------------*/
  *pbCRC = bCRC;

  /*-------------------------------------------------------------------------------------------------------------------
   * Return
   *-----------------------------------------------------------------------------------------------------------------*/
  return ( eRet );
}
/*--[ FUNCTION ]-------------------------------------------------------------------------------------------------------
 *
 * FUNCTION NAME        : CalcCrc16()
 *
 * DESCRIPTION          : This function uses the predefined table to calculate an 16-bit CRC.
 *
 * INPUT PARAMETERS     : classUtils* this                      - Object Pointer
 *                        L1_UINT8 *pbData                       - Pointer to data of which CRC is to be calculated
 *                        L1_UINT32 lSize                        - Size (in bytes) of data
 *
 * OUTPUT PARAMETERS    : L1_UINT16 *pwCRC                       - Pointer to variable to contain the calculated CRC
 *
 * RETURN PARAMETERS    : 0 - Success
 *
 * REMARKS              :
 *
 *
 *-------------------------------------------------------------------------------------------------------------------*/
L1_INT8 CalcCrc16( const L1_UINT8 *pbData, L1_UINT32 lSize, L1_UINT16 *pwCRC )
{
  /*-------------------------------------------------------------------------------------------------------------------
  * Variable Declaration
  *------------------------------------------------------------------------------------------------------------------*/
  L1_INT8   eRet;
  L1_UINT16  wCRC;
  L1_UINT8   bTblIdx;
  L1_UINT32  lDataIdx;
  /*-------------------------------------------------------------------------------------------------------------------
   * Variable Initialization
   *-----------------------------------------------------------------------------------------------------------------*/
  eRet     = 0;
  wCRC     = 0u;        /* CCITT initial value */
  bTblIdx  = 0u;
  lDataIdx = 0u;

  for( lDataIdx = 0; lDataIdx < lSize; lDataIdx++ )
  {
    bTblIdx = (L1_UINT8)( ( ( wCRC >> 8u ) ^ pbData[ lDataIdx ] ) & 0xFF );
    wCRC    = (L1_UINT16)( ( wCRC << 8u ) ^ crc16_table[ bTblIdx ] );
  }
  *pwCRC = wCRC;

  return( eRet );
}

/*--[ FUNCTION ]-------------------------------------------------------------------------------------------------------
 *
 * FUNCTION NAME        : CalcCrc32()
 *
 * DESCRIPTION          : This function uses the predefined table to calculate an 32-bit CRC.
 *
 * INPUT PARAMETERS     : classUtils* this                       - Object Pointer
 *                        L1_UINT8* pbData                        - pointer to data of which CRC is to be calculated
 *                        L1_UINT32 lSize                         - size (in bytes) of data
 *                        L1_UINT32 lSeed
 *
 * OUTPUT PARAMETERS    : L1_UINT32* plCRC (output)               - pointer to variable to contain the calculated CRC
 *
 * RETURN PARAMETERS    : 0 - Success
 *
 * REMARKS              : Start with Seed of 0xFFFFFFFFu
 *
 *
 *-------------------------------------------------------------------------------------------------------------------*/
L1_INT8 CalcCrc32( const L1_UINT8 *pbData, L1_UINT32 lSize, L1_UINT32 lSeed, L1_UINT32 *plCRC )
{
  /*-------------------------------------------------------------------------------------------------------------------
   * Variable Declaration
   *-----------------------------------------------------------------------------------------------------------------*/
  L1_INT8   eRet;
  L1_UINT32  lCRC;
  L1_UINT8   bTblIdx;
  L1_UINT32  lDataIdx;
  /*-------------------------------------------------------------------------------------------------------------------
   * Variable Initialization
   *-----------------------------------------------------------------------------------------------------------------*/
  eRet     = 0;
  bTblIdx  = 0u;
  lDataIdx = 0u;
  /*-------------------------------------------------------------------------------------------------------------------
   * Set initial CRC value
   *-----------------------------------------------------------------------------------------------------------------*/
  if( lSeed != UTL_CRC32_INITIAL_SEED)
  {
    lCRC = lSeed ^ UTL_CRC32_INITIAL_SEED;
  }
  else
  {
    lCRC = lSeed;
  }

  for( lDataIdx = 0; lDataIdx < lSize; lDataIdx++ )
  {
    bTblIdx = ( L1_UINT8 )( ( lCRC ^ pbData[ lDataIdx ] ) & 0xFFu );
    lCRC    = ( lCRC >> 8u ) ^ crc32_table[ bTblIdx ];
  }
  *plCRC  = lCRC ^ UTL_CRC32_INITIAL_SEED;

  return( eRet );
}


/*--[ FUNCTION ]-[ PRIVATE ]-------------------------------------------------------------------------------------------
 *
 * FUNCTION NAME      : CalcXor()
 *
 * DESCRIPTION        : This function is responsible for calculating a checksum by performing an XOR on all data in the
 *                      buffer passed.
 *
 * INPUT PARAMETERS   : classSightsController *this               - Object pointer
 *                      L1_UINT8               *pbBuffer           - Pointer to buffer containing data to be XOR'ed
 *                      L1_UINT32              lNumOfBytes         - Number of bytes in buffer
 *
 * OUTPUT PARAMETERS  : L1_UINT8               *pbChecksum         - Calculated checksum
 *
 * RETURN PARAMETERS  : ERRNO_t                                   - eNoError
 *                                                                - eErrThisPointerIsNull
 *
 * REMARKS            :
 *
 *
 *-------------------------------------------------------------------------------------------------------------------*/
L1_INT8 CalcXor( L1_UINT8 *pbBuffer, L1_UINT32 lNumOfBytes, L1_UINT8 *pbChecksum )
{
  /*-------------------------------------------------------------------------------------------------------------------
   * Variable Declaration
   *-----------------------------------------------------------------------------------------------------------------*/
  L1_INT8                 eRet;
  L1_UINT32               lCount;

  /*-------------------------------------------------------------------------------------------------------------------
   * Variable Initialization
   *-----------------------------------------------------------------------------------------------------------------*/
  eRet                    = 0;
  lCount                  = 0;

  /*-----------------------------------------------------------------------------------------------------------------
   * XOR byte by byte
   *---------------------------------------------------------------------------------------------------------------*/
  *pbChecksum = 0u;
  for( lCount = 0u; lCount < lNumOfBytes; lCount++ )
  {
    *pbChecksum ^= pbBuffer[ lCount ];
  }

  /*-------------------------------------------------------------------------------------------------------------------
   * Return
   *-----------------------------------------------------------------------------------------------------------------*/
  return( eRet );
}

/*--[ FUNCTION ]-------------------------------------------------------------------------------------------------------
 *
 * FUNCTION NAME        : CalcSUM8()
 *
 * DESCRIPTION          : This function calculates the summation checksum of the input array
 *                      : for an 8 bit result, in the fasion of the LMCU
 *
 * INPUT PARAMETERS     : *pbData                       - Pointer to data of which checksum is to be calculated
 *                        lSize                         - Size (in bytes) of data
 *                        wCalcValue                    - Initial seed
 *
 * OUTPUT PARAMETERS    : *pbSUM                        - Pointer to variable to contain the calculated SUM
 *
 * RETURN PARAMETERS    : bool_t                        - eL1_TRUE - Calculation completed
 *
 * REMARKS              : This function is used to calculate the checksum of the LMCU messages
 *
 *-------------------------------------------------------------------------------------------------------------------*/
L1_BOOL CalcSUM8(  const L1_UINT8  *pbData, L1_UINT32 lSize, L1_UINT16 wCalcValue, L1_UINT8 *pbSUM )
{
  L1_UINT8 bTempChecksum=0;
  L1_UINT8 bSum;
  L1_UINT32 lI;
  L1_UINT8 bTmp;

  bTempChecksum = 0u;
  for ( lI = 0; lI < lSize; lI++ )
  {
    bTmp = pbData[lI];
    bTempChecksum += bTmp;
  }
  bSum = ( L1_UINT8 ) ( wCalcValue - ( (L1_UINT16)bTempChecksum % wCalcValue ) );

  *pbSUM = bSum;
  //*pbSUM = bSum-1; //TBD: Is this correct? LMCU seems to require this. Double check LMCU vs CalcSUM
  return (L1_TRUE);
}

/*--[ FUNCTION ]-------------------------------------------------------------------------------------------------------
 *
 * FUNCTION NAME        : CalcSUM16()
 *
 * DESCRIPTION          : This function calculates the summation checksum of the input array
 *                      : for an 16 bit result, in the fasion of the SGOU
 *
 * INPUT PARAMETERS     : *pbData                       - Pointer to data of which checksum is to be calculated
 *                        lSize                         - Size (in bytes) of data
 *                        wSeed                         - Initial seed
 *
 * OUTPUT PARAMETERS    : *pbSUM                        - Pointer to variable to contain the calculated SUM
 *
 * RETURN PARAMETERS    : bool_t                        - eL1_TRUE - Calculation completed
 *
 * REMARKS              : This function is used to calculate the checksum of the LMCU messages
 *
 *-------------------------------------------------------------------------------------------------------------------*/
L1_BOOL CalcSUM16(  const L1_UINT8  *pbData, L1_UINT32 lSize, L1_UINT16 wSeed, L1_UINT16 *pwSUM )
{
  L1_UINT16 wTempChecksum=0;
  L1_UINT16 wSum;
  L1_UINT32 lI;
  L1_UINT16 wTmp;

  for ( lI = 0; lI < lSize; lI++ )
  {
    wTmp = pbData[lI];
    wTempChecksum += wTmp;
  }
  wSum = ( L1_UINT16 ) ( wTempChecksum );

  *pwSUM =  wSeed & wSum;
  return (L1_TRUE);
}
