#include <boost/version.hpp>
#include <curl/curl.h>
#include <iostream>
#include <string>
#include <cstring>
#include <memory>

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "komodo_cJSON.h"
#include "hex.h"
#include <zmq.h>

char *CClib_name(); // cclib.cpp (no interface)


int main(int argc, char* argv[])
{
    std::string sPackageString = "v0.01a";
#ifdef PACKAGE_STRING
    sPackageString = sPackageString + " (" + std::string(PACKAGE_STRING) + ")";
#endif
    std::cerr << "Test Linkage : Runner by Decker " << sPackageString << std::endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    if (info) {
        printf("libcurl version: %s\n", info->version);
        printf("Version number: %x (%d.%d.%d)\n", info->version_num,
               (info->version_num >> 16) & 0xff, (info->version_num >> 8) & 0xff, info->version_num & 0xff);
    }

    std::cout << "Boost version: "
              << BOOST_VERSION / 100000 << "."     // Major version
              << BOOST_VERSION / 100 % 1000 << "." // Minor version
              << BOOST_VERSION % 100               // Patch version
              << std::endl;

    curl_global_cleanup();

    // cjson test
    std::cout << "cJSON version: " << cJSON_Version() << std::endl;

    // decode_hex test from bitcoin_common
    const char hexString[] = "4465636B6572";
    size_t len = std::strlen(hexString);
    size_t byteLen = len / 2 + 1;
    std::unique_ptr<char[]> byteArray(new char[byteLen]);
    decode_hex((uint8_t *)byteArray.get(), len / 2, hexString);
    std::cerr << "Decoded hex: '" << byteArray.get() << "'" << std::endl;

    // libzmq test
    int zmq_major, zmq_minor, zmq_patch;
    zmq_version(&zmq_major, &zmq_minor, &zmq_patch);
    std::cout << "ZeroMQ version: " << zmq_major << "." << zmq_minor << "." << zmq_patch << std::endl;

    // std::cout << "CClib name: " << CClib_name() << std::endl;
    // nb! libcc can't be added without bitcoin_server and other dependencies

    /*

    Remaining libs to test:

    $(LIBBITCOIN_SERVER)
    $(LIBBITCOIN_ZMQ)
    $(LIBBITCOIN_PROTON)
    $(LIBLEVELDB)
    $(LIBMEMENV)
    $(EVENT_PTHREADS_LIBS)
    $(ZMQ_LIBS)
    $(PROTON_LIBS)
    $(LIBCC)
    -lcurl (explicitly added)

    libbitcoin_wallet.a
    */
}