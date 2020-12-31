
#include "params.h"
#include "ui_interface.h"

std::map<std::string, ParamFile> mapParams;
static const int K_READ_BUF_SIZE{ 1024 * 16 };

std::string CalcSha256(std::string filename)
{
    // Initialize openssl
    SHA256_CTX context;
    if(!SHA256_Init(&context)) {
        return "";
    }

    // Read file and update calculated SHA
    char buf[K_READ_BUF_SIZE];
    std::ifstream file(filename, std::ifstream::binary);
    while (file.good()) {
        file.read(buf, sizeof(buf));
        if(!SHA256_Update(&context, buf, file.gcount())) {
            return "";
        }
    }

    // Get Final SHA
    unsigned char result[SHA256_DIGEST_LENGTH];
    if(!SHA256_Final(result, &context)) {
        return "";
    }

    // Transform byte-array to string
    std::stringstream shastr;
    shastr << std::hex << std::setfill('0');
    for (const auto &byte: result) {
        shastr << std::setw(2) << (int)byte;
    }
    return shastr.str();
}



bool checkParams() {
    bool allVerified = true;
    for (std::map<std::string, ParamFile>::iterator it = mapParams.begin(); it != mapParams.end(); ++it) {
        std::string uiMessage = "Verifying " + it->second.name + "....";
        uiInterface.InitMessage(_(uiMessage.c_str()));

        std::string sha256Sum = CalcSha256(it->second.path.string());

        LogPrintf("sha256Sum %s\n", sha256Sum);
        LogPrintf("checkSum %s\n", it->second.hash);

        if (sha256Sum == it->second.hash) {
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

void initalizeMapParamBootstrap() {
  mapParams.clear();

  ParamFile bootFile;
  bootFile.name = "bootstrap";
  bootFile.URL = "http://bootstrap.dexstats.info/ARRR-bootstrap.tar.gz";
  bootFile.verified = false;
  bootFile.path = GetDataDir() / "ARRR-bootstrap.tar.gz";
  bootFile.dlnow = 0;
  bootFile.dltotal = 0;
  mapParams[bootFile.URL] = bootFile;

}


void initalizeMapParam() {

    mapParams.clear();

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

bool downloadFiles(std::string title)
{

    bool downloadComplete = true;
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
            it->second.curl = curl_easy_init();
            if(it->second.curl) {
                it->second.prog.lastruntime = 0;
                it->second.prog.curl = it->second.curl;
            }

            //open file for writing
            it->second.file = fopen(it->second.path.string().c_str(), "wb");
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

    std::string uiMessage;
    uiMessage = "Downloading " + title + "......0.00%";
    uiInterface.InitMessage(_(uiMessage.c_str()));
    int64_t nNow = GetTime();

    while(still_running) {
      boost::this_thread::interruption_point();

      if (ShutdownRequested()) {
          downloadComplete = false;
          break;
      }

      if (GetTime() >= nNow + 1) {
          nNow = GetTime();
          int64_t dltotal = 0;
          int64_t dlnow = 0;
          for (std::map<std::string, ParamFile>::iterator it = mapParams.begin(); it != mapParams.end(); ++it) {
              if (!it->second.verified) {
                  dltotal += it->second.dltotal;
                  dlnow += it->second.dlnow;
              }
          }
          double pert = 0.00;
          if (dltotal > 0) {
              pert = (dlnow / (double)dltotal) * 100;
          }
          uiMessage = "Downloading " + title + "......" + std::to_string(pert).substr(0,10) + "%";
          uiInterface.InitMessage(_(uiMessage.c_str()));
      }

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
        downloadComplete = false;
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
        downloadComplete = false;
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

    return downloadComplete;
}


void getBootstrap() {
    initalizeMapParamBootstrap();
    bool dlsuccess = downloadFiles("Bootstrap");

    for (std::map<std::string, ParamFile>::iterator it = mapParams.begin(); it != mapParams.end(); ++it) {
        const char *path = it->second.path.string().c_str();
        if (dlsuccess) {
            extract(path);
        }
        if (boost::filesystem::exists(it->second.path.string())) {
            boost::filesystem::remove(it->second.path.string());
        }
    }
}


bool extract(const char *filename) {

  bool extractComplete = true;
	struct archive *a;
	struct archive *ext;
	struct archive_entry *entry;
	int r;

  int flags = ARCHIVE_EXTRACT_TIME;
  flags |= ARCHIVE_EXTRACT_PERM;
  flags |= ARCHIVE_EXTRACT_ACL;
  flags |= ARCHIVE_EXTRACT_FFLAGS;

	a = archive_read_new();
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
  archive_write_disk_set_standard_lookup(ext);

	if (archive_read_support_format_tar(a) != ARCHIVE_OK)
      extractComplete = false;

  if (archive_read_support_filter_gzip(a) != ARCHIVE_OK)
        extractComplete = false;

	if (filename != NULL && strcmp(filename, "-") == 0)
		filename = NULL;

  r = archive_read_open_filename(a, filename, 10240);
	if (r != ARCHIVE_OK) {
      LogPrintf("archive_read_open_filename() %s %d\n",archive_error_string(a), r);
      extractComplete = false;
  }

  if (extractComplete) {
      for (;;) {
          r = archive_read_next_header(a, &entry);
          if (r == ARCHIVE_EOF) {
              break;
          }
          if (r != ARCHIVE_OK) {
              LogPrintf("archive_read_next_header() %s %d\n",archive_error_string(a), r);
              extractComplete = false;
          }

          const char* currentFile = archive_entry_pathname(entry);
          std::string path = GetDataDir().string() + "/" + currentFile;
          std::string uiMessage = "Extracting Bootstrap file ";
          uiMessage.append(currentFile);
          uiInterface.InitMessage(_(uiMessage.c_str()));
          archive_entry_set_pathname(entry, path.c_str());
          r = archive_write_header(ext, entry);
          if (r != ARCHIVE_OK) {
              LogPrintf("archive_write_header() %s %d\n",archive_error_string(ext), r);
              extractComplete = false;
          } else {
              copy_data(a, ext);
              r = archive_write_finish_entry(ext);
              if (r != ARCHIVE_OK) {
                  LogPrintf("archive_write_finish_entry() %s %d\n",archive_error_string(ext), r);
                  extractComplete = false;
              }
          }
      }
  }

	archive_read_close(a);
	archive_read_free(a);

	archive_write_close(ext);
  archive_write_free(ext);

	return extractComplete;
}


static int copy_data(struct archive *ar, struct archive *aw) {
    int r;
    const void *buff;
    size_t size;
    int64_t offset;

    for (;;) {
        r = archive_read_data_block(ar, &buff, &size, &offset);

        if (r == ARCHIVE_EOF)
            return (ARCHIVE_OK);

        if (r != ARCHIVE_OK)
            return (r);

        r = archive_write_data_block(aw, buff, size, offset);
        if (r != ARCHIVE_OK) {
            LogPrintf("archive_write_data_block() %s\n",archive_error_string(aw));
            return (r);
        }
    }
}
