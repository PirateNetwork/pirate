#include <gtest/gtest.h>
#include "komodo_utils.h"
#include <cstring>
#include "hash.h"
#include "random.h"

namespace TestOldHashRemoval {

    /* old rmd160 implementation derived from LibTomCrypt */

    #define MIN(x, y) ( ((x)<(y))?(x):(y) )

    #define STORE32L(x, y)                                                                     \
    { (y)[3] = (uint8_t)(((x)>>24)&255); (y)[2] = (uint8_t)(((x)>>16)&255);   \
    (y)[1] = (uint8_t)(((x)>>8)&255); (y)[0] = (uint8_t)((x)&255); }

    #define LOAD32L(x, y)                            \
    { x = (uint32_t)(((uint64_t)((y)[3] & 255)<<24) | \
    ((uint32_t)((y)[2] & 255)<<16) | \
    ((uint32_t)((y)[1] & 255)<<8)  | \
    ((uint32_t)((y)[0] & 255))); }

    #define STORE64L(x, y)                                                                     \
    { (y)[7] = (uint8_t)(((x)>>56)&255); (y)[6] = (uint8_t)(((x)>>48)&255);   \
    (y)[5] = (uint8_t)(((x)>>40)&255); (y)[4] = (uint8_t)(((x)>>32)&255);   \
    (y)[3] = (uint8_t)(((x)>>24)&255); (y)[2] = (uint8_t)(((x)>>16)&255);   \
    (y)[1] = (uint8_t)(((x)>>8)&255); (y)[0] = (uint8_t)((x)&255); }

    // rmd160: the five basic functions F(), G() and H()
    #define F(x, y, z)        ((x) ^ (y) ^ (z))
    #define G(x, y, z)        (((x) & (y)) | (~(x) & (z)))
    #define H(x, y, z)        (((x) | ~(y)) ^ (z))
    #define I(x, y, z)        (((x) & (z)) | ((y) & ~(z)))
    #define J(x, y, z)        ((x) ^ ((y) | ~(z)))
    #define ROLc(x, y) ( (((unsigned long)(x)<<(unsigned long)((y)&31)) | (((unsigned long)(x)&0xFFFFFFFFUL)>>(unsigned long)(32-((y)&31)))) & 0xFFFFFFFFUL)

    /* the ten basic operations FF() through III() */
    #define FF(a, b, c, d, e, x, s)        \
    (a) += F((b), (c), (d)) + (x);\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    #define GG(a, b, c, d, e, x, s)        \
    (a) += G((b), (c), (d)) + (x) + 0x5a827999UL;\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    #define HH(a, b, c, d, e, x, s)        \
    (a) += H((b), (c), (d)) + (x) + 0x6ed9eba1UL;\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    #define II(a, b, c, d, e, x, s)        \
    (a) += I((b), (c), (d)) + (x) + 0x8f1bbcdcUL;\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    #define JJ(a, b, c, d, e, x, s)        \
    (a) += J((b), (c), (d)) + (x) + 0xa953fd4eUL;\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    #define FFF(a, b, c, d, e, x, s)        \
    (a) += F((b), (c), (d)) + (x);\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    #define GGG(a, b, c, d, e, x, s)        \
    (a) += G((b), (c), (d)) + (x) + 0x7a6d76e9UL;\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    #define HHH(a, b, c, d, e, x, s)        \
    (a) += H((b), (c), (d)) + (x) + 0x6d703ef3UL;\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    #define III(a, b, c, d, e, x, s)        \
    (a) += I((b), (c), (d)) + (x) + 0x5c4dd124UL;\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    #define JJJ(a, b, c, d, e, x, s)        \
    (a) += J((b), (c), (d)) + (x) + 0x50a28be6UL;\
    (a) = ROLc((a), (s)) + (e);\
    (c) = ROLc((c), 10);

    static int32_t rmd160_vcompress(struct rmd160_vstate *md,uint8_t *buf)
    {
        uint32_t aa,bb,cc,dd,ee,aaa,bbb,ccc,ddd,eee,X[16];
        int i;

        /* load words X */
        for (i = 0; i < 16; i++){
            LOAD32L(X[i], buf + (4 * i));
        }

        /* load state */
        aa = aaa = md->state[0];
        bb = bbb = md->state[1];
        cc = ccc = md->state[2];
        dd = ddd = md->state[3];
        ee = eee = md->state[4];

        /* round 1 */
        FF(aa, bb, cc, dd, ee, X[ 0], 11);
        FF(ee, aa, bb, cc, dd, X[ 1], 14);
        FF(dd, ee, aa, bb, cc, X[ 2], 15);
        FF(cc, dd, ee, aa, bb, X[ 3], 12);
        FF(bb, cc, dd, ee, aa, X[ 4],  5);
        FF(aa, bb, cc, dd, ee, X[ 5],  8);
        FF(ee, aa, bb, cc, dd, X[ 6],  7);
        FF(dd, ee, aa, bb, cc, X[ 7],  9);
        FF(cc, dd, ee, aa, bb, X[ 8], 11);
        FF(bb, cc, dd, ee, aa, X[ 9], 13);
        FF(aa, bb, cc, dd, ee, X[10], 14);
        FF(ee, aa, bb, cc, dd, X[11], 15);
        FF(dd, ee, aa, bb, cc, X[12],  6);
        FF(cc, dd, ee, aa, bb, X[13],  7);
        FF(bb, cc, dd, ee, aa, X[14],  9);
        FF(aa, bb, cc, dd, ee, X[15],  8);

        /* round 2 */
        GG(ee, aa, bb, cc, dd, X[ 7],  7);
        GG(dd, ee, aa, bb, cc, X[ 4],  6);
        GG(cc, dd, ee, aa, bb, X[13],  8);
        GG(bb, cc, dd, ee, aa, X[ 1], 13);
        GG(aa, bb, cc, dd, ee, X[10], 11);
        GG(ee, aa, bb, cc, dd, X[ 6],  9);
        GG(dd, ee, aa, bb, cc, X[15],  7);
        GG(cc, dd, ee, aa, bb, X[ 3], 15);
        GG(bb, cc, dd, ee, aa, X[12],  7);
        GG(aa, bb, cc, dd, ee, X[ 0], 12);
        GG(ee, aa, bb, cc, dd, X[ 9], 15);
        GG(dd, ee, aa, bb, cc, X[ 5],  9);
        GG(cc, dd, ee, aa, bb, X[ 2], 11);
        GG(bb, cc, dd, ee, aa, X[14],  7);
        GG(aa, bb, cc, dd, ee, X[11], 13);
        GG(ee, aa, bb, cc, dd, X[ 8], 12);

        /* round 3 */
        HH(dd, ee, aa, bb, cc, X[ 3], 11);
        HH(cc, dd, ee, aa, bb, X[10], 13);
        HH(bb, cc, dd, ee, aa, X[14],  6);
        HH(aa, bb, cc, dd, ee, X[ 4],  7);
        HH(ee, aa, bb, cc, dd, X[ 9], 14);
        HH(dd, ee, aa, bb, cc, X[15],  9);
        HH(cc, dd, ee, aa, bb, X[ 8], 13);
        HH(bb, cc, dd, ee, aa, X[ 1], 15);
        HH(aa, bb, cc, dd, ee, X[ 2], 14);
        HH(ee, aa, bb, cc, dd, X[ 7],  8);
        HH(dd, ee, aa, bb, cc, X[ 0], 13);
        HH(cc, dd, ee, aa, bb, X[ 6],  6);
        HH(bb, cc, dd, ee, aa, X[13],  5);
        HH(aa, bb, cc, dd, ee, X[11], 12);
        HH(ee, aa, bb, cc, dd, X[ 5],  7);
        HH(dd, ee, aa, bb, cc, X[12],  5);

        /* round 4 */
        II(cc, dd, ee, aa, bb, X[ 1], 11);
        II(bb, cc, dd, ee, aa, X[ 9], 12);
        II(aa, bb, cc, dd, ee, X[11], 14);
        II(ee, aa, bb, cc, dd, X[10], 15);
        II(dd, ee, aa, bb, cc, X[ 0], 14);
        II(cc, dd, ee, aa, bb, X[ 8], 15);
        II(bb, cc, dd, ee, aa, X[12],  9);
        II(aa, bb, cc, dd, ee, X[ 4],  8);
        II(ee, aa, bb, cc, dd, X[13],  9);
        II(dd, ee, aa, bb, cc, X[ 3], 14);
        II(cc, dd, ee, aa, bb, X[ 7],  5);
        II(bb, cc, dd, ee, aa, X[15],  6);
        II(aa, bb, cc, dd, ee, X[14],  8);
        II(ee, aa, bb, cc, dd, X[ 5],  6);
        II(dd, ee, aa, bb, cc, X[ 6],  5);
        II(cc, dd, ee, aa, bb, X[ 2], 12);

        /* round 5 */
        JJ(bb, cc, dd, ee, aa, X[ 4],  9);
        JJ(aa, bb, cc, dd, ee, X[ 0], 15);
        JJ(ee, aa, bb, cc, dd, X[ 5],  5);
        JJ(dd, ee, aa, bb, cc, X[ 9], 11);
        JJ(cc, dd, ee, aa, bb, X[ 7],  6);
        JJ(bb, cc, dd, ee, aa, X[12],  8);
        JJ(aa, bb, cc, dd, ee, X[ 2], 13);
        JJ(ee, aa, bb, cc, dd, X[10], 12);
        JJ(dd, ee, aa, bb, cc, X[14],  5);
        JJ(cc, dd, ee, aa, bb, X[ 1], 12);
        JJ(bb, cc, dd, ee, aa, X[ 3], 13);
        JJ(aa, bb, cc, dd, ee, X[ 8], 14);
        JJ(ee, aa, bb, cc, dd, X[11], 11);
        JJ(dd, ee, aa, bb, cc, X[ 6],  8);
        JJ(cc, dd, ee, aa, bb, X[15],  5);
        JJ(bb, cc, dd, ee, aa, X[13],  6);

        /* parallel round 1 */
        JJJ(aaa, bbb, ccc, ddd, eee, X[ 5],  8);
        JJJ(eee, aaa, bbb, ccc, ddd, X[14],  9);
        JJJ(ddd, eee, aaa, bbb, ccc, X[ 7],  9);
        JJJ(ccc, ddd, eee, aaa, bbb, X[ 0], 11);
        JJJ(bbb, ccc, ddd, eee, aaa, X[ 9], 13);
        JJJ(aaa, bbb, ccc, ddd, eee, X[ 2], 15);
        JJJ(eee, aaa, bbb, ccc, ddd, X[11], 15);
        JJJ(ddd, eee, aaa, bbb, ccc, X[ 4],  5);
        JJJ(ccc, ddd, eee, aaa, bbb, X[13],  7);
        JJJ(bbb, ccc, ddd, eee, aaa, X[ 6],  7);
        JJJ(aaa, bbb, ccc, ddd, eee, X[15],  8);
        JJJ(eee, aaa, bbb, ccc, ddd, X[ 8], 11);
        JJJ(ddd, eee, aaa, bbb, ccc, X[ 1], 14);
        JJJ(ccc, ddd, eee, aaa, bbb, X[10], 14);
        JJJ(bbb, ccc, ddd, eee, aaa, X[ 3], 12);
        JJJ(aaa, bbb, ccc, ddd, eee, X[12],  6);

        /* parallel round 2 */
        III(eee, aaa, bbb, ccc, ddd, X[ 6],  9);
        III(ddd, eee, aaa, bbb, ccc, X[11], 13);
        III(ccc, ddd, eee, aaa, bbb, X[ 3], 15);
        III(bbb, ccc, ddd, eee, aaa, X[ 7],  7);
        III(aaa, bbb, ccc, ddd, eee, X[ 0], 12);
        III(eee, aaa, bbb, ccc, ddd, X[13],  8);
        III(ddd, eee, aaa, bbb, ccc, X[ 5],  9);
        III(ccc, ddd, eee, aaa, bbb, X[10], 11);
        III(bbb, ccc, ddd, eee, aaa, X[14],  7);
        III(aaa, bbb, ccc, ddd, eee, X[15],  7);
        III(eee, aaa, bbb, ccc, ddd, X[ 8], 12);
        III(ddd, eee, aaa, bbb, ccc, X[12],  7);
        III(ccc, ddd, eee, aaa, bbb, X[ 4],  6);
        III(bbb, ccc, ddd, eee, aaa, X[ 9], 15);
        III(aaa, bbb, ccc, ddd, eee, X[ 1], 13);
        III(eee, aaa, bbb, ccc, ddd, X[ 2], 11);

        /* parallel round 3 */
        HHH(ddd, eee, aaa, bbb, ccc, X[15],  9);
        HHH(ccc, ddd, eee, aaa, bbb, X[ 5],  7);
        HHH(bbb, ccc, ddd, eee, aaa, X[ 1], 15);
        HHH(aaa, bbb, ccc, ddd, eee, X[ 3], 11);
        HHH(eee, aaa, bbb, ccc, ddd, X[ 7],  8);
        HHH(ddd, eee, aaa, bbb, ccc, X[14],  6);
        HHH(ccc, ddd, eee, aaa, bbb, X[ 6],  6);
        HHH(bbb, ccc, ddd, eee, aaa, X[ 9], 14);
        HHH(aaa, bbb, ccc, ddd, eee, X[11], 12);
        HHH(eee, aaa, bbb, ccc, ddd, X[ 8], 13);
        HHH(ddd, eee, aaa, bbb, ccc, X[12],  5);
        HHH(ccc, ddd, eee, aaa, bbb, X[ 2], 14);
        HHH(bbb, ccc, ddd, eee, aaa, X[10], 13);
        HHH(aaa, bbb, ccc, ddd, eee, X[ 0], 13);
        HHH(eee, aaa, bbb, ccc, ddd, X[ 4],  7);
        HHH(ddd, eee, aaa, bbb, ccc, X[13],  5);

        /* parallel round 4 */
        GGG(ccc, ddd, eee, aaa, bbb, X[ 8], 15);
        GGG(bbb, ccc, ddd, eee, aaa, X[ 6],  5);
        GGG(aaa, bbb, ccc, ddd, eee, X[ 4],  8);
        GGG(eee, aaa, bbb, ccc, ddd, X[ 1], 11);
        GGG(ddd, eee, aaa, bbb, ccc, X[ 3], 14);
        GGG(ccc, ddd, eee, aaa, bbb, X[11], 14);
        GGG(bbb, ccc, ddd, eee, aaa, X[15],  6);
        GGG(aaa, bbb, ccc, ddd, eee, X[ 0], 14);
        GGG(eee, aaa, bbb, ccc, ddd, X[ 5],  6);
        GGG(ddd, eee, aaa, bbb, ccc, X[12],  9);
        GGG(ccc, ddd, eee, aaa, bbb, X[ 2], 12);
        GGG(bbb, ccc, ddd, eee, aaa, X[13],  9);
        GGG(aaa, bbb, ccc, ddd, eee, X[ 9], 12);
        GGG(eee, aaa, bbb, ccc, ddd, X[ 7],  5);
        GGG(ddd, eee, aaa, bbb, ccc, X[10], 15);
        GGG(ccc, ddd, eee, aaa, bbb, X[14],  8);

        /* parallel round 5 */
        FFF(bbb, ccc, ddd, eee, aaa, X[12] ,  8);
        FFF(aaa, bbb, ccc, ddd, eee, X[15] ,  5);
        FFF(eee, aaa, bbb, ccc, ddd, X[10] , 12);
        FFF(ddd, eee, aaa, bbb, ccc, X[ 4] ,  9);
        FFF(ccc, ddd, eee, aaa, bbb, X[ 1] , 12);
        FFF(bbb, ccc, ddd, eee, aaa, X[ 5] ,  5);
        FFF(aaa, bbb, ccc, ddd, eee, X[ 8] , 14);
        FFF(eee, aaa, bbb, ccc, ddd, X[ 7] ,  6);
        FFF(ddd, eee, aaa, bbb, ccc, X[ 6] ,  8);
        FFF(ccc, ddd, eee, aaa, bbb, X[ 2] , 13);
        FFF(bbb, ccc, ddd, eee, aaa, X[13] ,  6);
        FFF(aaa, bbb, ccc, ddd, eee, X[14] ,  5);
        FFF(eee, aaa, bbb, ccc, ddd, X[ 0] , 15);
        FFF(ddd, eee, aaa, bbb, ccc, X[ 3] , 13);
        FFF(ccc, ddd, eee, aaa, bbb, X[ 9] , 11);
        FFF(bbb, ccc, ddd, eee, aaa, X[11] , 11);

        /* combine results */
        ddd += cc + md->state[1];               /* final result for md->state[0] */
        md->state[1] = md->state[2] + dd + eee;
        md->state[2] = md->state[3] + ee + aaa;
        md->state[3] = md->state[4] + aa + bbb;
        md->state[4] = md->state[0] + bb + ccc;
        md->state[0] = ddd;

        return 0;
    }

    /**
     Initialize the hash state
    @param md   The hash state you wish to initialize
    @return 0 if successful
    */
    int rmd160_vinit(struct rmd160_vstate * md)
    {
        md->state[0] = 0x67452301UL;
        md->state[1] = 0xefcdab89UL;
        md->state[2] = 0x98badcfeUL;
        md->state[3] = 0x10325476UL;
        md->state[4] = 0xc3d2e1f0UL;
        md->curlen   = 0;
        md->length   = 0;
        return 0;
    }

    /**
     Process a block of memory though the hash
    @param md     The hash state
    @param in     The data to hash
    @param inlen  The length of the data (octets)
    @return 0 if successful
    */
    int rmd160_vprocess (struct rmd160_vstate * md, const unsigned char *in, unsigned long inlen)
    {
        unsigned long n;
        int           err;
        if (md->curlen > sizeof(md->buf)) {
            return -1;
        }
        while (inlen > 0) {
            if (md->curlen == 0 && inlen >= 64) {
                if ((err = rmd160_vcompress (md, (unsigned char *)in)) != 0) {
                    return err;
                }
                md->length += 64 * 8;
                in             += 64;
                inlen          -= 64;
            } else {
                n = MIN(inlen, (64 - md->curlen));
                memcpy(md->buf + md->curlen, in, (size_t)n);
                md->curlen += n;
                in             += n;
                inlen          -= n;
                if (md->curlen == 64) {
                    if ((err = rmd160_vcompress (md, md->buf)) != 0) {
                        return err;
                    }
                    md->length += 8*64;
                    md->curlen = 0;
                }
            }
        }
        return 0;
    }

    /**
     Terminate the hash to get the digest
    @param md  The hash state
    @param out [out] The destination of the hash (20 bytes)
    @return 0 if successful
    */
    int rmd160_vdone(struct rmd160_vstate * md, unsigned char *out)
    {
        int i;
        if (md->curlen >= sizeof(md->buf)) {
            return -1;
        }
        /* increase the length of the message */
        md->length += md->curlen * 8;

        /* append the '1' bit */
        md->buf[md->curlen++] = (unsigned char)0x80;

        /* if the length is currently above 56 bytes we append zeros
        * then compress.  Then we can fall back to padding zeros and length
        * encoding like normal.
        */
        if (md->curlen > 56) {
            while (md->curlen < 64) {
                md->buf[md->curlen++] = (unsigned char)0;
            }
            rmd160_vcompress(md, md->buf);
            md->curlen = 0;
        }
        /* pad upto 56 bytes of zeroes */
        while (md->curlen < 56) {
            md->buf[md->curlen++] = (unsigned char)0;
        }
        /* store length */
        STORE64L(md->length, md->buf+56);
        rmd160_vcompress(md, md->buf);
        /* copy output */
        for (i = 0; i < 5; i++) {
            STORE32L(md->state[i], out+(4*i));
        }
        return 0;
    }

    void calc_rmd160(char deprecated[41],uint8_t buf[20],uint8_t *msg,int32_t len)
    {
        struct rmd160_vstate md;
        TestOldHashRemoval::rmd160_vinit(&md);
        TestOldHashRemoval::rmd160_vprocess(&md, msg, len);
        TestOldHashRemoval::rmd160_vdone(&md, buf);
    }

    #undef F
    #undef G
    #undef H
    #undef I
    #undef J
    #undef ROLc
    #undef FF
    #undef GG
    #undef HH
    #undef II
    #undef JJ
    #undef FFF
    #undef GGG
    #undef HHH
    #undef III
    #undef JJJ

    void calc_rmd160_sha256_new(uint8_t rmd160[20],uint8_t *data,int32_t datalen) {
        CHash160().Write((const unsigned char *)data, datalen).Finalize(rmd160); // SHA-256 + RIPEMD-160
    }

    TEST(TestOldHashRemoval, calc_rmd160)
    {
        /* test vectors from http://gobittest.appspot.com/Address */

        uint8_t sha256_hash_1[] = {
            0x60, 0x0f, 0xfe, 0x42, 0x2b, 0x4e, 0x00, 0x73, 0x1a, 0x59, 0x55, 0x7a, 0x5c, 0xca, 0x46, 0xcc, 0x18, 0x39, 0x44, 0x19, 0x10, 0x06, 0x32, 0x4a, 0x44, 0x7b, 0xdb, 0x2d, 0x98, 0xd4, 0xb4, 0x08 };
        uint8_t rmd160_hash_1[] = {
            0x1, 0x9, 0x66, 0x77, 0x60, 0x6, 0x95, 0x3d, 0x55, 0x67, 0x43, 0x9e, 0x5e, 0x39, 0xf8, 0x6a, 0xd, 0x27, 0x3b, 0xee };

        uint8_t rmd160[20];
        bits256 hash;

        memset(hash.bytes, 0, sizeof(hash));
        memset(rmd160, 0, sizeof(rmd160));

        memcpy(hash.bytes, sha256_hash_1, sizeof(hash));
        calc_rmd160(0, rmd160, hash.bytes, sizeof(hash));
        EXPECT_EQ(memcmp(rmd160, rmd160_hash_1, sizeof(rmd160)), 0);

        // echo "0450863AD64A87AE8A2FE83C1AF1A8403CB53F53E486D8511DAD8A04887E5B23522CD470243453A299FA9E77237716103ABC11A1DF38855ED6F2EE187E9C582BA6" | sed 's/../0x&,/g' | tr '[:upper:]' '[:lower:]'
        uint8_t public_ecsda[] = {
            0x04, 0x50, 0x86, 0x3a, 0xd6, 0x4a, 0x87, 0xae, 0x8a, 0x2f, 0xe8, 0x3c, 0x1a, 0xf1, 0xa8, 0x40, 0x3c, 0xb5, 0x3f, 0x53, 0xe4, 0x86, 0xd8, 0x51, 0x1d, 0xad, 0x8a, 0x04, 0x88, 0x7e, 0x5b, 0x23, 0x52, 0x2c, 0xd4, 0x70, 0x24, 0x34, 0x53, 0xa2, 0x99, 0xfa, 0x9e, 0x77, 0x23, 0x77, 0x16, 0x10, 0x3a, 0xbc, 0x11, 0xa1, 0xdf, 0x38, 0x85, 0x5e, 0xd6, 0xf2, 0xee, 0x18, 0x7e, 0x9c, 0x58, 0x2b, 0xa6 };
        
        memset(rmd160, 0, sizeof(rmd160));
        calc_rmd160_sha256(rmd160,public_ecsda,sizeof(public_ecsda));
        EXPECT_EQ(memcmp(rmd160, rmd160_hash_1, sizeof(rmd160)), 0);

        memset(rmd160, 0, sizeof(rmd160));
        calc_rmd160_sha256_new(rmd160,public_ecsda,sizeof(public_ecsda));
        EXPECT_EQ(memcmp(rmd160, rmd160_hash_1, sizeof(rmd160)), 0);

        for (size_t idx = 0; idx < 64; idx++) {
            uint8_t pubkey[33];
            uint8_t rmd160_l[20], rmd160_r[20];
            memset(pubkey, 0, sizeof(pubkey));
            GetRandBytes(pubkey, sizeof(pubkey));
            calc_rmd160_sha256(rmd160_l,pubkey,sizeof(pubkey));
            calc_rmd160_sha256_new(rmd160_r,pubkey,sizeof(pubkey));
            EXPECT_EQ(memcmp(rmd160_l, rmd160_r, 20), 0);
        }
    
    }

} // namespace TestOldHashRemoval
