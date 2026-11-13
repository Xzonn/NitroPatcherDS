#include "file_table.h"
#ifdef ARM9
#include <nds.h>
#include <malloc.h>
#endif

namespace np3d {

FileTable::FileTable(Workspace& work, SourcePtr original)
    : input(original), path(work.directory + "/files-" + std::to_string(work.next++)),
      memory(std::make_unique<Memory>(work)) {}

void FileTable::reserve(uint32_t rows, uint32_t bytes) {
    if (!memory)
        return;
    size_t rowCapacity = memory->records.capacity(), textCapacity = memory->text.capacity();
    size_t newRows = rows > rowCapacity ? std::max<size_t>(rows, std::max<size_t>(128, rowCapacity * 2)) : rowCapacity;
    size_t newText =
        bytes > textCapacity ? std::max<size_t>(bytes, std::max<size_t>(4096, textCapacity * 2)) : textCapacity;
    size_t total = newRows * sizeof(Record) + newText;
    bool fits = total - memory->bytes <= memory->work.directoryMemory;
#ifdef ARM9
    size_t available =
        reinterpret_cast<uintptr_t>(getHeapLimit()) - reinterpret_cast<uintptr_t>(getHeapEnd()) + mallinfo().fordblks;
    size_t growing = (newRows != rowCapacity ? newRows * sizeof(Record) : 0) + (newText != textCapacity ? newText : 0);
    fits = fits && growing <= (available > 1024 * 1024 ? available - 1024 * 1024 : 0);
#endif
    if (fits) {
        memory->records.reserve(newRows);
        memory->text.reserve(newText);
        memory->work.directoryMemory -= total - memory->bytes;
        memory->bytes = total;
        return;
    }
    records = std::make_shared<Disk>(path + ".bin", "w+b", true);
    text = std::make_shared<Disk>(path + "-names.bin", "w+b", true);
    records->write(0, memory->records.data(), memory->records.size() * sizeof(Record));
    text->write(0, memory->text.data(), memory->text.size());
    memory.reset();
    names = std::make_unique<Cached>(text);
}

FileTable::~FileTable() { flush(); }

void FileTable::flush() {
    if (pages)
        for (auto& item : *pages)
            flush(item);
}

void FileTable::flush(Page& item) {
    if (item.dirty) {
        records->write(item.first * sizeof(Record), item.records.data(),
                       std::min<uint32_t>(item.records.size(), count - item.first) * sizeof(Record));
        item.dirty = false;
    }
}

void FileTable::load(uint32_t index) {
    uint32_t base = index / 128 * 128;
    if (!pages)
        pages = std::make_unique<std::array<Page, 2>>();
    for (auto& item : *pages)
        if (item.first == base) {
            page = &item;
            page->age = ++age;
            return;
        }
    page = &(*pages)[0];
    for (auto& item : *pages)
        if (item.age < page->age)
            page = &item;
    flush(*page);
    uint32_t stored = records->size / sizeof(Record);
    if (base < stored)
        records->read(base * sizeof(Record), page->records.data(),
                      std::min<uint32_t>(page->records.size(), stored - base) * sizeof(Record));
    page->first = base;
    page->age = ++age;
}

FileTable::Record FileTable::record(uint32_t index) {
    require(index < count, "File index outside directory table");
    if (memory)
        return memory->records[index];
    load(index);
    return page->records[index - page->first];
}

FileTable::Text FileTable::store(const std::string& value) {
    if (memory) {
        Text result{uint32_t(memory->text.size()), uint32_t(value.size())};
        memory->text.insert(memory->text.end(), value.begin(), value.end());
        return result;
    }
    Text result{text->size, uint32_t(value.size())};
    text->write(result.offset, value.data(), result.size);
    return result;
}

std::string FileTable::string(Text value) {
    if (memory)
        return std::string(memory->text.data() + value.offset, value.size);
    std::string result(value.size, '\0');
    names->read(value.offset, result.data(), value.size);
    return result;
}

uint32_t FileTable::source(SourcePtr data, uint32_t offset, uint32_t size) {
    if (!data)
        return UINT32_MAX;
    auto slice = std::dynamic_pointer_cast<Slice>(data);
    if (slice && slice->input == input && slice->base == offset && slice->size == size)
        return UINT32_MAX;
    for (uint32_t index = 0; index < sources.size(); index++)
        if (sources[index] == data)
            return index;
    sources.push_back(data);
    return sources.size() - 1;
}

FileEntry FileTable::get(uint32_t index) {
    auto row = record(index);
    SourcePtr data =
        row.source == UINT32_MAX ? std::make_shared<Slice>(input, row.offset, row.size) : sources[row.source];
    return {string(row.name), row.id, data, row.original, row.offset, row.size, string(row.rawName), row.directory};
}

std::string FileTable::name(uint32_t index) { return string(record(index).name); }

void FileTable::push_back(const FileEntry& file) {
    reserve(count + 1, memory ? memory->text.size() + file.name.size() + file.rawName.size() : 0);
    Record row{store(file.name), store(file.rawName), file.id,        file.original,
               file.offset,      file.size,           file.directory, source(file.data, file.offset, file.size)};
    if (memory)
        memory->records.push_back(row);
    else {
        load(count);
        page->records[count - page->first] = row;
        page->dirty = true;
    }
    count++;
}

void FileTable::setId(uint32_t index, uint32_t id) {
    record(index);
    if (memory) {
        memory->records[index].id = id;
        return;
    }
    page->records[index - page->first].id = id;
    page->dirty = true;
}

void FileTable::setData(uint32_t index, SourcePtr data) {
    auto row = record(index);
    uint32_t value = source(data, row.offset, row.size);
    if (memory) {
        memory->records[index].source = value;
        return;
    }
    page->records[index - page->first].source = value;
    page->dirty = true;
}

uint32_t FileTable::find(const std::string& value) {
    std::string key = normalize(value);
    for (uint32_t index = 0; index < count; index++)
        if (normalize(name(index)) == key)
            return index;
    return UINT32_MAX;
}

void FileTable::clear() {
    memory.reset();
    pages.reset();
    page = nullptr;
    names.reset();
    records.reset();
    text.reset();
    sources.clear();
    count = 0;
}

}
