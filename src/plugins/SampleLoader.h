#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// What the Sampler and Corona's response loader share with the app: a decoded
// sample, the Loader the app gives the factory to decode one, and the state string
// that keeps a sample's path in an insert. Kept apart from plugins/Sampler.h so
// the factory's header (plugins/BuiltIns.h) does not bring the whole instrument
// into every file that makes an insert. JUCE-free.

namespace northlight::plugins::sampler
{

// A decoded sample, immutable once made and shared by every processor that
// plays the file.
struct Sample
{
    std::string path;
    double sampleRate = 48000.0;
    std::vector<float> left, right; // right is empty for a mono file

    std::int64_t frames() const noexcept { return static_cast<std::int64_t>(left.size()); }
    bool stereo() const noexcept { return !right.empty(); }
};

// Decodes a file for the sampler (controller thread; may block on disk). Null
// with `error` set when the file cannot be read.
using Loader = std::function<std::shared_ptr<const Sample>(const std::string& path, std::string& error)>;

// The state string for a sample path (base64 of the path) and back; an empty
// path is an empty state.
std::string encodeState(const std::string& path);
std::string decodeState(const std::string& state);

} // namespace northlight::plugins::sampler
