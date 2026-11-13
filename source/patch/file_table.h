#pragma once
#include "core.h"

namespace np3d {

struct FileEntry {
    std::string name;
    uint32_t id;
    SourcePtr data;
    uint32_t original;
    uint32_t offset = 0, size = 0;
    std::string rawName;
    uint32_t directory = 0;
};

class FileTable {
    struct Text {
        uint32_t offset, size;
    };
    struct Record {
        Text name, rawName;
        uint32_t id, original, offset, size, directory, source;
    };
    struct Memory {
        Workspace& work;
        size_t bytes = 0;
        std::vector<Record> records;
        std::vector<char> text;
        explicit Memory(Workspace& work) : work(work) {}
        ~Memory() { work.directoryMemory += bytes; }
    };
    struct Page {
        std::array<Record, 128> records;
        uint32_t first = UINT32_MAX, age = 0;
        bool dirty = false;
    };
    SourcePtr input;
    std::string path;
    std::unique_ptr<Memory> memory;
    std::shared_ptr<Disk> records, text;
    std::unique_ptr<Cached> names;
    std::unique_ptr<std::array<Page, 2>> pages;
    Page* page = nullptr;
    std::vector<SourcePtr> sources;
    uint32_t count = 0, age = 0;
    void reserve(uint32_t rows, uint32_t bytes);
    void flush();
    void flush(Page& page);
    void load(uint32_t index);
    Record record(uint32_t index);
    Text store(const std::string& value);
    std::string string(Text value);
    uint32_t source(SourcePtr data, uint32_t offset, uint32_t size);

  public:
    FileTable(Workspace& work, SourcePtr input);
    FileTable(FileTable&&) = default;
    FileTable(const FileTable&) = delete;
    ~FileTable();
    uint32_t size() const { return count; }
    FileEntry get(uint32_t index);
    std::string name(uint32_t index);
    void push_back(const FileEntry& file);
    void setId(uint32_t index, uint32_t id);
    void setData(uint32_t index, SourcePtr data);
    uint32_t find(const std::string& name);
    void clear();
};

}
