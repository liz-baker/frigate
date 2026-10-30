# ROM extraction (frigate issue #17): turns a ROM into the O2R archives on the device. Needs only zlib (the same
# deflate libultraship's libzip already depends on), so it builds without libultraship or the decomp.
find_package(ZLIB)
if(ZLIB_FOUND)
    add_library(frigate_extract STATIC
        src/extract/Sha1.cpp
        src/extract/Region.cpp
        src/extract/RomSearch.cpp
        src/extract/FirstRun.cpp
        src/extract/RomLayout.cpp
        src/extract/RomLayoutData.cpp
        src/extract/Inflate.cpp
        src/extract/O2rWriter.cpp
        src/extract/Extractor.cpp
        src/extract/ImageDefs.cpp
        src/extract/ImageDefData.cpp
        src/extract/Texture.cpp
        src/extract/TextureMip.cpp
        src/extract/DisplayListEmit.cpp
        src/extract/PackTextures.cpp
        src/extract/PackImages.cpp
        src/extract/PackSegments.cpp
        src/extract/TexturePackers.cpp
        src/extract/ModelContainer.cpp
        src/extract/ModelTree.cpp
        src/extract/ModelTables.cpp
        src/extract/SkeletonData.cpp
        src/extract/Stan.cpp
        src/extract/PackModels.cpp
        src/extract/PackAnimations.cpp
        src/extract/PackGeometry.cpp
        src/extract/AudioBank.cpp
        src/extract/AudioPack.cpp
        src/extract/AudioNamesData.cpp
        src/extract/ObjectTables.cpp
        src/extract/AnimationTableData.cpp
        src/extract/AnimationClipData.cpp
        src/extract/ChrCombatData.cpp
        src/extract/ChrRecordData.cpp
        src/extract/ModelHeaderData.cpp
        src/extract/MonitorListData.cpp
        src/extract/PropRecordData.cpp
        src/extract/RamromData.cpp
        src/extract/WeaponStatsData.cpp
    )
    target_include_directories(frigate_extract PUBLIC src)
    target_link_libraries(frigate_extract PUBLIC ZLIB::ZLIB)

    add_executable(frigate-extract src/extract/ExtractCli.cpp)
    target_link_libraries(frigate-extract PRIVATE frigate_extract)
elseif(FRIGATE_REQUIRE_GAME)
    message(FATAL_ERROR "zlib not found, so frigate_extract would be skipped.")
else()
    message(STATUS "zlib not found; skipping frigate_extract")
endif()
