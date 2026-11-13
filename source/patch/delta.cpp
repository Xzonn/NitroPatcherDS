#include "core.h"

#include <cstring>
#include <cstddef>
#include <cstdlib>
#ifdef ARM9
#include <nds.h>
#include <malloc.h>
#endif

extern "C" {
#include "xdelta3.h"
#include "miniz.h"
}

namespace np3d {

struct DeltaOutput {
    SourcePtr source;
    std::shared_ptr<Map> target;
    uint32_t adler = 1;
    uint32_t size = 0, buffered = 0, notified = 0;
    size_t budget = 0, allocated = 0;
    bool exhausted = false;
    std::string label;
    uint32_t chunk;
    std::vector<uint8_t> buffer, literals, history;
    std::unique_ptr<Cached> payload;

    explicit DeltaOutput(uint32_t bytes = BufferSize) : chunk(bytes), buffer(bytes), literals(bytes), history(bytes) {}

    void flush() {
        if (!buffered)
            return;
        target->payload->write(target->payload->size, literals.data(), buffered);
        buffered = 0;
    }

    void record(xd3_stream* stream, const uint8_t* data, uint32_t length) {
        if (stream->dec_win_ind & 4)
            adler = mz_adler32(adler, data, length);
        uint32_t first = std::min(length, chunk - size % chunk);
        std::memcpy(history.data() + size % chunk, data, first);
        std::memcpy(history.data(), data + first, length - first);
        size += length;
        if (size - notified >= BufferSize || size == stream->dec_winstart + stream->dec_tgtlen) {
            progress("Applying patch section", size - stream->dec_winstart, stream->dec_tgtlen, true, label.c_str());
            notified = size;
        }
    }

    void literal(xd3_stream* stream, const uint8_t* data, uint32_t length) {
        while (length) {
            uint32_t take = std::min(length, chunk - buffered);
            target->append(Kind::Patch, target->payload->size + buffered, take);
            std::memcpy(literals.data() + buffered, data, take);
            buffered += take;
            record(stream, data, take);
            if (buffered == chunk)
                flush();
            data += take;
            length -= take;
        }
    }

    void read(uint32_t offset, uint8_t* data, uint32_t length) {
        if (size - offset <= chunk) {
            uint32_t first = std::min(length, chunk - offset % chunk);
            std::memcpy(data, history.data() + offset % chunk, first);
            std::memcpy(data + first, history.data(), length - first);
        } else {
            while (length) {
                Extent item = target->find(offset);
                uint32_t take = std::min(length, item.start + item.length - offset);
                uint32_t position = item.source + offset - item.start;
                if (item.kind == Kind::Patch && position >= target->payload->size) {
                    std::memcpy(data, literals.data() + position - target->payload->size, take);
                } else if (item.kind == Kind::Patch) {
                    take = std::min(take, target->payload->size - position);
                    payload->read(position, data, take);
                } else {
                    target->read(offset, data, take);
                }
                offset += take;
                data += take;
                length -= take;
            }
        }
    }
};

static int emit(xd3_stream* stream, const xd3_hinst* instruction, const uint8_t* data) {
    auto& state = *static_cast<DeltaOutput*>(stream->opaque);
    require(instruction->size <= 0x40000000 && instruction->addr <= 0x40000000,
            "VCDIFF instruction exceeds ROM size limit");
    uint32_t length = instruction->size;
    if (instruction->type == XD3_ADD)
        state.literal(stream, data, length);
    else if (instruction->type == XD3_RUN) {
        bool materialize = length < 0x4000;
        if (!materialize)
            state.target->append(Kind::Fill, *data, length);
        std::memset(state.buffer.data(), *data, std::min(length, state.chunk));
        while (length) {
            uint32_t take = std::min(length, state.chunk);
            if (materialize)
                state.literal(stream, state.buffer.data(), take);
            else
                state.record(stream, state.buffer.data(), take);
            length -= take;
        }
    } else {
        uint32_t address = instruction->addr;
        if (address < stream->dec_cpylen) {
            require(stream->dec_cpyoff <= 0x40000000, "VCDIFF source offset exceeds ROM size limit");
            SourcePtr input = (stream->dec_win_ind & 2) ? state.target : state.source;
            require(stream->dec_cpyoff <= input->size && stream->dec_cpylen <= input->size - stream->dec_cpyoff,
                    "VCDIFF source window exceeds source file");
            uint32_t take = std::min<uint32_t>(length, stream->dec_cpylen - address);
            bool materialize = take < 0x4000;
            if (!materialize)
                state.target->copy(input, stream->dec_cpyoff + address, take);
            length -= take;
            uint32_t offset = stream->dec_cpyoff + address;
            while (take) {
                uint32_t bytes = std::min(take, state.chunk);
                if (input == state.target)
                    state.read(offset, state.buffer.data(), bytes);
                else
                    input->read(offset, state.buffer.data(), bytes);
                if (materialize)
                    state.literal(stream, state.buffer.data(), bytes);
                else
                    state.record(stream, state.buffer.data(), bytes);
                offset += bytes;
                take -= bytes;
            }
            address = 0;
        } else
            address -= stream->dec_cpylen;
        if (length) {
            uint32_t offset = stream->dec_winstart + address;
            require(offset < state.size, "VCDIFF target copy references unwritten data");
            uint32_t distance = state.size - offset;
            while (length) {
                uint32_t take = std::min(length, state.chunk);
                uint32_t filled = std::min(take, distance);
                state.read(offset, state.buffer.data(), filled);
                while (filled < take) {
                    uint32_t bytes = std::min(filled, take - filled);
                    std::memcpy(state.buffer.data() + filled, state.buffer.data(), bytes);
                    filled += bytes;
                }
                state.literal(stream, state.buffer.data(), take);
                offset += take;
                length -= take;
            }
        }
    }
    return 0;
}

static uint32_t checksum(xd3_stream* stream) { return static_cast<DeltaOutput*>(stream->opaque)->adler; }

struct alignas(std::max_align_t) DeltaAllocation {
    size_t length;
};

static void* allocate(void* opaque, size_t items, usize_t size) {
    auto& state = *static_cast<DeltaOutput*>(opaque);
    if (items > (SIZE_MAX - sizeof(DeltaAllocation)) / std::max<usize_t>(size, 1)) {
        state.exhausted = true;
        return nullptr;
    }
    size_t length = items * size + sizeof(DeltaAllocation);
    if (length > state.budget - state.allocated) {
        state.exhausted = true;
        return nullptr;
    }
    auto allocation = static_cast<DeltaAllocation*>(std::malloc(length));
    if (!allocation) {
        state.exhausted = true;
        return nullptr;
    }
    allocation->length = length;
    state.allocated += length;
    return allocation + 1;
}

static void release(void* opaque, void* address) {
    if (!address)
        return;
    auto& state = *static_cast<DeltaOutput*>(opaque);
    auto allocation = static_cast<DeltaAllocation*>(address) - 1;
    state.allocated -= allocation->length;
    std::free(allocation);
}

std::shared_ptr<Map> applyDelta(Workspace& work, SourcePtr source, const std::string& path, const std::string& label) {
    return applyDelta(work, source, std::make_shared<Disk>(path), label);
}

std::shared_ptr<Map> applyDelta(Workspace& work, SourcePtr source, SourcePtr input, const std::string& label) {
    logPreparation("DELTA begin name=" + label + " sourceSize=" + std::to_string(source->size));
    uint32_t chunk = work.dsOnly ? 16384 : BufferSize;
#ifdef ARM9
    if (!isDSiMode())
        chunk = 16384;
#endif
    DeltaOutput output(chunk);
    output.source = source;
    output.label = label;
    output.target = work.map();
    output.payload = std::make_unique<Cached>(output.target->payload);
#ifdef ARM9
    size_t available =
        reinterpret_cast<uintptr_t>(getHeapLimit()) - reinterpret_cast<uintptr_t>(getHeapEnd()) + mallinfo().fordblks;
    output.budget = available > BufferSize ? available - BufferSize : 0;
#else
    output.budget = 8 * 1024 * 1024;
#endif
    xd3_stream stream = {};
    std::unique_ptr<xd3_stream, decltype(&xd3_free_stream)> cleanup(&stream, xd3_free_stream);
    xd3_config config;
    xd3_init_config(&config, 0);
    config.opaque = &output;
    config.alloc = allocate;
    config.freef = release;
    require(xd3_config_stream(&stream, &config) == 0, "Cannot initialize xdelta3");
    stream.external_emit = emit;
    stream.external_adler32 = checksum;
    auto reject = [&](const char* message) -> std::shared_ptr<Map> {
        work.error = std::string("Cannot decode VCDIFF: ") +
                     (output.exhausted ? "Decoder memory budget exceeded" : message) + " | " + label;
        return nullptr;
    };
    xd3_source original = {};
    original.blksize = chunk;
    require(xd3_set_source_and_size(&stream, &original, source->size) == 0, "Cannot configure VCDIFF source");
    std::vector<uint8_t> buffer(chunk);
    uint32_t offset = 0;
    while (true) {
        NP3D_PREPARE_LOG("DELTA decode begin inputOffset=" + std::to_string(offset));
        int status = xd3_decode_input(&stream);
        NP3D_PREPARE_LOG("DELTA decode done status=" + std::to_string(status) +
                         " outputSize=" + std::to_string(output.target->size));
        if (status == XD3_INPUT) {
            if (offset == input->size) {
                if (xd3_close_stream(&stream) != 0)
                    return reject(stream.msg ? stream.msg : "Incomplete VCDIFF stream");
                break;
            }
            uint32_t take = std::min(chunk, input->size - offset);
            NP3D_PREPARE_LOG("DELTA read begin offset=" + std::to_string(offset) + " bytes=" + std::to_string(take));
            input->read(offset, buffer.data(), take);
            NP3D_PREPARE_LOG("DELTA read done offset=" + std::to_string(offset));
            offset += take;
            xd3_avail_input(&stream, buffer.data(), take);
            if (offset == input->size)
                xd3_set_flags(&stream, stream.flags | XD3_FLUSH);
        } else if (status == XD3_OUTPUT) {
            xd3_consume_output(&stream);
        } else if (status == XD3_GOTHEADER || status == XD3_WINSTART) {
            output.adler = 1;
            output.notified = output.size;
            progress("Applying patch section", 0, stream.dec_tgtlen, true, label.c_str());
        } else if (status != XD3_WINFINISH) {
            return reject(stream.msg ? stream.msg : "invalid stream");
        }
    }
    if (!stream.dec_window_count)
        return reject("VCDIFF contains no target windows");
    cleanup.reset();
    output.flush();
    output.target->finish();
    logPreparation("DELTA done name=" + label + " outputSize=" + std::to_string(output.target->size));
    return output.target;
}

}
