#include "environment.h"

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace np3d {

void environmentRecord(FILE* report, const char* format, ...) {
    int savedError = errno;
    va_list arguments;
    va_start(arguments, format);
    int result = std::vfprintf(report, format, arguments);
    va_end(arguments);
    require(result >= 0 && std::fputc('\n', report) != EOF, "Cannot write environment report");
    require(std::fflush(report) == 0, "Cannot flush environment report");
    require(fsync(fileno(report)) == 0, "Cannot sync environment report");
    errno = savedError;
}

static void recordResult(FILE* report, const char* operation, long long result, int error) {
    environmentRecord(report, "%s result=%lld errno=%d (%s)", operation, result, error, std::strerror(error));
}

struct ProbeCookie {
    const std::vector<uint8_t>& data;
    uint32_t position = 0;
};

#ifdef ARM9
static int readProbe(void* opaque, char* output, int length) {
#else
static ssize_t readProbe(void* opaque, char* output, size_t length) {
#endif
    auto& cookie = *static_cast<ProbeCookie*>(opaque);
    uint32_t take = std::min<uint64_t>(length, cookie.data.size() - cookie.position);
    std::memcpy(output, cookie.data.data() + cookie.position, take);
    cookie.position += take;
    return take;
}

#ifdef ARM9
static fpos_t seekProbe(void* opaque, fpos_t offset, int origin) {
#else
static int seekProbe(void* opaque, off64_t* offset, int origin) {
#endif
    auto& cookie = *static_cast<ProbeCookie*>(opaque);
    int64_t base = origin == SEEK_CUR ? cookie.position : origin == SEEK_END ? cookie.data.size() : 0;
#ifdef ARM9
    int64_t position = base + offset;
#else
    int64_t position = base + *offset;
#endif
    if (position < 0 || uint64_t(position) > cookie.data.size()) {
        errno = EINVAL;
        return -1;
    }
    cookie.position = position;
#ifdef ARM9
    return position;
#else
    *offset = position;
    return 0;
#endif
}

static void probeDirectoryRemoval(FILE* report, const std::string& path) {
    environmentRecord(report, "directory.remove.begin path=%s", path.c_str());
    errno = 0;
#ifdef ARM9
    int result = unlink(path.c_str());
    recordResult(report, "unlink.directory", result, errno);
#else
    int result = rmdir(path.c_str());
    recordResult(report, "rmdir.directory", result, errno);
#endif
    require(result == 0, "Environment probe cannot remove directory " + path);
}

void probeFilesystem(FILE* report, const std::string& directory) {
    environmentRecord(report, "filesystem.probe.begin directory=%s", directory.c_str());
    long savedPosition = std::ftell(report);
    require(savedPosition > 0, "Cannot locate environment report data");
    std::vector<uint8_t> sample(std::min<uint32_t>(savedPosition, BufferSize));
    require(std::fseek(report, 0, SEEK_SET) == 0, "Cannot seek environment report sample");
    require(std::fread(sample.data(), 1, sample.size(), report) == sample.size(),
            "Cannot read environment report sample");
    require(std::fseek(report, savedPosition, SEEK_SET) == 0, "Cannot restore environment report position");
    environmentRecord(report, "filesystem.sample source=environment-report bytes=%u", unsigned(sample.size()));
    environmentRecord(report, "filesystem.cleanup.begin");
    removeTree(directory);
    environmentRecord(report, "filesystem.cleanup.done");

    std::string pending = directory + "/pending";
    std::string work = pending + "/work";
    std::string ready = directory + "/ready";
    for (const auto& path : {directory, pending, work}) {
        environmentRecord(report, "mkdir.begin path=%s", path.c_str());
        errno = 0;
        int result = mkdir(path.c_str(), 0777);
        recordResult(report, "mkdir", result, errno);
        require(result == 0, "Environment probe cannot create directory " + path);
    }
    std::string temporary = work + "/sample.new", published = work + "/sample.bin";
    environmentRecord(report, "fopen.write.begin path=%s", temporary.c_str());
    errno = 0;
    FILE* output = std::fopen(temporary.c_str(), "wb");
    recordResult(report, "fopen.write", output ? 0 : -1, errno);
    require(output, "Environment probe cannot create sample file");
    environmentRecord(report, "fwrite.begin bytes=%u", unsigned(sample.size()));
    errno = 0;
    size_t written = std::fwrite(sample.data(), 1, sample.size(), output);
    recordResult(report, "fwrite", written, errno);
    require(written == sample.size(), "Environment probe cannot write sample file");
    environmentRecord(report, "fflush.begin");
    errno = 0;
    int result = std::fflush(output);
    recordResult(report, "fflush", result, errno);
    require(result == 0, "Environment probe cannot flush sample file");
    environmentRecord(report, "fsync.begin");
    errno = 0;
    result = fsync(fileno(output));
    recordResult(report, "fsync", result, errno);
    require(result == 0, "Environment probe cannot sync sample file");
    environmentRecord(report, "fclose.write.begin");
    errno = 0;
    result = std::fclose(output);
    recordResult(report, "fclose.write", result, errno);
    require(result == 0, "Environment probe cannot close sample file");

    environmentRecord(report, "stat.begin path=%s", temporary.c_str());
    struct stat info;
    errno = 0;
    result = stat(temporary.c_str(), &info);
    recordResult(report, "stat", result, errno);
    require(result == 0 && info.st_size == off_t(sample.size()), "Environment probe sample size mismatch");
    environmentRecord(report, "stat size=%lld mode=%lo", static_cast<long long>(info.st_size),
                      static_cast<unsigned long>(info.st_mode));
    environmentRecord(report, "fopen.read.begin");
    errno = 0;
    FILE* input = std::fopen(temporary.c_str(), "rb");
    recordResult(report, "fopen.read", input ? 0 : -1, errno);
    require(input, "Environment probe cannot reopen sample file");
    environmentRecord(report, "fseeko.begin");
    errno = 0;
    result = fseeko(input, 1, SEEK_SET);
    recordResult(report, "fseeko", result, errno);
    require(result == 0, "Environment probe cannot seek sample file");
    errno = 0;
    off_t position = ftello(input);
    recordResult(report, "ftello", position, errno);
    require(position == 1, "Environment probe file position mismatch");
    environmentRecord(report, "fseek.begin");
    errno = 0;
    result = std::fseek(input, 0, SEEK_SET);
    recordResult(report, "fseek", result, errno);
    require(result == 0 && std::ftell(input) == 0, "Environment probe cannot rewind sample file");
    std::vector<uint8_t> actual(sample.size());
    environmentRecord(report, "fread.begin bytes=%u", unsigned(actual.size()));
    errno = 0;
    size_t received = std::fread(actual.data(), 1, actual.size(), input);
    recordResult(report, "fread", received, errno);
    require(received == sample.size() && actual == sample, "Environment probe sample bytes differ");
    environmentRecord(report, "fclose.read.begin");
    errno = 0;
    result = std::fclose(input);
    recordResult(report, "fclose.read", result, errno);
    require(result == 0, "Environment probe cannot close sample reader");

    environmentRecord(report, "cookie.open.begin");
    ProbeCookie cookie{sample};
#ifdef ARM9
    FILE* stream = funopen(&cookie, readProbe, nullptr, seekProbe, nullptr);
#else
    cookie_io_functions_t functions = {readProbe, nullptr, seekProbe, nullptr};
    FILE* stream = fopencookie(&cookie, "rb", functions);
#endif
    require(stream, "Environment probe cannot open callback reader");
    environmentRecord(report, "cookie.seek.begin");
    errno = 0;
    result = std::fseek(stream, -1, SEEK_END);
    recordResult(report, "cookie.fseek", result, errno);
    require(result == 0 && std::ftell(stream) == long(sample.size() - 1), "Environment probe callback seek mismatch");
    environmentRecord(report, "cookie.read.begin");
    uint8_t last;
    errno = 0;
    received = std::fread(&last, 1, 1, stream);
    recordResult(report, "cookie.fread", received, errno);
    require(received == 1 && last == sample.back(), "Environment probe callback bytes differ");
    environmentRecord(report, "cookie.close.begin");
    errno = 0;
    result = std::fclose(stream);
    recordResult(report, "cookie.fclose", result, errno);
    require(result == 0, "Environment probe cannot close callback reader");

    environmentRecord(report, "rename.file.begin");
    errno = 0;
    result = std::rename(temporary.c_str(), published.c_str());
    recordResult(report, "rename.file", result, errno);
    require(result == 0, "Environment probe cannot rename sample file");
    environmentRecord(report, "opendir.begin");
    errno = 0;
    DIR* entries = opendir(work.c_str());
    recordResult(report, "opendir", entries ? 0 : -1, errno);
    require(entries, "Environment probe cannot open sample directory");
    bool found = false;
    while (true) {
        environmentRecord(report, "readdir.begin");
        errno = 0;
        dirent* entry = readdir(entries);
        int error = errno;
        recordResult(report, "readdir", entry ? 1 : 0, error);
        if (!entry) {
            require(error == 0 || error == ENOENT, "Environment probe cannot read sample directory");
            break;
        }
        environmentRecord(report, "readdir.name=%s", entry->d_name);
        if (std::strcmp(entry->d_name, "sample.bin") == 0)
            found = true;
    }
    require(found, "Environment probe directory entry is missing");
    environmentRecord(report, "closedir.begin");
    errno = 0;
    result = closedir(entries);
    recordResult(report, "closedir", result, errno);
    require(result == 0, "Environment probe cannot close sample directory");
    environmentRecord(report, "rename.directory.begin");
    errno = 0;
    result = std::rename(pending.c_str(), ready.c_str());
    recordResult(report, "rename.directory", result, errno);
    require(result == 0, "Environment probe cannot rename sample directory");
    environmentRecord(report, "unlink.file.begin");
    errno = 0;
    result = unlink((ready + "/work/sample.bin").c_str());
    recordResult(report, "unlink.file", result, errno);
    require(result == 0, "Environment probe cannot delete sample file");
    probeDirectoryRemoval(report, ready + "/work");
    probeDirectoryRemoval(report, ready);
    probeDirectoryRemoval(report, directory);
    environmentRecord(report, "filesystem.probe.complete");
}

}
