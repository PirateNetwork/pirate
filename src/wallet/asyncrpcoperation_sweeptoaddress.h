#include "amount.h"
#include "asyncrpcoperation.h"
#include "univalue.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"

//Default fee used for sweep transactions
static const CAmount DEFAULT_SWEEP_FEE = 10000;
extern CAmount fSweepTxFee;
extern bool fSweepMapUsed;
extern boost::optional<libzcash::SaplingPaymentAddress> rpcSweepAddress;

class AsyncRPCOperation_sweeptoaddress : public AsyncRPCOperation
{
public:
    AsyncRPCOperation_sweeptoaddress(int targetHeight, bool fromRpc = false);
    virtual ~AsyncRPCOperation_sweeptoaddress();

    // We don't want to be copied or moved around
    AsyncRPCOperation_sweeptoaddress(AsyncRPCOperation_sweeptoaddress const&) = delete;            // Copy construct
    AsyncRPCOperation_sweeptoaddress(AsyncRPCOperation_sweeptoaddress&&) = delete;                 // Move construct
    AsyncRPCOperation_sweeptoaddress& operator=(AsyncRPCOperation_sweeptoaddress const&) = delete; // Copy assign
    AsyncRPCOperation_sweeptoaddress& operator=(AsyncRPCOperation_sweeptoaddress&&) = delete;      // Move assign

    virtual void main();

    virtual void cancel();

    virtual UniValue getStatus() const;

private:
    int targetHeight_;
    bool fromRPC_;

    bool main_impl();

    void setSweepResult(int numTxCreated, const CAmount& amountSwept, const std::vector<std::string>& sweepTxIds);

};
