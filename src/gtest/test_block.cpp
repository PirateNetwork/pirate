#include <gtest/gtest.h>

#include "primitives/block.h"
#include "streams.h"
#include "version.h"


TEST(BlockTests, HeaderSizeIsExpected) {
    // Dummy header with an empty Equihash solution.
    CBlockHeader header;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << header;

    auto headerSize = CBlockHeader::HEADER_SIZE + 1; 
    //+1 for the data stream header of 1 byte
    ASSERT_EQ(ss.size(), headerSize);
    
}
