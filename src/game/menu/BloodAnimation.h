#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frigate::Game::Menu {

// The blood overlay's frame size: the decoded 96x80 picture, as an
// intensity texture of 4 bits a texel.
constexpr int kBloodTextureWidth = 96;
constexpr int kBloodTextureHeight = 80;
constexpr size_t kBloodTextureBytes = kBloodTextureWidth * kBloodTextureHeight / 2;

// The blood animation's stream and the per-frame decode of it that
// die_blood_image_routine() and decrypt_bleeding_animation_data() do. The
// stream's layout is in docs/formats/goldeneye-eye-intro-resources.md.
class BloodAnimation {
  public:
    explicit BloodAnimation(std::vector<uint8_t> stream);

    // die_blood_image_routine(0): decodes the stream's first frame.
    void Start();

    // die_blood_image_routine(1): moves to the frame after the last one
    // decoded (unless that ran off the end of the stream) and decodes it.
    // Returns whether the stream is now exhausted.
    bool Advance();

    // The current frame, kBloodTextureBytes long.
    const uint8_t* Texture() const {
        return frames_[current_frame_].data();
    }

  private:
    bool Decode();

    std::vector<uint8_t> stream_;
    size_t current_ = 0;
    size_t next_ = 0;
    // bloodImgBufPtrArray: each frame is decoded into the buffer the last one
    // was not drawn from, so a frame still being drawn is never overwritten.
    std::array<std::array<uint8_t, kBloodTextureWidth * kBloodTextureHeight>, 2> frames_{};
    size_t current_frame_ = 0;
};

} // namespace Frigate::Game::Menu
