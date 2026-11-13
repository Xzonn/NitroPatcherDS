#include "app_path.h"
#include "../patch/core.h"
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace np3d {
std::string appPath(int argc, char** argv, const void* header) {
    if (argc > 0 && argv && argv[0] && argv[0][0]) {
        std::string filename = argv[0];
        if (filename.compare(0, 5, "fat:/") == 0)
            filename = "sd:" + filename.substr(4);
        if (filename.compare(0, 6, "sdmc:/") == 0)
            filename = "sd:" + filename.substr(5);
        require(filename.compare(0, 4, "sd:/") == 0, "Start this app from the console SD card.");
        return filename;
    }
    require(header, "Cannot determine the app path.");
    std::vector<std::string> directories = {"sd:/"};
    while (!directories.empty()) {
        auto directory = directories.back();
        directories.pop_back();
        DIR* entries = opendir(directory.c_str());
        require(entries, "Cannot read app directory: " + directory);
        while (dirent* entry = readdir(entries)) {
            std::string name = entry->d_name;
            if (name == "." || name == "..")
                continue;
            std::string path = directory + name;
            struct stat info;
            require(stat(path.c_str(), &info) == 0, "Cannot inspect app path: " + path);
            if (S_ISDIR(info.st_mode))
                directories.push_back(path + "/");
            else if (S_ISREG(info.st_mode) && info.st_size >= 0x160 && name.size() > 4 &&
                     strcasecmp(name.c_str() + name.size() - 4, ".nds") == 0) {
                auto source = std::make_shared<Disk>(path);
                auto candidate = readBytes(source, 0, 0x160);
                // libnds stores struct __argv in the header at 0x02FFFE70..0x02FFFE8B.
                auto expected = static_cast<const uint8_t*>(header);
                if (std::memcmp(candidate.data(), expected, 0x70) == 0 &&
                    std::memcmp(candidate.data() + 0x8c, expected + 0x8c, candidate.size() - 0x8c) == 0) {
                    require(closedir(entries) == 0, "Cannot close app directory: " + directory);
                    return path;
                }
            }
        }
        require(closedir(entries) == 0, "Cannot close app directory: " + directory);
    }
    fail("Cannot find the running app on the storage card.");
}
}
