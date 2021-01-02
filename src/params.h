#ifndef BITCOIN_PARAMS_H
#define BITCOIN_PARAMS_H


#include "init.h"
#include "util.h"

#include <stdlib.h>
#include <stdio.h>
#include <openssl/sha.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>

/* somewhat unix-specific */
#include <sys/time.h>
#include <unistd.h>

/* curl stuff */
#include <curl/curl.h>

#include <archive.h>
#include <archive_entry.h>

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#define TIME_IN_US 1
#define TIMETYPE curl_off_t
#define TIMEOPT CURLINFO_TOTAL_TIME_T
#define MINIMAL_PROGRESS_FUNCTIONALITY_INTERVAL     3000000

static const std::string PK_SHA256 = "8bc20a7f013b2b58970cddd2e7ea028975c88ae7ceb9259a5344a16bc2c0eef7";
static const std::string VK_SHA256 = "4bd498dae0aacfd8e98dc306338d017d9c08dd0918ead18172bd0aec2fc5df82";
static const std::string SAPLING_SPEND_SHA256 = "8e48ffd23abb3a5fd9c5589204f32d9c31285a04b78096ba40a79b75677efc13";
static const std::string SAPLING_OUTPUT_SHA256 = "2f0ebbcbb9bb0bcffe95a397e7eba89c29eb4dde6191c339db88570e3f3fb0e4";
static const std::string SPROUT_GROTH16_SHA256 = "b685d700c60328498fbde589c8c7c484c722b788b265b72af448a5bf0ee55b50";

static const std::string PK_URL = "https://download.z.cash/downloads/sprout-proving.key";
static const std::string VK_URL = "https://download.z.cash/downloads/sprout-verifying.key";
static const std::string SAPLING_SPEND_URL = "https://download.z.cash/downloads/sapling-spend.params";
static const std::string SAPLING_OUTPUT_URL = "https://download.z.cash/downloads/sapling-output.params";
static const std::string SPROUT_GROTH16_URL = "https://download.z.cash/downloads/sprout-groth16.params";





struct CurlProgress {
  TIMETYPE lastruntime; /* type depends on version, see above */
  CURL *curl;
};


struct ParamFile {
    std::string name;
    std::string URL;
    std::string hash;
    bool verified;
    bool complete = false;
    boost::filesystem::path path;
    FILE *file;
    int64_t dlnow;
    int64_t dltotal;
    int64_t dlretrytotal = 0;
    CURL *curl;
    CurlProgress prog;
};



extern std::map<std::string, ParamFile> mapParams;

extern bool checkParams();
extern void initalizeMapParamBootstrap();
extern void initalizeMapParam();
static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream);
extern bool downloadFiles(std::string title);
extern bool getBootstrap();
static bool extract(boost::filesystem::path filename);
static int copy_data(struct archive *ar, struct archive *aw);

static int verbose = 0;

#endif // BITCOIN_PARAMS_H
