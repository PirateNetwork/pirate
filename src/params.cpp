
#include "params.h"
#include "ui_interface.h"

std::map<std::string, ParamFile> mapParams;

void sha256_hash_string (unsigned char hash[SHA256_DIGEST_LENGTH], char outputBuffer[65])
{
    int i = 0;

    for(i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }

    outputBuffer[64] = 0;
}

int sha256_file(const char *path, char outputBuffer[65])
{
    errno = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        LogPrintf("Error %d \n", errno);
        return -534;
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    const int bufSize = 32768;
    unsigned char *buffer = (unsigned char *)malloc(bufSize);
    int bytesRead = 0;
    if(!buffer) return ENOMEM;
    while((bytesRead = fread(buffer, 1, bufSize, file)))
    {
        SHA256_Update(&sha256, buffer, bytesRead);
    }
    SHA256_Final(hash, &sha256);

    sha256_hash_string(hash, outputBuffer);
    fclose(file);
    free(buffer);
    return 0;
}


bool checkParams() {
    bool allVerified = true;
    for (std::map<std::string, ParamFile>::iterator it = mapParams.begin(); it != mapParams.end(); ++it) {
        std::string uiMessage = "Verifying " + it->second.name + "....\n";
        uiInterface.InitMessage(_(uiMessage.c_str()));
        const char *path = it->second.path.string().c_str();
        char calc_hash[65];
        sha256_file(path ,calc_hash);
        if (calc_hash == it->second.hash) {
            it->second.verified = true;
        } else {
            allVerified = false;
        }
    }
    return allVerified;
}


static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
  size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
  return written;
}



static int xferinfo(void *p,
                    curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t ultotal, curl_off_t ulnow)
{
  struct CurlProgress *myp = (struct CurlProgress *)p;
  CURL *curl = myp->curl;
  TIMETYPE curtime = 0;

  char *url = NULL;
  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);

  std::map<std::string, ParamFile>::iterator mi = mapParams.find(url);
  if (mi != mapParams.end()) {
      mi->second.dlnow = dlnow;
      mi->second.dltotal = dltotal;
  }

  return 0;
}

void initalizeMapParam() {

    ParamFile pkFile;
    pkFile.name = "sprout-proving.key";
    pkFile.URL = PK_URL;
    pkFile.hash = PK_SHA256;
    pkFile.verified = false;
    pkFile.path = ZC_GetParamsDir() / "sprout-proving.key";
    pkFile.dlnow = 0;
    pkFile.dltotal = 0;
    mapParams[pkFile.URL] = pkFile;

    ParamFile vkFile;
    vkFile.name = "sprout-verifying.key";
    vkFile.URL = VK_URL;
    vkFile.hash = VK_SHA256;
    vkFile.verified = false;
    vkFile.path = ZC_GetParamsDir() / "sprout-verifying.key";
    vkFile.dlnow = 0;
    vkFile.dltotal = 0;
    mapParams[vkFile.URL] = vkFile;

    ParamFile spendFile;
    spendFile.name = "sapling-spend.params";
    spendFile.URL = SAPLING_SPEND_URL;
    spendFile.hash = SAPLING_SPEND_SHA256;
    spendFile.verified = false;
    spendFile.path = ZC_GetParamsDir() / "sapling-spend.params";
    spendFile.dlnow = 0;
    spendFile.dltotal = 0;
    mapParams[spendFile.URL] = spendFile;

    ParamFile outputFile;
    outputFile.name = "sapling-output.params";
    outputFile.URL = SAPLING_OUTPUT_URL;
    outputFile.hash = SAPLING_OUTPUT_SHA256;
    outputFile.verified = false;
    outputFile.path = ZC_GetParamsDir() / "sapling-output.params";
    outputFile.dlnow = 0;
    outputFile.dltotal = 0;
    mapParams[outputFile.URL] = outputFile;

    ParamFile groth16File;
    groth16File.name = "sprout-groth16.params";
    groth16File.URL = SPROUT_GROTH16_URL;
    groth16File.hash = SPROUT_GROTH16_SHA256;
    groth16File.verified = false;
    groth16File.path = ZC_GetParamsDir() / "sprout-groth16.params";
    groth16File.dlnow = 0;
    groth16File.dltotal = 0;
    mapParams[groth16File.URL] = groth16File;

}

bool getParams()
{

    curl_global_init(CURL_GLOBAL_ALL);
    CURLM *multi_handle;
    multi_handle = curl_multi_init();
    int still_running = 0; /* keep number of running handles */

    if (!exists(ZC_GetParamsDir())) {
        create_directory(ZC_GetParamsDir());
    }

    for (std::map<std::string, ParamFile>::iterator it = mapParams.begin(); it != mapParams.end(); ++it) {

        if (!it->second.verified) {
            /* init the curl session */
            LogPrintf("Downloading %s\n",it->second.URL);
            it->second.curl = curl_easy_init();
            if(it->second.curl) {
                it->second.prog.lastruntime = 0;
                it->second.prog.curl = it->second.curl;
            }

            //open file for writing
            const char *path = it->second.path.string().c_str();
            it->second.file = fopen(path, "wb");
            if (!it->second.file) {
                return false;
            }

            curl_easy_setopt(it->second.curl, CURLOPT_URL, it->second.URL.c_str());
            curl_easy_setopt(it->second.curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(it->second.curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(it->second.curl, CURLOPT_VERBOSE, 0L);
            curl_easy_setopt(it->second.curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
            curl_easy_setopt(it->second.curl, CURLOPT_XFERINFODATA, &it->second.prog);
            curl_easy_setopt(it->second.curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(it->second.curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(it->second.curl, CURLOPT_WRITEDATA, it->second.file);
            curl_multi_add_handle(multi_handle, it->second.curl);
        }
    }


    curl_multi_perform(multi_handle, &still_running);

    while(still_running) {
      boost::this_thread::interruption_point();

      if (ShutdownRequested())
          break;

      std::string uiNotification;
      uiNotification = "Downloading Params...\n";
      for (std::map<std::string, ParamFile>::iterator it = mapParams.begin(); it != mapParams.end(); ++it) {
          if (!it->second.verified) {
              if (it->second.dltotal > 0) {
                  double pert = (it->second.dlnow / (double)it->second.dltotal) * 100;
                  // double rounded = (int)(pert * 100 + .5);
                  // double roundedPert = (double)rounded / 100;
                  std::string addedNotification = it->second.name + "...... " + std::to_string(pert).substr(0,6) + "%\n";
                  uiNotification.append(addedNotification);
              } else {
                std::string addedNotification = it->second.name + "...... " + std::to_string(0.00) + "%\n";
                uiNotification.append(addedNotification);
              }
          }
      }
      uiInterface.InitMessage(_(uiNotification.c_str()));

      struct timeval timeout;
      int rc; /* select() return code */
      CURLMcode mc; /* curl_multi_fdset() return code */

      fd_set fdread;
      fd_set fdwrite;
      fd_set fdexcep;
      int maxfd = -1;

      long curl_timeo = -1;

      FD_ZERO(&fdread);
      FD_ZERO(&fdwrite);
      FD_ZERO(&fdexcep);

      /* set a suitable timeout to play around with */
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;

      curl_multi_timeout(multi_handle, &curl_timeo);
      if(curl_timeo >= 0) {
        timeout.tv_sec = curl_timeo / 1000;
        if(timeout.tv_sec > 1)
          timeout.tv_sec = 1;
        else
          timeout.tv_usec = (curl_timeo % 1000) * 1000;
      }

      /* get file descriptors from the transfers */
      mc = curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

      if(mc != CURLM_OK) {
        fprintf(stderr, "curl_multi_fdset() failed, code %d.\n", mc);
        break;
      }

      /* On success the value of maxfd is guaranteed to be >= -1. We call
         select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
         no fds ready yet so we call select(0, ...) --or Sleep() on Windows--
         to sleep 100ms, which is the minimum suggested value in the
         curl_multi_fdset() doc. */

      if(maxfd == -1) {
#ifdef _WIN32
        Sleep(100);
        rc = 0;
#else
        /* Portable sleep for platforms other than Windows. */
        struct timeval wait = { 0, 100 * 1000 }; /* 100ms */
        rc = select(0, NULL, NULL, NULL, &wait);
#endif
      }
      else {
        /* Note that on some platforms 'timeout' may be modified by select().
           If you need access to the original value save a copy beforehand. */
        rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
      }

      switch(rc) {
      case -1:
        /* select error */
        break;
      case 0:
      default:
        /* timeout or readable/writable sockets */
        curl_multi_perform(multi_handle, &still_running);
        break;
      }
    }


    for (std::map<std::string, ParamFile>::iterator it = mapParams.begin(); it != mapParams.end(); ++it) {
        if (!it->second.verified) {
            fclose(it->second.file);
            curl_easy_cleanup(it->second.curl);
        }
    }

    curl_multi_cleanup(multi_handle);
    curl_global_cleanup();

    return true;
}
