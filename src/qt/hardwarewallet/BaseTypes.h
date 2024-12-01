#ifndef _BASETYPES_H_INCLUDED
#define _BASETYPES_H_INCLUDED



/*--[ INCLUDE FILES ]--------------------------------------------------------*/

/*--[ PREPROCESSOR ]---------------------------------------------------------*/

/*--[ ENUMERATIONS ]---------------------------------------------------------*/

/*--[ DEFINITIONS ]----------------------------------------------------------*/
#ifndef NULL
   #define NULL            0
#endif

/*--[ TYPES ]----------------------------------------------------------------*/

#ifdef QT
   typedef bool                 bool_t;
#else
   typedef unsigned char         bool_t;
#endif

typedef char                  char_t;

typedef signed char           int8_t;
typedef signed short int      int16_t;



typedef unsigned char         uint8_t;
typedef unsigned short int    uint16_t;
typedef unsigned int          uint32_t;



typedef float                 float_tt;


#ifndef TRUE
   #define FALSE           ((bool_t)0)
   #define TRUE            ((bool_t)1)
#endif

/*---------------------------------------------------------------------------*/
#endif


