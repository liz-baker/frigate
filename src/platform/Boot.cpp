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

    // ---- Logging ----
    try {
        auto logPath = Ship::Context::GetPathRelativeToAppDirectory("logs/" + name + ".log");
        auto logger = std::make_shared<Ship::Logger>(name, logPath);
        context->GetChildren().Add(logger);
        logger->Init();
    } catch (const std::exception& ex) {
        spdlog::error("Frigate::Platform::CreateContext: log initialization failed: {}", ex.what());
        return nullptr;
    }

    // Constructed ahead of Config, matching Context::CreateDefaultInstance's own
    // contract that Config caches whatever Window it's given at construction time.
    auto window = std::make_shared<Fast::Fast3dWindow>();

    // ---- Configuration ----
    auto config = std::make_shared<Ship::Config>(Ship::Context::GetPathRelativeToAppDirectory(configFilePath), window);
    context->GetChildren().Add(config);

    // ---- Console Variables ----
    auto consoleVariable = std::make_shared<Ship::ConsoleVariable>(config);
    context->GetChildren().Add(consoleVariable);

    // ---- Thread Pool ----
    auto hardwareThreads = std::thread::hardware_concurrency();
    size_t threadCount = hardwareThreads > 1 ? static_cast<size_t>(hardwareThreads - 1) : 1;
    context->GetChildren().Add(std::make_shared<Ship::ThreadPool>(threadCount));

    // ---- Resource Manager ----
    // No archives yet: see Boot.h -- GoldenEye's O2R packer (frigate#10)
    // doesn't exist yet, so there's nothing to point archivePaths at.
    auto resourceManager = std::make_shared<Ship::ResourceManager>(context->GetChildren().GetFirst<Ship::ThreadPool>());
    context->GetChildren().Add(resourceManager);

    // ---- Control Deck ----
    auto controlDeck = std::make_shared<LUS::ControlDeck>(window, consoleVariable);
    context->GetChildren().Add(controlDeck);

    // ---- Crash Handler ----
    context->GetChildren().Add(std::make_shared<Ship::CrashHandler>());

    // ---- Console ----
    auto console = std::make_shared<Ship::Console>();
    context->GetChildren().Add(console);

    // ---- Window ----
    context->GetChildren().Add(window);

    // ---- Audio ----
    auto audio = std::make_shared<Ship::Audio>(Ship::AudioSettings{}, config);
    context->GetChildren().Add(audio);

    // ---- Events ----
    context->GetChildren().Add(std::make_shared<Ship::Events>());

    // ---- File Drop Manager ----
    auto fileDropMgr = std::make_shared<Ship::FileDrop>(window);
    context->GetChildren().Add(fileDropMgr);

    // ---- Init all components that need it ----
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
    // Rare's "Indy" engine ucode, GoldenEye variant (frigate#4/#2 -- see
    // libultraship-wiiu's fast/indy.h for the GE/PD opcode-numbering split).
    // UcodeHandlers is declared at global scope, not inside Fast::.
    window->SetRendererUCode(::ucode_indy_ge);
    audio->Init();
    fileDropMgr->Init();

    return context;
}

} // namespace Frigate::Platform
