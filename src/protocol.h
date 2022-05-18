// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#ifndef __cplusplus
#error This header can only be compiled as C++.
#endif

#ifndef BITCOIN_PROTOCOL_H
#define BITCOIN_PROTOCOL_H

#include "netbase.h"
#include "serialize.h"
#include "streams.h"
#include "uint256.h"
#include "version.h"

#include <stdint.h>
#include <string>

#define MESSAGE_START_SIZE 4

/** Message header.
 * (4) message start.
 * (12) command.
 * (4) size.
 * (4) checksum.
 */
class CMessageHeader
{
public:
    typedef unsigned char MessageStartChars[MESSAGE_START_SIZE];

    CMessageHeader(const MessageStartChars& pchMessageStartIn);
    CMessageHeader(const MessageStartChars& pchMessageStartIn, const char* pszCommand, unsigned int nMessageSizeIn);

    std::string GetCommand() const;
    bool IsValid(const MessageStartChars& messageStart) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(FLATDATA(pchMessageStart));
        READWRITE(FLATDATA(pchCommand));
        READWRITE(nMessageSize);
        READWRITE(nChecksum);
    }

    // TODO: make private (improves encapsulation)
public:
    enum {
        COMMAND_SIZE = 12,
        MESSAGE_SIZE_SIZE = sizeof(int),
        CHECKSUM_SIZE = sizeof(int),

        MESSAGE_SIZE_OFFSET = MESSAGE_START_SIZE + COMMAND_SIZE,
        CHECKSUM_OFFSET = MESSAGE_SIZE_OFFSET + MESSAGE_SIZE_SIZE,
        HEADER_SIZE = MESSAGE_START_SIZE + COMMAND_SIZE + MESSAGE_SIZE_SIZE + CHECKSUM_SIZE
    };
    char pchMessageStart[MESSAGE_START_SIZE];
    char pchCommand[COMMAND_SIZE];
    unsigned int nMessageSize;
    unsigned int nChecksum;
};

/**
 * Bitcoin protocol message types. When adding new message types, don't forget
 * to update allNetMessageTypes in protocol.cpp.
 */
namespace NetMsgType {

/**
 * The version message provides information about the transmitting node to the
 * receiving node at the beginning of a connection.
 */
extern const char* VERSION;
/**
 * The verack message acknowledges a previously-received version message,
 * informing the connecting node that it can begin to send other messages.
 */
extern const char* VERACK;
/**
 * The addr (IP address) message relays connection information for peers on the
 * network.
 */
extern const char* ADDR;
/**
 * The addrv2 message relays connection information for peers on the network just
 * like the addr message, but is extended to allow gossiping of longer node
 * addresses (see BIP155).
 */
extern const char *ADDRV2;
/**
 * The sendaddrv2 message signals support for receiving ADDRV2 messages (BIP155).
 * It also implies that its sender can encode as ADDRV2 and would send ADDRV2
 * instead of ADDR to a peer that has signaled ADDRV2 support by sending SENDADDRV2.
 */
extern const char *SENDADDRV2;
/**
 * The inv message (inventory message) transmits one or more inventories of
 * objects known to the transmitting peer.
 */
extern const char* INV;
/**
 * The getdata message requests one or more data objects from another node.
 */
extern const char* GETDATA;
/**
 * The merkleblock message is a reply to a getdata message which requested a
 * block using the inventory type MSG_MERKLEBLOCK.
 * @since protocol version 70001 as described by BIP37.
 */
extern const char* MERKLEBLOCK;
/**
 * The getblocks message requests an inv message that provides block header
 * hashes starting from a particular point in the block chain.
 */
extern const char* GETBLOCKS;
/**
 * The getheaders message requests a headers message that provides block
 * headers starting from a particular point in the block chain.
 * @since protocol version 31800.
 */
extern const char* GETHEADERS;
/**
 * The tx message transmits a single transaction.
 */
extern const char* TX;
/**
 * The headers message sends one or more block headers to a node which
 * previously requested certain headers with a getheaders message.
 * @since protocol version 31800.
 */
extern const char* HEADERS;
/**
 * The block message transmits a single serialized block.
 */
extern const char* BLOCK;
/**
 * The getaddr message requests an addr message from the receiving node,
 * preferably one with lots of IP addresses of other receiving nodes.
 */
extern const char* GETADDR;
/**
 * The mempool message requests the TXIDs of transactions that the receiving
 * node has verified as valid but which have not yet appeared in a block.
 * @since protocol version 60002.
 */
extern const char* MEMPOOL;
/**
 * The ping message is sent periodically to help confirm that the receiving
 * peer is still connected.
 */
extern const char* PING;
/**
 * The pong message replies to a ping message, proving to the pinging node that
 * the ponging node is still alive.
 * @since protocol version 60001 as described by BIP31.
 */
extern const char* PONG;
/**
 * The notfound message is a reply to a getdata message which requested an
 * object the receiving node does not have available for relay.
 * @since protocol version 70001.
 */
extern const char* NOTFOUND;
/**
 * The filterload message tells the receiving peer to filter all relayed
 * transactions and requested merkle blocks through the provided filter.
 * @since protocol version 70001 as described by BIP37.
 *   Only available with service bit NODE_BLOOM since protocol version
 *   70011 as described by BIP111.
 */
extern const char* FILTERLOAD;
/**
 * The filteradd message tells the receiving peer to add a single element to a
 * previously-set bloom filter, such as a new public key.
 * @since protocol version 70001 as described by BIP37.
 *   Only available with service bit NODE_BLOOM since protocol version
 *   70011 as described by BIP111.
 */
extern const char* FILTERADD;
/**
 * The filterclear message tells the receiving peer to remove a previously-set
 * bloom filter.
 * @since protocol version 70001 as described by BIP37.
 *   Only available with service bit NODE_BLOOM since protocol version
 *   70011 as described by BIP111.
 */
extern const char* FILTERCLEAR;
/**
 * Indicates that a node prefers to receive new block announcements via a
 * "headers" message rather than an "inv".
 * @since protocol version 70012 as described by BIP130.
 */
extern const char* SENDHEADERS;
/**
 * The feefilter message tells the receiving peer not to inv us any txs
 * which do not meet the specified min fee rate.
 * @since protocol version 70013 as described by BIP133
 */
extern const char* FEEFILTER;
/**
 * Contains a 1-byte bool and 8-byte LE version number.
 * Indicates that a node is willing to provide blocks via "cmpctblock" messages.
 * May indicate that a node prefers to receive new block announcements via a
 * "cmpctblock" message rather than an "inv", depending on message contents.
 * @since protocol version 70014 as described by BIP 152
 */
extern const char* SENDCMPCT;
/**
 * Contains a CBlockHeaderAndShortTxIDs object - providing a header and
 * list of "short txids".
 * @since protocol version 70014 as described by BIP 152
 */
extern const char* CMPCTBLOCK;
/**
 * Contains a BlockTransactionsRequest
 * Peer should respond with "blocktxn" message.
 * @since protocol version 70014 as described by BIP 152
 */
extern const char* GETBLOCKTXN;
/**
 * Contains a BlockTransactions.
 * Sent in response to a "getblocktxn" message.
 * @since protocol version 70014 as described by BIP 152
 */
extern const char* BLOCKTXN;
/**
 * getcfilters requests compact filters for a range of blocks.
 * Only available with service bit NODE_COMPACT_FILTERS as described by
 * BIP 157 & 158.
 */
extern const char* GETCFILTERS;
/**
 * cfilter is a response to a getcfilters request containing a single compact
 * filter.
 */
extern const char* CFILTER;
/**
 * getcfheaders requests a compact filter header and the filter hashes for a
 * range of blocks, which can then be used to reconstruct the filter headers
 * for those blocks.
 * Only available with service bit NODE_COMPACT_FILTERS as described by
 * BIP 157 & 158.
 */
extern const char* GETCFHEADERS;
/**
 * cfheaders is a response to a getcfheaders request containing a filter header
 * and a vector of filter hashes for each subsequent block in the requested range.
 */
extern const char* CFHEADERS;
/**
 * getcfcheckpt requests evenly spaced compact filter headers, enabling
 * parallelized download and validation of the headers between them.
 * Only available with service bit NODE_COMPACT_FILTERS as described by
 * BIP 157 & 158.
 */
extern const char* GETCFCHECKPT;
/**
 * cfcheckpt is a response to a getcfcheckpt request containing a vector of
 * evenly spaced filter headers for blocks on the requested chain.
 */
extern const char* CFCHECKPT;
/**
 * Indicates that a node prefers to relay transactions via wtxid, rather than
 * txid.
 * @since protocol version 70016 as described by BIP 339.
 */
extern const char* WTXIDRELAY;

extern const char* EVENTS;
extern const char* GETNSPV;
extern const char* NSPV;
extern const char* ALERT;
extern const char* REJECT;
}; // namespace NetMsgType

/* Get a vector of all valid message types (see above) */
const std::vector<std::string>& getAllNetMessageTypes();

/** nServices flags */
enum ServiceFlags : uint64_t {
    // NODE_NETWORK means that the node is capable of serving the block chain. It is currently
    // set by all Bitcoin Core nodes, and is unset by SPV clients or other peers that just want
    // network services but don't provide them.
    NODE_NETWORK = (1 << 0),
    // NODE_BLOOM means the node is capable and willing to handle bloom-filtered connections.
    // Zcash nodes used to support this by default, without advertising this bit,
    // but no longer do as of protocol version 170004 (= NO_BLOOM_VERSION)
    NODE_BLOOM = (1 << 2),

    NODE_NSPV = (1 << 30),
    NODE_ADDRINDEX = (1 << 29),
    NODE_SPENTINDEX = (1 << 28),

    // Bits 24-31 are reserved for temporary experiments. Just pick a bit that
    // isn't getting used, or one not being used much, and notify the
    // bitcoin-development mailing list. Remember that service bits are just
    // unauthenticated advertisements, so your code must be robust against
    // collisions and other cases where nodes may be advertising a service they
    // do not actually support. Other service bits should be allocated via the
    // BIP process.
};

/** A CService with information about it as peer */
class CAddress : public CService
{
      static constexpr uint32_t TIME_INIT{100000000};

    /** Historically, CAddress disk serialization stored the CLIENT_VERSION, optionally OR'ed with
     *  the ADDRV2_FORMAT flag to indicate V2 serialization. The first field has since been
     *  disentangled from client versioning, and now instead:
     *  - The low bits (masked by DISK_VERSION_IGNORE_MASK) store the fixed value DISK_VERSION_INIT,
     *    (in case any code exists that treats it as a client version) but are ignored on
     *    deserialization.
     *  - The high bits (masked by ~DISK_VERSION_IGNORE_MASK) store actual serialization information.
     *    Only 0 or DISK_VERSION_ADDRV2 (equal to the historical ADDRV2_FORMAT) are valid now, and
     *    any other value triggers a deserialization failure. Other values can be added later if
     *    needed.
     *
     *  For disk deserialization, ADDRV2_FORMAT in the stream version signals that ADDRV2
     *  deserialization is permitted, but the actual format is determined by the high bits in the
     *  stored version field. For network serialization, the stream version having ADDRV2_FORMAT or
     *  not determines the actual format used (as it has no embedded version number).
     */
    static constexpr uint32_t DISK_VERSION_INIT{220000};
    static constexpr uint32_t DISK_VERSION_IGNORE_MASK{0b00000000000001111111111111111111};
    /** The version number written in disk serialized addresses to indicate V2 serializations.
     * It must be exactly 1<<29, as that is the value that historical versions used for this
     * (they used their internal ADDRV2_FORMAT flag here). */
    static constexpr uint32_t DISK_VERSION_ADDRV2{1 << 29};
    static_assert((DISK_VERSION_INIT & ~DISK_VERSION_IGNORE_MASK) == 0, "DISK_VERSION_INIT must be covered by DISK_VERSION_IGNORE_MASK");
    static_assert((DISK_VERSION_ADDRV2 & DISK_VERSION_IGNORE_MASK) == 0, "DISK_VERSION_ADDRV2 must not be covered by DISK_VERSION_IGNORE_MASK");

public:
    CAddress();
    explicit CAddress(CService ipIn, uint64_t nServicesIn = NODE_NETWORK);

    void Init();

    SERIALIZE_METHODS(CAddress, obj)
    {
        // CAddress has a distinct network serialization and a disk serialization, but it should never
        // be hashed (except through CHashWriter in addrdb.cpp, which sets SER_DISK), and it's
        // ambiguous what that would mean. Make sure no code relying on that is introduced:
        assert(!(s.GetType() & SER_GETHASH));
        bool use_v2;
        bool store_time;
        if (s.GetType() & SER_DISK) {
            // In the disk serialization format, the encoding (v1 or v2) is determined by a flag version
            // that's part of the serialization itself. ADDRV2_FORMAT in the stream version only determines
            // whether V2 is chosen/permitted at all.
            uint32_t stored_format_version = DISK_VERSION_INIT;
            if (s.GetVersion() & ADDRV2_FORMAT) stored_format_version |= DISK_VERSION_ADDRV2;
            READ_WRITE(stored_format_version);
            stored_format_version &= ~DISK_VERSION_IGNORE_MASK; // ignore low bits
            if (stored_format_version == 0) {
                use_v2 = false;
            } else if (stored_format_version == DISK_VERSION_ADDRV2 && (s.GetVersion() & ADDRV2_FORMAT)) {
                // Only support v2 deserialization if ADDRV2_FORMAT is set.
                use_v2 = true;
            } else {
                throw std::ios_base::failure("Unsupported CAddress disk format version");
            }
            store_time = true;
        } else {
            // In the network serialization format, the encoding (v1 or v2) is determined directly by
            // the value of ADDRV2_FORMAT in the stream version, as no explicitly encoded version
            // exists in the stream.
            assert(s.GetType() & SER_NETWORK);
            use_v2 = s.GetVersion() & ADDRV2_FORMAT;
            // The only time we serialize a CAddress object without nTime is in
            // the initial VERSION messages which contain two CAddress records.
            // At that point, the serialization version is INIT_PROTO_VERSION.
            // After the version handshake, serialization version is >=
            // MIN_PEER_PROTO_VERSION and all ADDR messages are serialized with
            // nTime.
            store_time = s.GetVersion() != INIT_PROTO_VERSION;
        }

        SER_READ(obj, obj.nTime = TIME_INIT);
        if (store_time) READWRITE(obj.nTime);
        // nServices is serialized as CompactSize in V2; as uint64_t in V1.
        if (use_v2) {
            uint64_t services_tmp;
            SER_WRITE(obj, services_tmp = obj.nServices);
            READ_WRITE(Using<CompactSizeFormatter<false>>(services_tmp));
            SER_READ(obj, obj.nServices = static_cast<ServiceFlags>(services_tmp));
        } else {
            READ_WRITE(Using<CustomUintFormatter<8>>(obj.nServices));
        }
        // Invoke V1/V2 serializer for CService parent object.
        OverrideStream<Stream> os(&s, s.GetType(), use_v2 ? ADDRV2_FORMAT : 0);
        SerReadWriteMany(os, ser_action, ReadWriteAsHelper<CService>(obj));
    }

    // TODO: make private (improves encapsulation)
public:
    uint64_t nServices;

    // disk and network only
    unsigned int nTime;
};

/** inv message data */
class CInv
{
public:
    CInv();
    CInv(int typeIn, const uint256& hashIn);
    CInv(const std::string& strType, const uint256& hashIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(type);
        READWRITE(hash);
    }

    friend bool operator<(const CInv& a, const CInv& b);

    bool IsKnownType() const;
    const char* GetCommand() const;
    std::string ToString() const;

    // TODO: make private (improves encapsulation)
public:
    int type;
    uint256 hash;
};

enum {
    MSG_TX = 1,
    MSG_BLOCK,
    // Nodes may always request a MSG_FILTERED_BLOCK in a getdata, however,
    // MSG_FILTERED_BLOCK should not appear in any invs except as a part of getdata.
    MSG_FILTERED_BLOCK,
};

#endif // BITCOIN_PROTOCOL_H
