#ifndef ZC_ADDRESS_H_
#define ZC_ADDRESS_H_

#include "zcash/address/pirate_orchard.hpp"
#include "zcash/address/sapling.hpp"
#include "zcash/address/sprout.hpp"
#include "zcash/address/zip32.h"

#include <variant>
#include <boost/variant/static_visitor.hpp>

namespace libzcash {

class InvalidEncoding {
public:
    friend bool operator==(const InvalidEncoding &a, const InvalidEncoding &b) { return true; }
    friend bool operator<(const InvalidEncoding &a, const InvalidEncoding &b) { return true; }
};

typedef std::variant<InvalidEncoding, SproutPaymentAddress, SaplingPaymentAddress, OrchardPaymentAddressPirate> PaymentAddress;
typedef std::variant<InvalidEncoding, SproutViewingKey, SaplingExtendedFullViewingKey, OrchardExtendedFullViewingKeyPirate> ViewingKey;
typedef std::variant<InvalidEncoding, SaplingDiversifiedExtendedFullViewingKey> DiversifiedViewingKey;
typedef std::variant<InvalidEncoding, SproutSpendingKey, SaplingExtendedSpendingKey, OrchardExtendedSpendingKeyPirate> SpendingKey;
typedef std::variant<InvalidEncoding, SaplingDiversifiedExtendedSpendingKey> DiversifiedSpendingKey;

class AddressInfoFromSpendingKey : public boost::static_visitor<std::pair<std::string, PaymentAddress>> {
public:
    std::pair<std::string, PaymentAddress> operator()(const SproutSpendingKey&) const;
    std::pair<std::string, PaymentAddress> operator()(const struct SaplingExtendedSpendingKey&) const;
    std::pair<std::string, PaymentAddress> operator()(const struct OrchardExtendedSpendingKeyPirate&) const;
    std::pair<std::string, PaymentAddress> operator()(const InvalidEncoding&) const;
};

class AddressInfoFromDiversifiedSpendingKey : public boost::static_visitor<std::pair<std::string, PaymentAddress>> {
public:
    std::pair<std::string, PaymentAddress> operator()(const struct SaplingDiversifiedExtendedSpendingKey&) const;
    std::pair<std::string, PaymentAddress> operator()(const InvalidEncoding&) const;
};

class AddressInfoFromViewingKey : public boost::static_visitor<std::pair<std::string, PaymentAddress>> {
public:
    std::pair<std::string, PaymentAddress> operator()(const SproutViewingKey&) const;
    std::pair<std::string, PaymentAddress> operator()(const struct SaplingExtendedFullViewingKey&) const;
    std::pair<std::string, PaymentAddress> operator()(const struct OrchardExtendedFullViewingKeyPirate&) const;
    std::pair<std::string, PaymentAddress> operator()(const InvalidEncoding&) const;
};

class AddressInfoFromDiversifiedViewingKey : public boost::static_visitor<std::pair<std::string, PaymentAddress>> {
public:
    std::pair<std::string, PaymentAddress> operator()(const struct SaplingDiversifiedExtendedFullViewingKey&) const;
    std::pair<std::string, PaymentAddress> operator()(const InvalidEncoding&) const;
};

}

/** Check whether a PaymentAddress is not an InvalidEncoding. */
extern const uint32_t SAPLING_BRANCH_ID;
bool IsValidPaymentAddress(const libzcash::PaymentAddress& zaddr);

/** Check whether a ViewingKey is not an InvalidEncoding. */
bool IsValidViewingKey(const libzcash::ViewingKey& vk);

/** Check whether a Diversified ViewingKey is not an InvalidEncoding. */
bool IsValidDiversifiedViewingKey(const libzcash::DiversifiedViewingKey& vk);

/** Check whether a SpendingKey is not an InvalidEncoding. */
bool IsValidSpendingKey(const libzcash::SpendingKey& zkey);

/** Check whether a Diversified SpendingKey is not an InvalidEncoding. */
bool IsValidDiversifiedSpendingKey(const libzcash::DiversifiedSpendingKey& zkey);

#endif // ZC_ADDRESS_H_
