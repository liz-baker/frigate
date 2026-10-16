#include "extract/Inflate.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace Frigate::Extract {

Error InflateRaw(std::span<const uint8_t> data, std::vector<uint8_t>& out, size_t expectedSize) {
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -15) != Z_OK) {
        return Error::Make(ErrorCode::Inflate, "Could not start decompression.");
    }

    out.clear();
    out.resize(expectedSize != 0 ? expectedSize : std::max<size_t>(data.size() * 4, 4096));
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = uInt(data.size());

    int status = Z_OK;
    size_t produced = 0;
    for (;;) {
        stream.next_out = out.data() + produced;
        stream.avail_out = uInt(out.size() - produced);
        status = inflate(&stream, Z_NO_FLUSH);
        produced = out.size() - stream.avail_out;
        if (status == Z_STREAM_END) {
            break;
        }
        if (status == Z_OK || status == Z_BUF_ERROR) {
            if (stream.avail_out == 0) {
                // A stream longer than the size the caller expected is corrupt; with no expected size, grow.
                if (expectedSize != 0) {
                    // The end-of-block code may not have been consumed yet; only a further byte is an overrun.
                    uint8_t extra;
                    stream.next_out = &extra;
                    stream.avail_out = 1;
                    status = inflate(&stream, Z_NO_FLUSH);
                    if (status == Z_STREAM_END && stream.avail_out == 1) {
                        break;
                    }
                    status = Z_DATA_ERROR;
                    break;
                }
                out.resize(out.size() * 2);
                continue;
            }
            if (stream.avail_in == 0) {
                break; // truncated
            }
            continue;
        }
        break;
    }
    inflateEnd(&stream);

    if (status != Z_STREAM_END) {
        out.clear();
        return Error::Make(ErrorCode::Inflate, "Compressed data in the ROM is damaged or cut short.");
    }
    out.resize(produced);
    if (expectedSize != 0 && produced != expectedSize) {
        out.clear();
        return Error::Make(ErrorCode::Corrupt, "Compressed data in the ROM inflated to an unexpected size.");
    }
    return {};
}

Error Inflate1172(std::span<const uint8_t> data, std::vector<uint8_t>& out, size_t expectedSize) {
    size_t header = 0;
    if (data.size() >= 2 && data[0] == 0x11 && data[1] == 0x72) {
        header = 2;
    } else if (data.size() >= 5 && data[0] == 0x11 && data[1] == 0x73) {
        header = 5;
    } else {
        return Error::Make(ErrorCode::Corrupt, "Expected compressed data in the ROM but found something else.");
    }
    return InflateRaw(data.subspan(header), out, expectedSize);
}

Error DeflateRaw(std::span<const uint8_t> data, std::vector<uint8_t>& out, int level) {
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (deflateInit2(&stream, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return Error::Make(ErrorCode::Archive, "Could not start compression.");
    }
    out.resize(deflateBound(&stream, uLong(data.size())));
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = uInt(data.size());
    stream.next_out = out.data();
    stream.avail_out = uInt(out.size());
    int status = deflate(&stream, Z_FINISH);
    size_t produced = out.size() - stream.avail_out;
    deflateEnd(&stream);
    if (status != Z_STREAM_END) {
        out.clear();
        return Error::Make(ErrorCode::Archive, "Compression failed.");
    }
    out.resize(produced);
    return {};
}

uint32_t Crc32(std::span<const uint8_t> data) {
    return uint32_t(crc32(0, data.data(), uInt(data.size())));
}

} // namespace Frigate::Extract
