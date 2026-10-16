// Unit tests for the extraction library core. Everything runs without a ROM except the checks that call
// FindUsRom(), which skip unless FRIGATE_ROMS (a path-separator list of ROM files and directories) points at
// the US ROM.

#include "extract/Extractor.h"
#include "extract/Inflate.h"
#include "extract/O2rWriter.h"
#include "extract/Region.h"
#include "extract/RomLayout.h"
#include "extract/Sha1.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

using namespace Frigate::Extract;
namespace fs = std::filesystem;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

std::vector<uint8_t> FromHex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(uint8_t(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

// Raw DEFLATE streams made by Python's zlib (compressobj, wbits=-15), with the inputs they were made from below.
const char* kTextDeflate = "73cfcf4949cd73ad4c5530303057701fe58df2f0f200";
const char* kPatternDeflate =
    "6360e7139551d632b4b077f30d8949ce2aaca86feb9d327bd1ca0d3bf61f3b7be5f6a3971fbeff63e5119156d23430b7"
    "73f5098e4acc2828af6bed993c6be18af5dbf61e3973f9d6c317efbffd65e1169254d0d037b375f10e8a4c48cf2bad69"
    "e99e3473c1f2755bf71c3e75f1c683e7efbefe61e612949057d335b171f60a8c884fcb2da96eea9c3063feb2b55b761f"
    "3a79e1fabda76fbefc66e214109753d531b672f408088f4bcd29ae6aece89f3677c99acdbb0e9e387fedee93d79f7e32"
    "70f08bc9aa681b593ab8fb85c6a464175536b4f74d9db378d5c61d078e9fbb7ae7f1ab8f3ffeb3f18ac03d169d945900"
    "f7d8f67d47cfc03d262ca5a801f7587e596d0bdc63a72fdd7c00f798ba9ea98d0b00";

std::vector<uint8_t> TextInput() {
    std::string unit = "GoldenEye 007 ";
    std::vector<uint8_t> out;
    for (int i = 0; i < 40; i++) {
        out.insert(out.end(), unit.begin(), unit.end());
    }
    return out;
}

std::vector<uint8_t> PatternInput() {
    std::vector<uint8_t> out;
    for (int i = 0; i < 300; i++) {
        out.push_back(uint8_t(i * 7 + i / 13));
    }
    return out;
}

fs::path ScratchDir(const char* name) {
    fs::path dir = fs::temp_directory_path() / (std::string("frigate_extract_test_") + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::optional<fs::path> FindUsRom() {
    const char* env = std::getenv("FRIGATE_ROMS");
    if (env == nullptr) {
        return std::nullopt;
    }
    std::string list = env;
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(separator, start);
        if (end == std::string::npos) {
            end = list.size();
        }
        fs::path location = list.substr(start, end - start);
        start = end + 1;
        std::error_code ec;
        if (fs::is_regular_file(location, ec)) {
            if (IsUsRom(location)) {
                return location;
            }
        } else if (fs::is_directory(location, ec)) {
            for (const auto& entry : fs::directory_iterator(location, ec)) {
                if (entry.is_regular_file() && IsUsRom(entry.path())) {
                    return entry.path();
                }
            }
        }
    }
    return std::nullopt;
}

// Reads every member of a zip written by O2rWriter, checking CRCs. Enough of the format for these tests.
std::map<std::string, std::vector<uint8_t>> ReadZip(const fs::path& path, bool& ok) {
    std::map<std::string, std::vector<uint8_t>> members;
    ok = false;
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t at) { return uint32_t(data[at]) | (uint32_t(data[at + 1]) << 8); };
    auto u32 = [&](size_t at) { return u16(at) | (u16(at + 2) << 16); };
    if (data.size() < 22 || u32(data.size() - 22) != 0x06054B50) {
        return members;
    }
    size_t eocd = data.size() - 22;
    size_t count = u16(eocd + 10);
    size_t at = u32(eocd + 16);
    for (size_t i = 0; i < count; i++) {
        if (u32(at) != 0x02014B50) {
            return members;
        }
        uint32_t method = u16(at + 10), crc = u32(at + 16), csize = u32(at + 20), size = u32(at + 24);
        size_t nameLength = u16(at + 28), extraLength = u16(at + 30), commentLength = u16(at + 32);
        size_t local = u32(at + 42);
        std::string name(data.begin() + at + 46, data.begin() + at + 46 + nameLength);
        at += 46 + nameLength + extraLength + commentLength;

        size_t bodyAt = local + 30 + u16(local + 26) + u16(local + 28);
        std::span<const uint8_t> body(data.data() + bodyAt, csize);
        std::vector<uint8_t> bytes;
        if (method == 8) {
            if (!InflateRaw(body, bytes, size).ok() && size != 0) {
                return members;
            }
        } else {
            bytes.assign(body.begin(), body.end());
        }
        if (bytes.size() != size || Crc32(bytes) != crc) {
            return members;
        }
        members[name] = bytes;
    }
    ok = true;
    return members;
}

void TestSha1() {
    auto hex = [](const std::string& s) { return Sha1::ToHex(Sha1::Of(s.data(), s.size())); };
    Check(hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1 of abc");
    Check(hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1 of empty input");
    Check(hex(std::string(1000, 'a')) == "291e9a6c66994949b57ba5e650361e98fc36b1ba", "sha1 across blocks");

    // Splitting the input at awkward points must not change the digest.
    std::string text(1000, 'a');
    Sha1 split;
    split.Update(text.data(), 3);
    split.Update(text.data() + 3, 61);
    split.Update(text.data() + 64, 936);
    Check(Sha1::ToHex(split.Finish()) == "291e9a6c66994949b57ba5e650361e98fc36b1ba", "sha1 incremental");
}

void TestInflate() {
    std::vector<uint8_t> out;
    Check(InflateRaw(FromHex(kTextDeflate), out).ok() && out == TextInput(), "inflate python text vector");
    Check(InflateRaw(FromHex(kPatternDeflate), out, 300).ok() && out == PatternInput(),
          "inflate python pattern vector with expected size");
    Check(!InflateRaw(FromHex(kPatternDeflate), out, 299).ok(), "inflate rejects a stream longer than expected");
    Check(InflateRaw(FromHex(kPatternDeflate), out, 301).code == ErrorCode::Corrupt,
          "inflate rejects a stream shorter than expected");

    std::vector<uint8_t> truncated = FromHex(kPatternDeflate);
    truncated.resize(truncated.size() / 2);
    Check(InflateRaw(truncated, out).code == ErrorCode::Inflate, "inflate rejects truncated data");

    std::vector<uint8_t> framed = { 0x11, 0x72 };
    std::vector<uint8_t> body = FromHex(kTextDeflate);
    framed.insert(framed.end(), body.begin(), body.end());
    Check(Inflate1172(framed, out, 560).ok() && out == TextInput(), "1172 header then raw deflate");

    std::vector<uint8_t> framed73 = { 0x11, 0x73, 0, 0, 0 };
    framed73.insert(framed73.end(), body.begin(), body.end());
    Check(Inflate1172(framed73, out).ok() && out == TextInput(), "1173 five-byte header");

    framed[1] = 0x00;
    Check(Inflate1172(framed, out).code == ErrorCode::Corrupt, "1172 rejects another header");

    // Compression and decompression agree.
    std::vector<uint8_t> packed;
    Check(DeflateRaw(PatternInput(), packed).ok() && InflateRaw(packed, out).ok() && out == PatternInput(),
          "deflate then inflate");
}

void TestResourceHeader() {
    Check(ResourceTypeCode("OBLB") == kResourceTypeBlob, "OBLB fourcc");
    Check(ResourceTypeCode("TOO_LONG") == 0, "wrong-length fourcc");

    ResourceHeader header;
    header.type = ResourceTypeCode("OTEX");
    header.version = 3;
    header.id = 0x0102030405060708ull;
    std::vector<uint8_t> bytes = PackResourceHeader(header);
    Check(bytes.size() == 64, "header is 64 bytes");
    const uint8_t expected[] = { 1, 0, 0, 0, 'O', 'T', 'E', 'X', 0, 0, 0, 3, 1, 2, 3, 4, 5, 6, 7, 8 };
    Check(std::equal(std::begin(expected), std::end(expected), bytes.begin()), "big-endian header fields");
    Check(std::all_of(bytes.begin() + 20, bytes.end(), [](uint8_t b) { return b == 0; }), "header padding is zero");

    ResourceHeader back;
    Check(UnpackResourceHeader(bytes, back) && back.type == header.type && back.version == 3 && back.id == header.id &&
              back.byteOrder == Endianness::Big && !back.isCustom,
          "header round trip");

    header.byteOrder = Endianness::Little;
    header.isCustom = true;
    bytes = PackResourceHeader(header);
    Check(bytes[0] == 0 && bytes[1] == 1 && bytes[4] == 'X' && bytes[8] == 3 && bytes[12] == 8,
          "little-endian header fields");
    Check(UnpackResourceHeader(bytes, back) && back.isCustom && back.id == header.id && back.type == header.type,
          "little-endian header round trip");
}

void TestWriterRoundTrip() {
    fs::path dir = ScratchDir("writer");
    fs::path path = dir / "test.o2r";
    std::vector<uint8_t> big(100000);
    for (size_t i = 0; i < big.size(); i++) {
        big[i] = uint8_t(i % 251);
    }
    std::vector<uint8_t> noise(64);
    for (size_t i = 0; i < noise.size(); i++) {
        noise[i] = uint8_t(i * 197 + 13);
    }

    O2rWriter writer;
    Check(writer.Open(path).ok(), "writer opens");
    Check(writer.AddResource("blob/big", "OBLB", 0, 42, big).ok(), "add blob");
    Check(writer.AddResource("tex/small", "OTEX", 1, 7, noise).ok(), "add non-blob");
    Check(writer.AddRawFile("raw/empty", {}).ok(), "add empty raw file");
    Check(!writer.AddResource("x", "TOO_LONG", 0, 0, noise).ok(), "writer rejects bad fourcc");
    Check(writer.Close().ok(), "writer closes");
    Check(writer.MemberCount() == 3, "member count");

    bool ok = false;
    auto members = ReadZip(path, ok);
    Check(ok && members.size() == 3, "archive reads back");

    const std::vector<uint8_t>& blob = members["blob/big"];
    ResourceHeader header;
    Check(UnpackResourceHeader(blob, header) && header.type == kResourceTypeBlob && header.id == 42, "blob header");
    Check(blob.size() == 64 + 4 + big.size(), "blob length prefix added");
    Check(blob[64] == 0 && blob[65] == 1 && blob[66] == 0x86 && blob[67] == 0xA0, "blob prefix is big-endian 100000");
    Check(std::equal(big.begin(), big.end(), blob.begin() + 68), "blob payload");

    const std::vector<uint8_t>& tex = members["tex/small"];
    Check(tex.size() == 64 + noise.size() && std::equal(noise.begin(), noise.end(), tex.begin() + 64),
          "non-blob payload follows header unprefixed");
    Check(members["raw/empty"].empty(), "empty member");
}

void TestDetection() {
    Check(GetRegionInfo(Region::Us).imageShift == 0 && GetRegionInfo(Region::Eur).imageShift == -1456 &&
              GetRegionInfo(Region::Jp).imageShift == 4704,
          "image shifts");
    Check(std::string(GetRegionInfo(Region::Us).sha1) == "abe01e4aeb033b6c0836819f549c791b26cfde83", "us hash");
    Check(std::string(GetRegionInfo(Region::Eur).sha1) == "167c3c433dec1f1eb921736f7d53fac8cb45ee31", "eur hash");
    Check(std::string(GetRegionInfo(Region::Jp).sha1) == "2a5dade32f7fad6c73c659d2026994632c1b3174", "jp hash");

    fs::path dir = ScratchDir("detect");
    Error error;

    std::vector<uint8_t> wrongSize(1000, 0);
    Check(!DetectRegion(wrongSize, &error) && error.code == ErrorCode::RomSize, "wrong size rejected");

    std::vector<uint8_t> rom(kRomSize, 0);
    Check(!DetectRegion(rom, &error) && error.code == ErrorCode::RomUnrecognized, "unknown contents rejected");
    Check(!error.message.empty(), "error has a message");

    rom[0] = 0x37;
    rom[1] = 0x80;
    rom[2] = 0x40;
    rom[3] = 0x12;
    Check(!DetectRegion(rom, &error) && error.code == ErrorCode::RomByteOrder, "v64 dump named as such");

    fs::path file = dir / "fake.z64";
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(rom.data()), std::streamsize(rom.size()));
    }
    Check(!IsUsRom(file), "fake rom is not US");
    Check(!DetectRegionOfFile(file, &error) && error.code == ErrorCode::RomByteOrder,
          "file detection reports byte order");
    Check(!IsUsRom(dir / "missing.z64"), "missing file is not US");

    if (std::optional<fs::path> real = FindUsRom()) {
        Check(IsUsRom(*real), "real US rom is recognised");
        Check(DetectRegionOfFile(*real) == Region::Us, "real US rom detected as US");
        Check(!IsRegionRom(*real, Region::Eur), "US rom is not EUR");
    } else {
        std::printf("skip: no US ROM (set FRIGATE_ROMS)\n");
    }
}

void TestLayoutData() {
    for (const RegionInfo& info : AllRegions()) {
        const RomLayout& layout = GetRomLayout(info.region);
        Check(layout.region == info.region, "layout region");
        Check(!layout.files.empty(), "layout has files");
        for (const FileEntry& file : layout.files) {
            if (uint64_t(file.offset) + file.size > kRomSize) {
                std::fprintf(stderr, "file out of bounds: %s\n", file.name);
                Check(false, "file entries lie inside the ROM");
                break;
            }
        }
    }
    Check(GetRomLayout(Region::Us).files.size() == 812, "us file count");

    const FileEntry* bg = GetRomLayout(Region::Us).FindFile("assets/obseg/bg/bg_sev_all_p.bin");
    Check(bg != nullptr && bg->offset == 4425312 && bg->size == 69104 && !bg->compressed && !bg->extract,
          "known us file entry");
    Check(GetRomLayout(Region::Us).FindFileAt(4425312 + 100) == bg, "FindFileAt");
    Check(GetRomLayout(Region::Us).FindFile("nope") == nullptr, "FindFile miss");

    std::span<const ImageEntry> images = ImageTable();
    Check(images.size() == 2699, "image count");
    Check(images[0].offset == 9403888 && images[0].size == 1876, "first image");
    for (const RegionInfo& info : AllRegions()) {
        for (size_t i = 0; i < images.size(); i++) {
            if (uint64_t(ImageRomOffset(info.region, i)) + images[i].size > kRomSize) {
                Check(false, "image entries lie inside the ROM");
                break;
            }
        }
    }
    Check(ImageRomOffset(Region::Eur, 0) == 9403888 - 1456 && ImageRomOffset(Region::Jp, 0) == 9403888 + 4704,
          "image shift applied");

    const RegionConstants& us = GetRomLayout(Region::Us).constants;
    Check(us.known && us.cdataRomOffset == 137616 && us.rarewareLogoRomStart == 0x29E560, "us constants");
    Check(!GetRomLayout(Region::Eur).constants.known, "eur constants not worked out");
}

void TestLayoutAgainstRom() {
    std::optional<fs::path> path = FindUsRom();
    if (!path) {
        std::printf("skip: no US ROM (set FRIGATE_ROMS)\n");
        return;
    }
    std::ifstream file(*path, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    StageContext context(Region::Us, rom, nullptr, nullptr);

    // Every compressed entry of the file list inflates.
    int inflated = 0;
    for (const FileEntry& entry : context.layout().files) {
        if (!entry.compressed) {
            continue;
        }
        std::vector<uint8_t> out;
        Error error = context.ReadFile(entry, out);
        if (!error.ok()) {
            std::fprintf(stderr, "%s: %s\n", entry.name, error.message.c_str());
            Check(false, "compressed file list entries inflate");
            break;
        }
        inflated++;
    }
    Check(inflated > 500, "inflated the compressed file list entries");

    // The packers' fixed constants point at the headers and files they expect.
    const RegionConstants& us = context.layout().constants;
    std::span<const uint8_t> cdata;
    std::vector<uint8_t> out;
    Check(context.RomRange(us.cdataRomOffset, us.cdataRomSize, cdata).ok() && Inflate1172(cdata, out).ok(),
          "cdata segment inflates");
    const FileEntry* sight = context.layout().FindFileAt(us.eyeIntroSightRomStart);
    Check(sight != nullptr && sight->offset == us.eyeIntroSightRomStart && sight->size == us.eyeIntroSightRomSize,
          "eye intro backdrop is a file list entry");
    Check(context.RomRange(us.eyeIntroGameSegmentRomStart, 2, cdata).ok() && cdata[0] == 0x11 && cdata[1] == 0x72,
          "game segment starts with a 1172 header");
}

void TestExtractorPipeline() {
    // Detection needs a real dump, so without a ROM only the failure paths can run.
    fs::path dir = ScratchDir("pipeline");
    Extractor extractor;
    RegisterDefaultStages(extractor);

    Options options;
    options.outputDir = dir / "out";
    Check(extractor.Run(options).error.code == ErrorCode::InvalidArgument, "missing arguments");

    options.romPath = dir / "absent.z64";
    Check(extractor.Run(options).error.code == ErrorCode::FileOpen, "missing ROM");

    options.romPath = dir / "short.z64";
    {
        std::ofstream out(options.romPath, std::ios::binary);
        out << "not a rom";
    }
    Result result = extractor.Run(options);
    Check(result.error.code == ErrorCode::RomSize && !result.error.message.empty(), "short ROM");

    std::optional<fs::path> rom = FindUsRom();
    if (!rom) {
        std::printf("skip: no US ROM (set FRIGATE_ROMS)\n");
        return;
    }
    options.romPath = *rom;

    options.region = Region::Eur;
    Check(extractor.Run(options).error.code == ErrorCode::RegionMismatch, "region mismatch");
    options.region = Region::Us;

    double lastFraction = -1;
    bool monotonic = true;
    options.progress = [&](const Progress& progress) {
        monotonic = monotonic && progress.fraction >= lastFraction;
        lastFraction = progress.fraction;
    };
    result = extractor.Run(options);
    Check(result.error.ok() && result.region == Region::Us && result.archives.size() == 4, "default run succeeds");
    Check(lastFraction == 1.0 && monotonic, "progress reaches 1 monotonically");
    for (const char* name :
         { "goldeneye_textures.o2r", "goldeneye_objects.o2r", "goldeneye_geometry.o2r", "goldeneye_audio.o2r" }) {
        bool ok = false;
        ReadZip(options.outputDir / name, ok);
        Check(ok, "archive exists and is a valid zip");
    }
    Check(!fs::exists(options.outputDir / "goldeneye_audio.o2r.tmp"), "no temporary left behind");

    // A registered stage writes into its archive, and a failure removes everything.
    Extractor custom;
    custom.RegisterStage(MakeStage("Probe", ArchiveId::Geometry, [](StageContext& context, O2rWriter& writer) {
        std::span<const uint8_t> header;
        if (Error error = context.RomRange(0, 4, header); !error.ok()) {
            return error;
        }
        context.Report(1, 2, "probe");
        return writer.AddResource("probe/header", "OBLB", 0, 1, header);
    }));
    fs::path customDir = dir / "custom";
    options.outputDir = customDir;
    options.progress = nullptr;
    result = custom.Run(options);
    Check(result.error.ok() && result.archives.size() == 1 && result.archives[0].filename() == "goldeneye_geometry.o2r",
          "custom stage produces only its archive");
    bool ok = false;
    auto members = ReadZip(customDir / "goldeneye_geometry.o2r", ok);
    Check(ok && members["probe/header"].size() == 64 + 4 + 4 && members["probe/header"][68] == 0x80 &&
              members["probe/header"][69] == 0x37,
          "stage output is the ROM's first bytes");

    Extractor failing;
    failing.RegisterStage(MakeStage("Ok", ArchiveId::Textures, [](StageContext&, O2rWriter&) { return Error{}; }));
    failing.RegisterStage(MakeStage("Bad", ArchiveId::Objects,
                                    [](StageContext&, O2rWriter&) { return Error::Make(ErrorCode::Stage, "boom"); }));
    options.outputDir = dir / "failing";
    result = failing.Run(options);
    Check(result.error.code == ErrorCode::Stage && result.error.message == "boom", "stage error propagates");
    Check(fs::is_empty(options.outputDir), "failed run removes its archives");

    std::atomic<bool> cancel{ false };
    options.cancel = &cancel;
    options.outputDir = dir / "cancelled";
    options.progress = [&](const Progress& progress) {
        if (progress.stageName == "Objects") {
            cancel = true;
        }
    };
    result = extractor.Run(options);
    Check(result.error.code == ErrorCode::Cancelled, "cancellation");
    Check(fs::is_empty(options.outputDir), "cancelled run removes its archives");
}

} // namespace

int main() {
    TestSha1();
    TestInflate();
    TestResourceHeader();
    TestWriterRoundTrip();
    TestDetection();
    TestLayoutData();
    TestLayoutAgainstRom();
    TestExtractorPipeline();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
