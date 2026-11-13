#include "package.h"
#include <cerrno>

namespace np3d {

#if NP3D_PREPARE_TRACE
static size_t readArchive(void* opaque, mz_uint64 offset, void* output, size_t length) {
    auto& package = *static_cast<Package*>(opaque);
    logPreparation("ZIP read begin offset=" + std::to_string(offset) + " requested=" + std::to_string(length));
    size_t result = package.reader(package.readOpaque, offset, output, length);
    int error = errno;
    logPreparation("ZIP read done offset=" + std::to_string(offset) + " returned=" + std::to_string(result) +
                   " errno=" + std::to_string(result == length ? 0 : error));
    errno = error;
    return result;
}
#endif

Package::Package(const std::string& path) {
    logPreparation("ZIP open begin " + path);
    require(mz_zip_reader_init_file(&archive, path.c_str(), 0), "Cannot open XZP archive");
#if NP3D_PREPARE_TRACE
    reader = archive.m_pRead;
    readOpaque = archive.m_pIO_opaque;
    archive.m_pRead = readArchive;
    archive.m_pIO_opaque = this;
#endif
    uint64_t total = 0;
    unsigned int count = mz_zip_reader_get_num_files(&archive);
    logPreparation("ZIP open done entries=" + std::to_string(count));
    require(count <= 100000, "XZP contains too many entries");
    progress("Reading patch directory", 0, count);
    for (unsigned int index = 0; index < count; index++) {
        mz_zip_archive_file_stat info;
        require(mz_zip_reader_file_stat(&archive, index, &info), "Cannot read XZP directory");
        require(info.m_is_supported && !info.m_is_encrypted, "Unsupported XZP compression or encryption");
        total += info.m_uncomp_size;
        require(total <= 0x40000000, "XZP uncompressed size exceeds limit");
        std::string name(info.m_full_filename, info.m_full_filename_size);
        bool ascii = std::all_of(name.begin(), name.end(), [](unsigned char byte) { return byte < 128; });
        if (!ascii)
            name = convert(name, info.m_filename_utf8 ? "UTF-8" : "CP437");
        for (char& byte : name)
            if (byte == '\\')
                byte = '/';
        std::string key = normalize(name);
        NP3D_PREPARE_LOG(
            "ZIP entry=" + std::to_string(index) + " name=" + name + " method=" + std::to_string(info.m_method) +
            " compressed=" + std::to_string(info.m_comp_size) + " size=" + std::to_string(info.m_uncomp_size) +
            " crc32=" + std::to_string(info.m_crc32) + " header=" + std::to_string(info.m_local_header_ofs));
        if (!name.empty() && name.back() != '/') {
            entries[key] = {index, std::move(name)};
        }
        progress("Reading patch directory", index + 1, count);
    }
    if (has("metadata.json")) {
        auto data = bytes("metadata.json");
        metadata = readPatchMetadata(std::string(data.begin(), data.end()));
    }
}

Package::Package(const std::string& path, Workspace& directory) : Package(path) { work = &directory; }

Package::~Package() {
    NP3D_PREPARE_LOG("ZIP close begin");
    require(mz_zip_reader_end(&archive), "Cannot close XZP archive");
    NP3D_PREPARE_LOG("ZIP close done");
}

bool Package::has(const std::string& name) const { return entries.find(normalize(name)) != entries.end(); }

bool Package::needsMd5() const { return has("md5.txt") || hasPreprocessing(); }

bool Package::hasPreprocessing() const {
    auto found = entries.lower_bound("preprocessing/");
    return found != entries.end() && found->first.compare(0, 14, "preprocessing/") == 0;
}

struct Entry final : Source {
    Package& package;
    std::string name;
    mz_zip_reader_extract_iter_state* iterator;
    uint32_t position = 0;
    Entry(Package& owner, const std::string& filename) : package(owner), name(filename) {
        auto found = package.entries.find(normalize(name));
        require(found != package.entries.end(), "Missing XZP entry: " + name);
        mz_zip_archive_file_stat info;
        require(mz_zip_reader_file_stat(&package.archive, found->second.index, &info), "Cannot read XZP directory");
        size = info.m_uncomp_size;
        logPreparation("EXTRACT begin name=" + name + " bytes=" + std::to_string(size));
        iterator = mz_zip_reader_extract_iter_new(&package.archive, found->second.index, 0);
        require(iterator, "Cannot initialize XZP extraction: " + name);
        progress("Extracting patch file", 0, size, true, name.c_str());
    }
    ~Entry() {
        if (iterator)
            mz_zip_reader_extract_iter_free(iterator);
    }
    void read(uint32_t offset, void* output, uint32_t length) override {
        require(offset == position && length <= size - position, "Nonsequential XZP entry read");
        NP3D_PREPARE_LOG("INFLATE begin name=" + name + " output=" + std::to_string(position));
        size_t received = mz_zip_reader_extract_iter_read(iterator, output, length);
        if (received != length || iterator->status < TINFL_STATUS_DONE)
            fail("Cannot decode XZP entry: " + name);
        position += length;
        NP3D_PREPARE_LOG("INFLATE done name=" + name + " output=" + std::to_string(position));
        progress("Extracting patch file", position, size, true, name.c_str());
    }
    void verify() {
        uint8_t byte;
        require(position == size && mz_zip_reader_extract_iter_read(iterator, &byte, 1) == 0, "Incomplete XZP entry");
        mz_bool verified = mz_zip_reader_extract_iter_free(iterator);
        iterator = nullptr;
        if (!verified)
            fail("Cannot verify XZP entry: " + name);
        logPreparation("EXTRACT done name=" + name + " bytes=" + std::to_string(position));
    }
};

std::vector<uint8_t> Package::bytes(const std::string& name) {
    Entry input(*this, name);
    std::vector<uint8_t> result(input.size);
    for (uint32_t offset = 0; offset < input.size;) {
        uint32_t take = std::min(BufferSize, input.size - offset);
        input.read(offset, result.data() + offset, take);
        offset += take;
    }
    input.verify();
    return result;
}

SourcePtr Package::delta(const std::string& name, SourcePtr source, const std::string& label) {
    auto input = std::make_shared<Entry>(*this, name);
    auto result = applyDelta(*work, source, input, label);
    if (result)
        input->verify();
    return result;
}

SourcePtr Package::replace(const std::string& name, SourcePtr source) {
    std::string patch = "xdelta/" + normalize(name);
    if (has(patch))
        return delta(patch, source, name);
    if (!has(name))
        return source;
    Entry input(*this, name);
    auto result = work->map();
    std::vector<uint8_t> buffer(BufferSize);
    progress("Applying replacement", 0, input.size, true, name.c_str());
    for (uint32_t offset = 0; offset < input.size;) {
        uint32_t take = std::min(BufferSize, input.size - offset);
        input.read(offset, buffer.data(), take);
        result->literal(buffer.data(), take);
        offset += take;
        progress("Applying replacement", offset, input.size, true, name.c_str());
    }
    input.verify();
    result->finish();
    return result;
}

}
