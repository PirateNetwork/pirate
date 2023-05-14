#pragma once
#include <cstdint>

#define NUM_KMD_SEASONS 7
#define NUM_KMD_NOTARIES 64

extern const uint32_t nStakedDecemberHardforkTimestamp; //December 2019 hardfork
extern const int32_t nDecemberHardforkHeight;   //December 2019 hardfork

extern const uint32_t nS4Timestamp; //dPoW Season 4 2020 hardfork
extern const int32_t nS4HardforkHeight;   //dPoW Season 4 2020 hardfork

extern const uint32_t nS5Timestamp; //dPoW Season 5 June 14th, 2021 hardfork (03:00:00 PM UTC) (defined in komodo_globals.h)
extern const int32_t nS5HardforkHeight;   //dPoW Season 5 June 14th, 2021 hardfork estimated block height (defined in komodo_globals.h)

extern const uint32_t nS6Timestamp;       // dPoW Season 6, Fri Jun 24 2022 13:37:33 GMT+0000
extern const int32_t nS6HardforkHeight;   // dPoW Season 6, Fri Jun 24 2022

static const uint32_t KMD_SEASON_TIMESTAMPS[NUM_KMD_SEASONS] = {1525132800, 1563148800, nStakedDecemberHardforkTimestamp, nS4Timestamp, nS5Timestamp, nS6Timestamp, 1751328000};
static const int32_t KMD_SEASON_HEIGHTS[NUM_KMD_SEASONS] = {814000, 1444000, nDecemberHardforkHeight, nS4HardforkHeight, nS5HardforkHeight, nS6HardforkHeight, 7113400};

extern char NOTARYADDRS[64][64];
extern char NOTARY_ADDRESSES[NUM_KMD_SEASONS][64][64];

extern const char *notaries_elected[NUM_KMD_SEASONS][NUM_KMD_NOTARIES][2];
