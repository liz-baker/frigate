#include "platform/FirstRun.h"

#include "libultraship/log/luslog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#ifndef __WIIU__
#include <SDL3/SDL.h>
#endif

namespace Frigate::Platform {

namespace {

using Extract::FirstRunResult;
using Extract::FirstRunStatus;
using Extract::Progress;

// The worker's progress, read by the thread presenting it.
struct Shared {
    std::mutex mutex;
    std::condition_variable changed;
    Progress progress;
    bool done = false;
};

#ifndef __WIIU__

// SDL's debug text is 8x8 pixel cells; the renderer scale makes them readable.
constexpr float kScale = 2.0f;
constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 400;
constexpr int kColumns = int(kWindowWidth / kScale / SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) - 4;
constexpr int kRowHeight = 10;
constexpr int kRows = int(kWindowHeight / kScale / kRowHeight) - 4;
// How long an error stays up with nobody to close it.
constexpr auto kErrorTimeout = std::chrono::seconds(30);

std::vector<std::string> Wrap(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        std::string paragraph = text.substr(start, end == std::string::npos ? end : end - start);
        std::string line;
        size_t at = 0;
        while (at < paragraph.size()) {
            size_t space = paragraph.find(' ', at);
            std::string word = paragraph.substr(at, space == std::string::npos ? space : space - at);
            at = space == std::string::npos ? paragraph.size() : space + 1;
            while (word.size() > size_t(kColumns)) {
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                lines.push_back(word.substr(0, kColumns));
                word.erase(0, kColumns);
            }
            if (!line.empty() && line.size() + 1 + word.size() > size_t(kColumns)) {
                lines.push_back(line);
                line.clear();
            }
            line += (line.empty() ? "" : " ") + word;
        }
        lines.push_back(line);
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

class ProgressWindow {
  public:
    ~ProgressWindow() {
        if (mRenderer != nullptr) {
            SDL_DestroyRenderer(mRenderer);
        }
        if (mWindow != nullptr) {
            SDL_DestroyWindow(mWindow);
        }
        if (mVideo) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    bool Open() {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            LUSLOG_INFO("frigate: first run: no window (%s); progress goes to the log", SDL_GetError());
            return false;
        }
        mVideo = true;
        mWindow = SDL_CreateWindow("Frigate", kWindowWidth, kWindowHeight, 0);
        if (mWindow != nullptr) {
            mRenderer = SDL_CreateRenderer(mWindow, "software");
        }
        if (mRenderer == nullptr) {
            LUSLOG_INFO("frigate: first run: no window (%s); progress goes to the log", SDL_GetError());
            return false;
        }
        SDL_SetRenderScale(mRenderer, kScale, kScale);
        return true;
    }

    // False once the user has asked to close the window.
    bool Pump() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                return false;
            }
        }
        return true;
    }

    void DrawProgress(const Progress& progress) {
        Begin();
        Text(2, 2, "Preparing GoldenEye 007 (first run only)");
        Text(2, 5, "Extracting the game data from your ROM.");
        std::string stage = progress.stageCount != 0
                                ? "[" + std::to_string(progress.stageIndex + 1) + "/" +
                                      std::to_string(progress.stageCount) + "] " + progress.stageName
                                : "Reading the ROM";
        Text(2, 8, stage);
        Text(2, 10,
             progress.total != 0 ? std::to_string(progress.done) + " / " + std::to_string(progress.total)
                                 : progress.detail);

        const float barX = 2 * 8, barY = 13 * kRowHeight, barWidth = (kWindowWidth / kScale) - 4 * 8, barHeight = 12;
        SDL_SetRenderDrawColor(mRenderer, 60, 60, 70, 255);
        SDL_FRect back{ barX, barY, barWidth, barHeight };
        SDL_RenderFillRect(mRenderer, &back);
        SDL_SetRenderDrawColor(mRenderer, 90, 190, 110, 255);
        SDL_FRect fill{ barX, barY, barWidth * float(std::clamp(progress.fraction, 0.0, 1.0)), barHeight };
        SDL_RenderFillRect(mRenderer, &fill);
        Text(2, 16, std::to_string(int(std::clamp(progress.fraction, 0.0, 1.0) * 100)) + "%");
        Text(2, kRows + 1, "Close this window to cancel.");
        SDL_RenderPresent(mRenderer);
    }

    // Shows the message until a key is pressed, the window is closed or the timeout passes.
    void ShowError(const std::string& message) {
        std::vector<std::string> lines = Wrap(message);
        if (lines.size() > size_t(kRows - 4)) {
            lines.resize(kRows - 4);
            lines.back() = "...";
        }
        Begin();
        Text(2, 2, "Frigate could not set up the game data");
        for (size_t i = 0; i < lines.size(); i++) {
            Text(2, 5 + int(i), lines[i]);
        }
        Text(2, kRows + 1, "Press any key or close this window.");
        SDL_RenderPresent(mRenderer);
        const auto deadline = std::chrono::steady_clock::now() + kErrorTimeout;
        while (std::chrono::steady_clock::now() < deadline) {
            SDL_Event event;
            if (SDL_WaitEventTimeout(&event, 200) &&
                (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                 event.type == SDL_EVENT_KEY_DOWN)) {
                return;
            }
        }
    }

  private:
    void Begin() {
        SDL_SetRenderDrawColor(mRenderer, 20, 22, 28, 255);
        SDL_RenderClear(mRenderer);
        SDL_SetRenderDrawColor(mRenderer, 230, 230, 235, 255);
    }

    void Text(int column, int row, const std::string& text) {
        SDL_SetRenderDrawColor(mRenderer, 230, 230, 235, 255);
        SDL_RenderDebugText(mRenderer, float(column * 8), float(row * kRowHeight), text.c_str());
    }

    bool mVideo = false;
    SDL_Window* mWindow = nullptr;
    SDL_Renderer* mRenderer = nullptr;
};

#endif

} // namespace

FirstRunResult RunFirstRun(Extract::FirstRunOptions options, bool allowWindow) {
    if (Extract::MissingArchives(options.archiveDir).empty()) {
        return {};
    }

    Shared shared;
    std::atomic<bool> cancel{ false };
    options.cancel = &cancel;
    options.log = [](const std::string& text) { LUSLOG_INFO("frigate: first run: %s", text.c_str()); };
    std::string lastStage;
    options.progress = [&shared, &lastStage](const Progress& progress) {
        std::lock_guard lock(shared.mutex);
        shared.progress = progress;
        if (progress.stageName != lastStage && !progress.stageName.empty()) {
            lastStage = progress.stageName;
            LUSLOG_INFO("frigate: first run: [%zu/%zu] %s", progress.stageIndex + 1, progress.stageCount,
                        progress.stageName.c_str());
        }
    };

    FirstRunResult result;
    std::thread worker([&] {
        result = Extract::EnsureArchives(options);
        {
            std::lock_guard lock(shared.mutex);
            shared.done = true;
        }
        shared.changed.notify_one();
    });

#ifndef __WIIU__
    ProgressWindow window;
    const bool haveWindow = allowWindow && window.Open();
#else
    (void)allowWindow;
    const bool haveWindow = false;
#endif

    for (;;) {
        Progress progress;
        bool done;
        {
            std::unique_lock lock(shared.mutex);
            shared.changed.wait_for(lock, std::chrono::milliseconds(haveWindow ? 33 : 250));
            progress = shared.progress;
            done = shared.done;
        }
        if (done) {
            break;
        }
#ifndef __WIIU__
        if (haveWindow) {
            if (!window.Pump()) {
                cancel = true;
            }
            window.DrawProgress(progress);
        }
#endif
    }
    worker.join();

    if (result.status == FirstRunStatus::Failed) {
        LUSLOG_ERROR("frigate: first run failed:\n%s", result.message.c_str());
#ifndef __WIIU__
        if (haveWindow) {
            window.ShowError(result.message);
        }
#endif
    } else if (result.status == FirstRunStatus::Cancelled) {
        LUSLOG_WARN("frigate: first run: %s", result.message.c_str());
    }
    return result;
}

} // namespace Frigate::Platform
