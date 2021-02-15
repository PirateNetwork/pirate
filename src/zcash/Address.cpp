#include "Address.hpp"
#include "NoteEncryption.hpp"
#include "hash.h"
#include "prf.h"
#include "streams.h"

#include <librustzcash.h>


const uint32_t SAPLING_BRANCH_ID = 0x76b809bb;

namespace libzcash {

  std::pair<std::string, PaymentAddress> AddressInfoFromSpendingKey::operator()(const SproutSpendingKey &sk) const {
      return std::make_pair("z-sprout", sk.address());
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromSpendingKey::operator()(const SaplingExtendedSpendingKey &sk) const {
      return std::make_pair("z-sapling", sk.DefaultAddress());
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromSpendingKey::operator()(const InvalidEncoding&) const {
      throw std::invalid_argument("Cannot derive default address from invalid spending key");
  }

  std::pair<std::string, PaymentAddress> AddressInfoFromViewingKey::operator()(const SproutViewingKey &sk) const {
      return std::make_pair("z-sprout", sk.address());
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromViewingKey::operator()(const SaplingExtendedFullViewingKey &sk) const {
      return std::make_pair("z-sapling", sk.DefaultAddress());
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromViewingKey::operator()(const InvalidEncoding&) const {
      throw std::invalid_argument("Cannot derive default address from invalid viewing key");
  }

}

class IsValidAddressForNetwork : public boost::static_visitor<bool> {
    private:
        uint32_t branchId;
    public:
        IsValidAddressForNetwork(uint32_t consensusBranchId) : branchId(consensusBranchId) {}

        bool operator()(const libzcash::SproutPaymentAddress &addr) const {
            return true;
        }

        bool operator()(const libzcash::InvalidEncoding &addr) const {
            return false;
        }

        bool operator()(const libzcash::SaplingPaymentAddress &addr) const {
            if (SAPLING_BRANCH_ID == branchId)
                return true;
            else
                return false;
        }
};

bool IsValidPaymentAddress(const libzcash::PaymentAddress& zaddr, uint32_t consensusBranchId) {
    return boost::apply_visitor(IsValidAddressForNetwork(consensusBranchId), zaddr);
}

bool IsValidViewingKey(const libzcash::ViewingKey& vk) {
    return vk.which() != 0;
}

bool IsValidSpendingKey(const libzcash::SpendingKey& zkey) {
    return zkey.which() != 0;
}
