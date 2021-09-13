#include <gtest/gtest.h>

extern "C"
{
#include <crypto/haraka.h>
}

#include <iostream>
#include <cstdio>

namespace TestHaraka
{

TEST(TestHaraka, trunc)
{
    unsigned char expected[64] = 
            { 0xb0, 0x3d, 0x96, 0x61, 0xbd, 0x9b, 0x57, 0xd4, 
              0x48, 0xbc, 0xd7, 0x93, 0xfe, 0x23, 0x9e, 0x3b, 
              0x1d, 0x94, 0xc0, 0xb5, 0xc0, 0x16, 0x65, 0x40, 
              0xf1, 0x6c, 0x7d, 0x3a, 0xc7, 0x66, 0x84, 0x18};
    unsigned char in[128];
    unsigned char out[128];
    memset(out, 0, 128);

    for(int i = 0; i < 128; ++i)
        in[i] = i;

    // try to hash
    haraka512_zero(out, in);

    for(int i = 0; i < 128; ++i)
        printf("%02x", out[i]);
    
    printf("\n");

    for(int i = 0; i < 32; ++i)
        EXPECT_EQ(out[i], expected[i]);

}

} // namespace TestHaraka