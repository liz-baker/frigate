#include "platform/Boot.h"

#include <algorithm>
#include <filesystem>
#include <thread>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "fast/Fast3dWindow.h"
#include "fast/resource/ResourceType.h"
#include "fast/resource/factory/TextureFactory.h"
#include "fast/resource/factory/VertexFactory.h"
#include "fast/ucodehandlers.h"
#include "libultraship/controller/controldeck/ControlDeck.h"
#include "resource/ResourceType.h"
#include "resource/factory/AnimationClipFactory.h"
#include "resource/factory/AudioBankFactory.h"
#include "resource/factory/DisplayListFactory.h"
#include "resource/factory/FontFactory.h"
#include "resource/factory/ModelSkeletonFactory.h"
#include "resource/factory/ModelTreeFactory.h"
#include "resource/factory/MusicSequencesFactory.h"
#include "resource/factory/StanFactory.h"
#include "resource/factory/TextFactory.h"
#include "ship/audio/Audio.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/debug/Console.h"
#include "ship/debug/CrashHandler.h"
#include "ship/events/Events.h"
#include "ship/log/Logger.h"
#include "ship/resource/File.h"
#include "ship/resource/ResourceLoader.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/factory/BlobFactory.h"
#include "ship/thread/ThreadPool.h"
#include "ship/window/FileDrop.h"

namespace Frigate::Platform {

std::shared_ptr<Ship::Context> CreateContext(const std::string& name, const std::string& shortName,
                                             const std::string& configFilePath,
                                             const std::vector<std::string>& archivePaths, bool withWindow) {
    auto context = Ship::Context::CreateInstance(name, shortName);
    if (context == nullptr) {
        return nullptr;
    }
    // Components (e.g. the Gui) refuse to use a Context that isn't marked
    // initialized, and nothing else in the Context's own setup does that.
    context->Init();

    try {
        auto logPath = Ship::Context::GetPathRelativeToAppDirectory("logs/" + name + ".log", shortName);
        // The log sink won't create missing directories on every target (the
        // Wii U SD card has none until first run), and the app directory
        // itself may not exist yet either.
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(logPath).parent_path(), ec);
        auto logger = std::make_shared<Ship::Logger>(name, logPath);
        context->GetChildren().Add(logger);
        logger->Init();
    } catch (const std::exception& ex) {
        spdlog::error("Frigate::Platform::CreateContext: log initialization failed: {}", ex.what());
        return nullptr;
    }

    // Config only stores the Window it's handed, but Fast3dWindow throws on
    // first use ("Window requires Config dependency") unless it is constructed
    // with Config and ConsoleVariable, so those come first and the window is
    // added to the Context later.
    auto config = std::make_shared<Ship::Config>(
        Ship::Context::GetPathRelativeToAppDirectory(configFilePath, shortName), nullptr);
    context->GetChildren().Add(config);

    auto consoleVariable = std::make_shared<Ship::ConsoleVariable>(config);
    context->GetChildren().Add(consoleVariable);

    std::shared_ptr<Fast::Fast3dWindow> window;
    if (withWindow) {
        window = std::make_shared<Fast::Fast3dWindow>(config, consoleVariable);
    }

    auto hardwareThreads = std::thread::hardware_concurrency();
    size_t threadCount = hardwareThreads > 1 ? static_cast<size_t>(hardwareThreads - 1) : 1;
    context->GetChildren().Add(std::make_shared<Ship::ThreadPool>(threadCount));

    auto resourceManager = std::make_shared<Ship::ResourceManager>(context->GetChildren().GetFirst<Ship::ThreadPool>());
    context->GetChildren().Add(resourceManager);

    if (withWindow) {
        auto controlDeck = std::make_shared<LUS::ControlDeck>(window, consoleVariable);
        context->GetChildren().Add(controlDeck);
    }

    context->GetChildren().Add(std::make_shared<Ship::CrashHandler>());

    auto console = std::make_shared<Ship::Console>();
    context->GetChildren().Add(console);

    if (withWindow) {
        context->GetChildren().Add(window);
    }

    auto audio = std::make_shared<Ship::Audio>(Ship::AudioSettings{}, config);
    context->GetChildren().Add(audio);

    context->GetChildren().Add(std::make_shared<Ship::Events>());

    std::shared_ptr<Ship::FileDrop> fileDropMgr;
    if (withWindow) {
        fileDropMgr = std::make_shared<Ship::FileDrop>(window);
        context->GetChildren().Add(fileDropMgr);
    }

    try {
        nlohmann::json resourceManagerArgs;
        resourceManagerArgs["archivePaths"] = archivePaths;
        resourceManagerArgs["validHashes"] = std::vector<uint32_t>{};
        resourceManager->Init(resourceManagerArgs);
    } catch (const std::exception& ex) {
        spdlog::error("Frigate::Platform::CreateContext: ResourceManager init failed: {}", ex.what());
        return nullptr;
    }

    // Register frigate's own GoldenEye-specific resource types (frigate issue
    // #11) -- libultraship only auto-registers Json/Shader, so any other
    // type (including these) has to be registered by the consumer.
    auto resourceLoader = resourceManager->GetResourceLoader();
    resourceLoader->RegisterResourceFactory(std::make_shared<Frigate::ResourceFactoryBinaryFontV0>(),
                                            RESOURCE_FORMAT_BINARY, "GEFont", Frigate::kFontResourceType, 0);
    resourceLoader->RegisterResourceFactory(std::make_shared<Frigate::ResourceFactoryBinaryTextV0>(),
                                            RESOURCE_FORMAT_BINARY, "GEText", Frigate::kTextResourceType, 0);
    resourceLoader->RegisterResourceFactory(
        std::make_shared<Frigate::ResourceFactoryBinaryDisplayListV0>(resourceManager, consoleVariable),
        RESOURCE_FORMAT_BINARY, "GEDisplayList", Frigate::kDisplayListResourceType, 0);
    resourceLoader->RegisterResourceFactory(std::make_shared<Frigate::ResourceFactoryBinaryModelTreeV8>(),
                                            RESOURCE_FORMAT_BINARY, "GEModelTree", Frigate::kModelTreeResourceType, 8);
    resourceLoader->RegisterResourceFactory(std::make_shared<Frigate::ResourceFactoryBinaryModelSkeletonV0>(),
                                            RESOURCE_FORMAT_BINARY, "GEModelSkeleton",
                                            Frigate::kModelSkeletonResourceType, 0);
    resourceLoader->RegisterResourceFactory(std::make_shared<Frigate::ResourceFactoryBinaryAnimationClipV1>(),
                                            RESOURCE_FORMAT_BINARY, "GEAnimationClip",
                                            Frigate::kAnimationClipResourceType, 1);
    resourceLoader->RegisterResourceFactory(std::make_shared<Frigate::ResourceFactoryBinaryStanV0>(),
                                            RESOURCE_FORMAT_BINARY, "GEStan", Frigate::kStanResourceType, 0);
    resourceLoader->RegisterResourceFactory(std::make_shared<Frigate::ResourceFactoryBinaryAudioBankV0>(),
                                            RESOURCE_FORMAT_BINARY, "GEAudioBank", Frigate::kAudioBankResourceType, 0);
    resourceLoader->RegisterResourceFactory(std::make_shared<Frigate::ResourceFactoryBinaryMusicSequencesV0>(),
                                            RESOURCE_FORMAT_BINARY, "GEMusicSequences",
                                            Frigate::kMusicSequencesResourceType, 0);

    // libultraship only auto-registers Json/Shader; frigate's own raw-byte
    // resources (the gun barrel screen's blood stream) are Blobs.
    resourceLoader->RegisterResourceFactory(std::make_shared<Ship::ResourceFactoryBinaryBlobV0>(),
                                            RESOURCE_FORMAT_BINARY, "Blob",
                                            static_cast<uint32_t>(Ship::ResourceType::Blob), 0);

    // Targets of the OTR-hash vertex/texture references in GEDL lists. The
    // packer writes OVTX at version 0 and OTEX at version 1 (see
    // docs/formats/o2r-resource-header.md).
    resourceLoader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(),
                                            RESOURCE_FORMAT_BINARY, "Vertex",
                                            static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    resourceLoader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(),
                                            RESOURCE_FORMAT_BINARY, "Texture",
                                            static_cast<uint32_t>(Fast::ResourceType::Texture), 1);

    console->Init();
    if (withWindow) {
        window->Init();
        // UcodeHandlers is declared at global scope, not inside Fast::.
        window->SetRendererUCode(::ucode_indy_ge);
    }
    audio->Init();
    if (withWindow) {
        fileDropMgr->Init();
    }

    return context;
}

} // namespace Frigate::Platform
