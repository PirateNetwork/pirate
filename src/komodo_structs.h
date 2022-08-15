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
#pragma once
#include <memory>
#include <list>
#include <vector>
#include <cstdint>

#include "komodo_defs.h"
#include "komodo_extern_globals.h"

#include "uthash.h"
#include "utlist.h"

#define GENESIS_NBITS 0x1f00ffff
#define KOMODO_MINRATIFY ((height < 90000) ? 7 : 11)
#define KOMODO_NOTARIES_HARDCODED 180000 // DONT CHANGE Below this height notaries were hardcoded
#define KOMODO_MAXBLOCKS 250000 // DONT CHANGE

#define KOMODO_ASSETCHAIN_MAXLEN 65

#include "bits256.h"
#include <mutex>

//extern std::mutex komodo_mutex;  //todo remove

struct komodo_event
{
    struct komodo_event *related;
    uint16_t len;
    int32_t height;
    uint8_t type,reorged;
    char symbol[KOMODO_ASSETCHAIN_MAXLEN];
    uint8_t space[];
};

/***
 * @brief persist event to file stream
 * @param evt the event
 * @param fp the file
 * @returns the number of bytes written
 */
template<class T>
size_t write_event(T& evt, FILE *fp)
{
    std::stringstream ss;
    ss << evt;
    std::string buf = ss.str();
    return fwrite(buf.c_str(), buf.size(), 1, fp);
}

namespace komodo {

enum komodo_event_type
{
    EVENT_PUBKEYS,
    EVENT_NOTARIZED,
    EVENT_U,
    EVENT_KMDHEIGHT,
    EVENT_OPRETURN,
    EVENT_PRICEFEED,
    EVENT_REWIND
};

/***
 * Thrown by event constructors when it finds a problem with the input data
 */
class parse_error : public std::logic_error
{
public:
    parse_error(const std::string& in) : std::logic_error(in) {}
};

/***
 * base class for events
 */
class event
{
public:
    event(komodo_event_type t, int32_t height) : type(t), height(height) {}
    virtual ~event() = default;
    komodo_event_type type;
    int32_t height;
};
std::ostream& operator<<(std::ostream& os, const event& in);

struct event_rewind : public event
{
    event_rewind() : event(komodo_event_type::EVENT_REWIND, 0) {}
    event_rewind(int32_t ht) : event(EVENT_REWIND, ht) {}
    event_rewind(uint8_t* data, long &pos, long data_len, int32_t height);
};
std::ostream& operator<<(std::ostream& os, const event_rewind& in);

struct event_notarized : public event
{
    event_notarized() : event(komodo_event_type::EVENT_NOTARIZED, 0), notarizedheight(0), MoMdepth(0) {
        memset(this->dest, 0, sizeof(this->dest));
    }
    event_notarized(int32_t ht, const char* _dest) : event(komodo_event_type::EVENT_NOTARIZED, ht), notarizedheight(0), MoMdepth(0) {
        strncpy(this->dest, _dest, sizeof(this->dest)-1); this->dest[sizeof(this->dest)-1] = 0;
    }
    event_notarized(uint8_t* data, long &pos, long data_len, int32_t height, const char* _dest, bool includeMoM = false);
    event_notarized(FILE* fp, int32_t ht, const char* _dest, bool includeMoM = false);
    uint256 blockhash;
    uint256 desttxid;
    uint256 MoM; 
    int32_t notarizedheight;
    int32_t MoMdepth; 
    char dest[16];
};
std::ostream& operator<<(std::ostream& os, const event_notarized& in);

struct event_pubkeys : public event
{
    /***
     * Default ctor
     */
    event_pubkeys() : event(EVENT_PUBKEYS, 0), num(0)
    {
        memset(pubkeys, 0, 64 * 33);
    }
    event_pubkeys(int32_t ht) : event(EVENT_PUBKEYS, ht), num(0) 
    {
        memset(pubkeys, 0, 64 * 33);
    }
    /***
     * ctor from data stream
     * @param data the data stream
     * @param pos the starting position (will advance)
     * @param data_len full length of data
     */
    event_pubkeys(uint8_t* data, long &pos, long data_len, int32_t height);
    event_pubkeys(FILE* fp, int32_t height);
    uint8_t num = 0; 
    uint8_t pubkeys[64][33]; 
};
std::ostream& operator<<(std::ostream& os, const event_pubkeys& in);

struct event_u : public event
{
    event_u() : event(EVENT_U, 0) 
    {
        memset(mask, 0, 8);
        memset(hash, 0, 32);
    }
    event_u(int32_t ht) : event(EVENT_U, ht)
    {
        memset(mask, 0, 8);
        memset(hash, 0, 32);
    }
    event_u(uint8_t *data, long &pos, long data_len, int32_t height);
    event_u(FILE* fp, int32_t height);
    uint8_t n = 0;
    uint8_t nid = 0;
    uint8_t mask[8];
    uint8_t hash[32];
};
std::ostream& operator<<(std::ostream& os, const event_u& in);

struct event_kmdheight : public event
{
    event_kmdheight() : event(EVENT_KMDHEIGHT, 0) {}
    event_kmdheight(int32_t ht) : event(EVENT_KMDHEIGHT, ht) {}
    event_kmdheight(uint8_t *data, long &pos, long data_len, int32_t height, bool includeTimestamp = false);
    event_kmdheight(FILE* fp, int32_t height, bool includeTimestamp = false);
    int32_t kheight = 0;
    uint32_t timestamp = 0;
};
std::ostream& operator<<(std::ostream& os, const event_kmdheight& in);

struct event_opreturn : public event 
{ 
    event_opreturn() : event(EVENT_OPRETURN, 0) 
    {
        txid.SetNull();
    }
    event_opreturn(int32_t ht) : event(EVENT_OPRETURN, ht)
    {
        txid.SetNull();
    }
    event_opreturn(uint8_t *data, long &pos, long data_len, int32_t height);
    event_opreturn(FILE* fp, int32_t height);
    uint256 txid; 
    uint16_t vout = 0;
    uint64_t value = 0; 
    std::vector<uint8_t> opret;
};
std::ostream& operator<<(std::ostream& os, const event_opreturn& in);

struct event_pricefeed : public event
{
    event_pricefeed() : event(EVENT_PRICEFEED, 0), num(0) 
    {
        memset(prices, 0, 35);
    }
    event_pricefeed(int32_t ht) : event(EVENT_PRICEFEED, ht)
    {
        memset(prices, 0, 35);
    }
    event_pricefeed(uint8_t *data, long &pos, long data_len, int32_t height);
    event_pricefeed(FILE* fp, int32_t height); 
    uint8_t num = 0; 
    uint32_t prices[35]; 
};
std::ostream& operator<<(std::ostream& os, const event_pricefeed& in);

} // namespace komodo

struct knotary_entry { UT_hash_handle hh; uint8_t pubkey[33],notaryid; };
struct knotaries_entry 
{ 
    int32_t height;
    int32_t numnotaries; // The number of notaries stored in Notaries
    knotary_entry *Notaries; // A hashtable of notary ID/public key
};
struct notarized_checkpoint
{
    uint256 notarized_hash;
    uint256 notarized_desttxid;
    uint256 MoM;
    uint256 MoMoM;
    int32_t nHeight = 0;
    int32_t notarized_height = 0;
    int32_t MoMdepth = 0;
    int32_t MoMoMdepth = 0;
    int32_t MoMoMoffset = 0;
    int32_t kmdstarti = 0;
    int32_t kmdendi = 0;
    friend bool operator==(const notarized_checkpoint& lhs, const notarized_checkpoint& rhs);
};

bool operator==(const notarized_checkpoint& lhs, const notarized_checkpoint& rhs);

struct komodo_ccdataMoM
{
    uint256 MoM;
    int32_t MoMdepth,notarized_height,height,txi;
};

struct komodo_ccdata_entry { uint256 MoM; int32_t notarized_height,kmdheight,txi; char symbol[65]; };
struct komodo_ccdatapair { int32_t notarized_height,MoMoMoffset; };

struct komodo_ccdataMoMoM
{
    uint256 MoMoM;
    int32_t kmdstarti,kmdendi,MoMoMoffset,MoMoMdepth,numpairs,len;
    struct komodo_ccdatapair *pairs;
};

struct komodo_ccdata
{
    struct komodo_ccdata *next,*prev;
    struct komodo_ccdataMoM MoMdata;
    uint32_t CCid,len;
    char symbol[65];
};

class komodo_state
{
public:
    std::string symbol;
    int32_t SAVEDHEIGHT;
    int32_t CURRENT_HEIGHT;
    uint32_t SAVEDTIMESTAMP;
    uint64_t deposited;
    uint64_t issued;
    uint64_t withdrawn;
    uint64_t approved;
    uint64_t redeemed;
    uint64_t shorted;
    std::list<std::shared_ptr<komodo::event>> events;
    uint32_t RTbufs[64][3]; uint64_t RTmask;
    template<class T>
    bool add_event(const std::string& symbol, const uint32_t height, T& in)
    {
        if (!chainName.isKMD())
        {
            std::shared_ptr<T> ptr = std::make_shared<T>( in );
            std::lock_guard<std::mutex> lock(komodo_mutex);
            events.push_back( ptr );
            return true;
        }
        return false;
    }

protected:
    /***
     * @brief clear the checkpoints collection
     * @note should only be used by tests
     */
    void clear_checkpoints();
    std::vector<notarized_checkpoint> NPOINTS; // collection of notarizations
    mutable size_t NPOINTS_last_index = 0; // caches checkpoint linear search position
    notarized_checkpoint last;

public:
    const uint256 &LastNotarizedHash() const;
    void SetLastNotarizedHash(const uint256 &in);
    const uint256 &LastNotarizedDestTxId() const;
    void SetLastNotarizedDestTxId(const uint256 &in);
    const uint256 &LastNotarizedMoM() const;
    void SetLastNotarizedMoM(const uint256 &in);
    const int32_t &LastNotarizedHeight() const;
    void SetLastNotarizedHeight(const int32_t in);
    const int32_t &LastNotarizedMoMDepth() const;
    void SetLastNotarizedMoMDepth(const int32_t in);

    /*****
     * @brief add a checkpoint to the collection and update member values
     * @param in the new values
     */
    void AddCheckpoint(const notarized_checkpoint &in);

    uint64_t NumCheckpoints() const;

    /****
     * Get the notarization data below a particular height
     * @param[in] nHeight the height desired
     * @param[out] notarized_hashp the hash of the notarized block
     * @param[out] notarized_desttxidp the desttxid
     * @returns the notarized height
     */
    int32_t NotarizedData(int32_t nHeight,uint256 *notarized_hashp,uint256 *notarized_desttxidp) const;

    /******
     * @brief Get the last notarization information
     * @param[out] prevMoMheightp the MoM height
     * @param[out] hashp the notarized hash
     * @param[out] txidp the DESTTXID
     * @returns the notarized height
     */
    int32_t NotarizedHeight(int32_t *prevMoMheightp,uint256 *hashp,uint256 *txidp);

    /****
     * Search for the last (chronological) MoM notarized height
     * @returns the last notarized height that has a MoM
     */
    int32_t PrevMoMHeight() const;

    /******
     * @brief Search the notarized checkpoints for a particular height
     * @note Finding a mach does include other criteria other than height
     *      such that the checkpoint includes the desired height
     * @param height the notarized_height desired
     * @returns the checkpoint or nullptr
     */
    const notarized_checkpoint *CheckpointAtHeight(int32_t height) const;
};
