#include "Updater.h"
#include "bakkesmod/wrappers/PluginManagerWrapper.h"
#include <stdio.h>
#include <stdlib.h>

BAKKESMOD_PLUGIN(Updater, "MNCS Updater", "1.0.0", PERMISSION_ALL)

std::shared_ptr<CVarManagerWrapper> globalCvarManager;

// TODO: Check for updates using HTTP, which should return location and version
void Updater::onLoad()
{
    LOGC("Checking for updates...");
    // Read current version from file (if it exists)
    ifstream in(baseDir + LR"(\version)");
    if (in.is_open()) {
        LOG("version exists");
        string line;
        getline(in, line);
        if (line.find('.') == string::npos) {
            LOG("Current version: v" + line);
            curVersion = line;
        }
        in.close();
    }

    globalCvarManager = cvarManager;
    _wmkdir(baseDir.c_str());
    if (updateAvailable()) {
        LOG("Update available! v" + newVersion);
        std::shared_ptr<ModalWrapper> modal = std::make_shared<ModalWrapper>(gameWrapper->CreateModal("MNCS"));
        modal->AddButton("UPDATE", false, [&]() {
            LOG("User clicked update button");
            bool update = true;
            std::vector<std::shared_ptr<BakkesMod::Plugin::LoadedPlugin>>* plugins = gameWrapper->GetPluginManager().GetLoadedPlugins();
            for (int i = 0; i < plugins->size(); i++) {
                if (plugins->at(i)->_filename == "mncs") {
                    LOG("MNCS running, showing error modal");
                    update = false;
                    std::shared_ptr<ModalWrapper> err = std::make_shared<ModalWrapper>(gameWrapper->CreateModal("MNCS"));
                    err->AddButton("OK", true);
                    err->SetColor(240, 90, 90);
                    err->SetBody("MNCS.dll was already loaded! Please restart your game and try updating again.");
                    err->ShowModal();
                }
            }
            if (update) {
                LOG("Starting up thread.");
                std::shared_ptr<ModalWrapper> updating = std::make_shared<ModalWrapper>(gameWrapper->CreateModal("MNCS"));
                updating->SetBody("Updating...");
                updating->ShowModal();
                std::thread th([&, updating]() {
                    LOG("Downloading update");
                    download("https://www.dropbox.com/s/hgsweulwshxj9g6/SOS.dll?dl=1");
                    LOG("Copying to plugins");
                    CopyFile(saveDest.c_str(), pluginDest.c_str(), false);
                    gameWrapper->Execute([&, updating](GameWrapper*) {
                        LOG("Closing updating modal, done updating!");
                        updating->CloseModal();
                        std::shared_ptr<ModalWrapper> done = std::make_shared<ModalWrapper>(gameWrapper->CreateModal("MNCS"));
                        done->SetBody("Done! Would you like to launch MNCS now?");
                        done->AddButton("LAUNCH", false, [&]() {
                            LOG("Launching MNCS");
                            cvarManager->executeCommand("plugin load mncs");
                        });
                        done->AddButton("CANCEL", true);
                        done->ShowModal();
                    });
                });
                th.detach();
            }
        });
        modal->AddButton("CANCEL", true);
        modal->SetBody("\nA new update is available! Do you want to update it now?\n\nNew Version: v"+newVersion+"\nCurrent Version: v"+curVersion+"\n\n");
        modal->ShowModal();
    }
}

void Updater::onUnload() {}

size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    size_t written;
    written = fwrite(ptr, size, nmemb, stream);
    return written;
}

size_t write_data_string(const char* in, size_t size, size_t num, string* out)
{
    const std::size_t totalBytes(size * num);
    out->append(in, totalBytes);
    return totalBytes;
}

bool Updater::updateAvailable() {
    return true;
    CURL* curl = curl_easy_init();
    std::string url = "";
    if (curl) {
        LOGC("curl good, checking for updates");
        curl_easy_setopt(curl, CURLOPT_URL, url);

        // Don't bother trying IPv6, which would increase DNS resolution time.
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // Don't wait forever, time out after 10 seconds.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        // Follow HTTP redirects if necessary.
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // Response information.
        int httpCode(0);
        std::unique_ptr<std::string> httpData(new std::string());

        // Hook up data handling function.
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data_string);

        // Hook up data container (will be passed as the last parameter to the
        // callback handling function).  Can be any pointer type, since it will
        // internally be passed as a void pointer.
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, httpData.get());

        // Run our HTTP GET command, capture the HTTP response code, and clean up.
        curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);

        if (httpCode == 200)
        {
            LOGC("\nGot successful response from " + url);

            /*json data = json::parse(httpData.get());

            downloadURL = data["url"];*/
        }
    }
    return false;
}

bool Updater::download(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (curl) {
        LOGC("curl good, downloading " + url);
        FILE* fp;
        _wfopen_s(&fp, saveDest.c_str(), LR"(wb)");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        /* we tell libcurl to follow redirection */
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1); //Prevent "longjmp causes uninitialized stack frame" bug
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "deflate");
        std::stringstream out;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        /* Perform the request, res will get the return code */
        CURLcode res = curl_easy_perform(curl);
        /* Check for errors */
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
        }
        fclose(fp);
        LOGC("downloaded!");
        curl_easy_cleanup(curl);
        return true;
    }
    return false;
}

void Updater::log(string text, bool console) {
    ofstream file(baseDir + LR"(\log.txt)", ios_base::app);
    file << text << endl;
    file.close();
    if (console) {
        cvarManager->log(text);
    }
}