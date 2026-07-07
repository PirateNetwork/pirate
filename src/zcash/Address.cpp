#include "Address.hpp"
#include "NoteEncryption.hpp"
#include "hash.h"
#include "prf.h"
#include "streams.h"

#include <librustzcash.h>


const uint32_t SAPLING_BRANCH_ID = 0x76b809bb;

namespace libzcash {

  //Spending Keys
  std::pair<std::string, PaymentAddress> AddressInfoFromSpendingKey::operator()(const SproutSpendingKey &sk) const {
      return std::make_pair("z-sprout", sk.address());
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromSpendingKey::operator()(const SaplingExtendedSpendingKey &sk) const {
      return std::make_pair("z-sapling", sk.DefaultAddress());
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromSpendingKey::operator()(const IronwoodExtendedSpendingKeyPirate &extsk) const {
      libzcash::IronwoodPaymentAddress address;
      if (!extsk.sk.DeriveDefaultAddress(&address)) {
          throw std::invalid_argument("Cannot derive default address from invalid spending key");
      }
      return std::make_pair("z-ironwood", address);
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromSpendingKey::operator()(const InvalidEncoding&) const {
      throw std::invalid_argument("Cannot derive default address from invalid spending key");
  }

  //Diversifid SpendingKeys
  std::pair<std::string, PaymentAddress> AddressInfoFromDiversifiedSpendingKey::operator()(const SaplingDiversifiedExtendedSpendingKey &dsk) const {
      SaplingPaymentAddress addr;
      libzcash::SaplingIncomingViewingKey ivk;
      dsk.extsk.ToXFVK().fvk.DeriveIVK(&ivk);
      if (!ivk.DeriveAddress(&addr, dsk.d)) {
          throw std::invalid_argument("Cannot derive address from invalid diversifier");
      }
      return std::make_pair("z-sapling", addr);
  }
    std::pair<std::string, PaymentAddress> AddressInfoFromDiversifiedSpendingKey::operator()(const IronwoodDiversifiedExtendedSpendingKeyPirate &dsk) const {
        libzcash::IronwoodFullViewingKey fvk;
        if (!dsk.extsk.sk.DeriveFVK(&fvk)) {
            throw std::invalid_argument("Cannot derive default address from invalid diversified spending key");
        }
        libzcash::IronwoodPaymentAddress address;
        if (!fvk.DeriveAddress(&address, dsk.d)) {
            throw std::invalid_argument("Cannot derive default address from invalid diversified spending key");
        }
        return std::make_pair("z-ironwood", address);
    }
  std::pair<std::string, PaymentAddress> AddressInfoFromDiversifiedSpendingKey::operator()(const InvalidEncoding&) const {
      throw std::invalid_argument("Cannot derive default address from invalid spending key");
  }

  //Viewing Keys
  std::pair<std::string, PaymentAddress> AddressInfoFromViewingKey::operator()(const SproutViewingKey &sk) const {
      return std::make_pair("z-sprout", sk.address());
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromViewingKey::operator()(const SaplingExtendedFullViewingKey &sk) const {
      return std::make_pair("z-sapling", sk.DefaultAddress());
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromViewingKey::operator()(const IronwoodExtendedFullViewingKeyPirate &sk) const {
      libzcash::IronwoodPaymentAddress address;
      if (!sk.fvk.DeriveDefaultAddress(&address)) {
          throw std::invalid_argument("Cannot derive default address from invalid viewing key");
      }
      return std::make_pair("z-ironwood", address);
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromViewingKey::operator()(const InvalidEncoding&) const {
      throw std::invalid_argument("Cannot derive default address from invalid viewing key");
  }

  //Diversified Viewing Keys
  std::pair<std::string, PaymentAddress> AddressInfoFromDiversifiedViewingKey::operator()(const SaplingDiversifiedExtendedFullViewingKey &dvk) const {
      SaplingPaymentAddress addr;
      libzcash::SaplingIncomingViewingKey ivk;
      dvk.extfvk.fvk.DeriveIVK(&ivk);
      if (!ivk.DeriveAddress(&addr, dvk.d)) {
          throw std::invalid_argument("Cannot derive address from invalid diversifier");
      }
      return std::make_pair("z-sapling", addr);
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromDiversifiedViewingKey::operator()(const IronwoodDiversifiedExtendedFullViewingKeyPirate &dvk) const {
      libzcash::IronwoodPaymentAddress addr;
      if (!dvk.extfvk.fvk.DeriveAddress(&addr, dvk.d)) {
          throw std::invalid_argument("Cannot derive default address from invalid diversified full viewing key");
      }
      return std::make_pair("z-ironwood", addr);
  }
  std::pair<std::string, PaymentAddress> AddressInfoFromDiversifiedViewingKey::operator()(const InvalidEncoding&) const {
      throw std::invalid_argument("Cannot derive address from invalid viewing key");
  }

}

class IsValidAddressForNetwork : public boost::static_visitor<bool> {
    public:

        bool operator()(const libzcash::SproutPaymentAddress &addr) const {
            return false;
        }

        bool operator()(const libzcash::SaplingPaymentAddress &addr) const {
            return true;
        }

        bool operator()(const libzcash::IronwoodPaymentAddress &addr) const {
            return true;
        }

        bool operator()(const libzcash::InvalidEncoding &addr) const {
            return false;
        }
};

bool IsValidPaymentAddress(const libzcash::PaymentAddress& zaddr) {
    return std::visit(IsValidAddressForNetwork(), zaddr);
}

bool IsValidViewingKey(const libzcash::ViewingKey& vk) {
    return vk.index() != 0;
}

bool IsValidDiversifiedViewingKey(const libzcash::DiversifiedViewingKey& vk) {
    return vk.index() != 0;
}

bool IsValidSpendingKey(const libzcash::SpendingKey& zkey) {
    return zkey.index() != 0;
}

bool IsValidDiversifiedSpendingKey(const libzcash::DiversifiedSpendingKey& zkey) {
    return zkey.index() != 0;
}
