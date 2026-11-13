#include "nds_adapter.h"
#include "filename.h"
#include <cstring>
#include <set>

Header header;
FILE* fNDS;
int verbose = 0;
char* filerootdir = nullptr;
char* filemasks[MAX_FILEMASKS] = {};
int filemask_num = 0;
void (*ndsFileVisitor)(const char*, const char*, unsigned int, unsigned int, unsigned int);
void (*ndsDirectoryVisitor)(const char*, unsigned int, unsigned int, bool);
extern void ExtractDirectory(const char*, unsigned int);
static std::set<unsigned int> directories;

void ndsRequire(bool condition, const char* message) { np3d::patchRequire(condition, message); }

void ndsDirectoryCheck(const char* prefix, unsigned int id) {
    NP3D_PREPARE_LOG("NDS directory id=" + std::to_string(id) + " path=" + prefix);
    np3d::patchRequire(id >= 0xf000 && id <= 0xffff && directories.insert(id).second,
                       "Invalid NDS directory tree or cycle");
    np3d::patchRequire(std::count(prefix, prefix + std::strlen(prefix), '/') <= 257,
                       "NDS directory nesting exceeds limit");
}

namespace np3d {

struct Cookie {
    SourcePtr source;
    uint32_t position = 0;
};

static FileTable* files;
static std::vector<bool> visitedFiles;
static bool canonicalNames;
static std::vector<NamedDirectory>* tree;
static std::vector<uint32_t> directoryIndices;
static uint32_t currentDirectory;
static uint32_t romSize;

static void directory(const char* prefix, unsigned int id, unsigned int count, bool entering) {
    if (!entering)
        return;
    uint32_t parent = id == 0xf000 ? UINT32_MAX : directoryIndices.at(count & 0xfff);
    patchRequire(id == 0xf000 || parent != UINT32_MAX, "Missing FNT parent directory");
    std::string path(prefix), raw;
    if (path.size() > 1) {
        path.pop_back();
        raw = path.substr(path.find_last_of('/') + 1);
    }
    std::string name = parent == UINT32_MAX ? "data" : decodeName(raw);
    patchRequire(name.find_first_of("/\\") == std::string::npos && name != "." && name != "..",
                 "Invalid FNT path component");
    uint32_t index = tree->size();
    tree->push_back({name, raw, parent, {}, {}});
    if (parent != UINT32_MAX)
        (*tree)[parent].children.push_back(index);
    directoryIndices[id & 0xfff] = index;
    currentDirectory = index;
}

static void visit(const char* prefix, const char* name, unsigned int id, unsigned int offset, unsigned int length) {
    patchRequire(id < uint32_t(header.fat_size) / 8, "NDS file ID outside FAT");
    patchRequire(!visitedFiles[id], "Duplicate NDS file ID");
    visitedFiles[id] = true;
    std::string path = std::string(prefix) + name;
    if (std::any_of(path.begin(), path.end(), [](unsigned char byte) { return byte >= 128; }))
        path = canonicalNames ? decodeName(path) : convert(path, "CP932");
    uint32_t parent = tree ? currentDirectory : 0;
    if (tree) {
        auto decoded = decodeName(name);
        patchRequire(decoded.find_first_of("/\\") == std::string::npos && decoded != "." && decoded != "..",
                     "Invalid FNT path component");
        (*tree)[parent].files.push_back(files->size());
    }
    patchRequire(offset <= romSize && length <= romSize - offset, "NDS file outside ROM: data" + path);
    files->push_back({"data" + path, id, nullptr, id, offset, length, canonicalNames ? name : "", parent});
}

#ifdef ARM9
static int readCookie(void* opaque, char* output, int length) {
#else
static ssize_t readCookie(void* opaque, char* output, size_t length) {
#endif
    auto& cookie = *static_cast<Cookie*>(opaque);
    uint32_t take = std::min<uint64_t>(length, cookie.source->size - cookie.position);
    cookie.source->read(cookie.position, output, take);
    cookie.position += take;
    return take;
}

#ifdef ARM9
static fpos_t seekCookie(void* opaque, fpos_t offset, int origin) {
    auto& cookie = *static_cast<Cookie*>(opaque);
    int64_t position = (origin == SEEK_CUR   ? cookie.position
                        : origin == SEEK_END ? cookie.source->size
                                             : 0) +
                       int64_t(offset);
    require(position >= 0 && uint64_t(position) <= cookie.source->size, "NDS directory seek outside ROM");
    cookie.position = position;
    return position;
}
#else
static int seekCookie(void* opaque, off64_t* offset, int origin) {
    auto& cookie = *static_cast<Cookie*>(opaque);
    int64_t position = (origin == SEEK_CUR ? cookie.position : origin == SEEK_END ? cookie.source->size : 0) + *offset;
    require(position >= 0 && uint64_t(position) <= cookie.source->size, "NDS directory seek outside ROM");
    cookie.position = position;
    *offset = position;
    return 0;
}
#endif

FileTable enumerateFiles(SourcePtr source, const Header& value, Workspace& work, bool canonical,
                         std::vector<NamedDirectory>* outputDirectories) {
    Cookie cookie{source};
#ifdef ARM9
    fNDS = funopen(&cookie, readCookie, nullptr, seekCookie, nullptr);
#else
    cookie_io_functions_t functions = {readCookie, nullptr, seekCookie, nullptr};
    fNDS = fopencookie(&cookie, "rb", functions);
#endif
    require(fNDS, "Cannot open NDS directory reader");
    std::unique_ptr<FILE, decltype(&std::fclose)> reader(fNDS, std::fclose);
    header = value;
    romSize = source->size;
    canonicalNames = canonical;
    std::vector<NamedDirectory> localDirectories;
    tree = canonical ? (outputDirectories ? outputDirectories : &localDirectories) : nullptr;
    directoryIndices.assign(tree ? 4096 : 0, UINT32_MAX);
    if (tree)
        tree->clear();
    directories.clear();
    visitedFiles.assign(uint32_t(header.fat_size) / 8, false);
    FileTable result(work, source);
    files = &result;
    ndsFileVisitor = visit;
    ndsDirectoryVisitor = tree ? directory : nullptr;
    ExtractDirectory("/", 0xf000);
    require(!std::ferror(fNDS), "Cannot read NDS directory");
    require(std::fclose(reader.release()) == 0, "Cannot close NDS directory reader");
    fNDS = nullptr;
    ndsFileVisitor = nullptr;
    ndsDirectoryVisitor = nullptr;
    tree = nullptr;
    directoryIndices.clear();
    directoryIndices.shrink_to_fit();
    return result;
}

}
