#include "platform/Boot.h"

#include <algorithm>
#include <thread>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "fast/Fast3dWindow.h"
#include "fast/ucodehandlers.h"
#include "libultraship/controller/controldeck/ControlDeck.h"
#include "ship/audio/Audio.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/debug/Console.h"
#include "ship/debug/CrashHandler.h"
#include "ship/events/Events.h"
#include "ship/log/Logger.h"
#include "ship/resource/ResourceManager.h"
#include "ship/thread/ThreadPool.h"
#include "ship/window/FileDrop.h"

namespace Frigate::Platform {

std::shared_ptr<Ship::Context> CreateContext(const std::string& name, const std::string& shortName,
                                             const std::string& configFilePath) {
    auto context = Ship::Context::CreateInstance(name, shortName);
    if (context == nullptr) {
        return nullptr;
    }

    try {
        auto logPath = Ship::Context::GetPathRelativeToAppDirectory("logs/" + name + ".log");
        auto logger = std::make_shared<Ship::Logger>(name, logPath);
        context->GetChildren().Add(logger);
        logger->Init();
    } catch (const std::exception& ex) {
        spdlog::error("Frigate::Platform::CreateContext: log initialization failed: {}", ex.what());
        return nullptr;
    }

    // Config's constructor caches whatever Window it's given, so window has to
    // exist before config even though it's added to the Context later.
    auto window = std::make_shared<Fast::Fast3dWindow>();

    auto config = std::make_shared<Ship::Config>(Ship::Context::GetPathRelativeToAppDirectory(configFilePath), window);
    context->GetChildren().Add(config);

    auto consoleVariable = std::make_shared<Ship::ConsoleVariable>(config);
    context->GetChildren().Add(consoleVariable);

    auto hardwareThreads = std::thread::hardware_concurrency();
    size_t threadCount = hardwareThreads > 1 ? static_cast<size_t>(hardwareThreads - 1) : 1;
    context->GetChildren().Add(std::make_shared<Ship::ThreadPool>(threadCount));

    auto resourceManager = std::make_shared<Ship::ResourceManager>(context->GetChildren().GetFirst<Ship::ThreadPool>());
    context->GetChildren().Add(resourceManager);

    auto controlDeck = std::make_shared<LUS::ControlDeck>(window, consoleVariable);
    context->GetChildren().Add(controlDeck);

    context->GetChildren().Add(std::make_shared<Ship::CrashHandler>());

    auto console = std::make_shared<Ship::Console>();
    context->GetChildren().Add(console);

    context->GetChildren().Add(window);

    auto audio = std::make_shared<Ship::Audio>(Ship::AudioSettings{}, config);
    context->GetChildren().Add(audio);

    context->GetChildren().Add(std::make_shared<Ship::Events>());

    auto fileDropMgr = std::make_shared<Ship::FileDrop>(window);
    context->GetChildren().Add(fileDropMgr);

    try {
        nlohmann::json resourceManagerArgs;
        resourceManagerArgs["archivePaths"] = std::vector<std::string>{};
        resourceManagerArgs["validHashes"] = std::vector<uint32_t>{};
        resourceManager->Init(resourceManagerArgs);
    } catch (const std::exception& ex) {
        spdlog::error("Frigate::Platform::CreateContext: ResourceManager init failed: {}", ex.what());
        return nullptr;
    }

    console->Init();
    window->Init();
    // UcodeHandlers is declared at global scope, not inside Fast::.
    window->SetRendererUCode(::ucode_indy_ge);
    audio->Init();
    fileDropMgr->Init();

    return context;
}

} // namespace Frigate::Platform
