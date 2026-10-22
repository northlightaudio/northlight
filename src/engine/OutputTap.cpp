#include "engine/OutputTap.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace northlight::engine
{

namespace
{
constexpr int tapChannels = 2;
constexpr double highestSampleRate = 192000.0;

std::string nowAsText()
{
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[64];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
    return text;
}
} // namespace

OutputTap::OutputTap(std::string folder, double ringSeconds)
    : folder_(std::move(folder)),
      ring_(static_cast<std::size_t>(ringSeconds * highestSampleRate) * tapChannels)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("OutputTap::OutputTap");
    events_ = std::fopen((folder_ + "/events.csv").c_str(), "w");
    if (events_ != nullptr)
        std::fputs("file,seconds,kind,channel,size,block_offset,block_frames\n", events_);
    thread_ = std::thread([this] { run(); });
}

OutputTap::~OutputTap()
{
    stop();
}

// ----- audio thread ---------------------------------------------------------------

void OutputTap::capture(const float* const* outputs,
                        int numOutputs,
                        int numFrames,
                        double sampleRate,
                        double lateness,
                        double load) noexcept
{
    if (numFrames <= 0 || sampleRate <= 0.0)
        return;
    // The first two outputs; a mono device is written to both sides.
    const float* left = numOutputs > 0 ? outputs[0] : nullptr;
    const float* right = numOutputs > 1 ? outputs[1] : left;
    const float* channels[tapChannels] = {left, right};
    if (!ring_.pushFrames(channels, tapChannels, numFrames))
    {
        pendingDropped_ += static_cast<std::uint32_t>(numFrames);
        return;
    }
    // The frames go in before their record, so the writer always finds them.
    const Block block{numFrames,
                      static_cast<float>(lateness),
                      static_cast<float>(load),
                      sampleRate,
                      pendingDropped_,
                      pendingOrphans_};
    if (blocks_.push(block))
    {
        pendingDropped_ = 0;
        pendingOrphans_ = 0;
    }
    else
    {
        pendingOrphans_ += static_cast<std::uint32_t>(numFrames);
    }
}

// ----- controller -------------------------------------------------------------------

void OutputTap::stop()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("OutputTap::stop");
    {
        std::lock_guard lock(mutex_);
        if (stopped_)
            return;
        stopRequested_ = true;
        stopped_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

OutputTap::Totals OutputTap::totals() const noexcept
{
    Totals t;
    t.jumps = jumps_.load(std::memory_order_relaxed);
    t.gaps = gaps_.load(std::memory_order_relaxed);
    t.jumpsAtBlockStart = jumpsAtBlockStart_.load(std::memory_order_relaxed);
    t.lateCallbacks = late_.load(std::memory_order_relaxed);
    t.overruns = overruns_.load(std::memory_order_relaxed);
    t.droppedFrames = dropped_.load(std::memory_order_relaxed);
    t.frames = frames_.load(std::memory_order_relaxed);
    return t;
}

// ----- writer thread ----------------------------------------------------------------

void OutputTap::run()
{
    for (;;)
    {
        bool stopping = false;
        {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(20), [this] { return stopRequested_; });
            stopping = stopRequested_;
        }
        drain();
        if (stopping)
            break;
    }
    closeFile();
    writeSummary();
    if (events_ != nullptr)
    {
        std::fclose(events_);
        events_ = nullptr;
    }
}

void OutputTap::drain()
{
    Block block;
    while (blocks_.pop(block))
        handle(block);
    if (events_ != nullptr)
        std::fflush(events_);
}

void OutputTap::handle(const Block& block)
{
    // Frames whose record was lost: skip them so the rest stay in step.
    for (std::uint32_t left = block.orphanBefore; left > 0;)
    {
        const int chunk = static_cast<int>(std::min<std::uint32_t>(left, 4096));
        buffer_.resize(static_cast<size_t>(chunk) * tapChannels);
        ring_.pop(buffer_.data(), chunk * tapChannels);
        left -= static_cast<std::uint32_t>(chunk);
    }

    if (block.sampleRate != fileRate_ || !wav_.isOpen())
        openFile(block.sampleRate);

    const auto dropped = static_cast<std::int64_t>(block.droppedBefore) + block.orphanBefore;
    if (dropped > 0)
    {
        dropped_.fetch_add(dropped, std::memory_order_relaxed);
        logEvent(static_cast<double>(fileFrames_) / fileRate_,
                 "tap-dropped",
                 -1,
                 static_cast<double>(dropped),
                 -1,
                 0);
        detector_->reset(); // the audio has a hole the detector must not read as a click
    }

    const int samples = block.numFrames * tapChannels;
    buffer_.resize(static_cast<size_t>(samples));
    int got = 0;
    while (got < samples)
    {
        const int n = ring_.pop(buffer_.data() + got, samples - got);
        if (n <= 0)
            break; // cannot happen: the frames were pushed before the record
        got += n;
    }

    const double seconds = static_cast<double>(fileFrames_) / fileRate_;
    const double blockSeconds = block.numFrames / fileRate_;
    if (rt::CallbackStats::isLate(block.lateness * blockSeconds, blockSeconds))
    {
        late_.fetch_add(1, std::memory_order_relaxed);
        logEvent(seconds, "late-callback", -1, block.lateness, 0, block.numFrames);
    }
    if (block.load > 1.0f)
    {
        overruns_.fetch_add(1, std::memory_order_relaxed);
        logEvent(seconds, "overrun", -1, block.load, 0, block.numFrames);
    }

    recent_.push_back({fileFrames_, block.numFrames});
    wav_.writeInterleaved(buffer_.data(), block.numFrames);
    detector_->setBlockSize(block.numFrames);
    found_.clear();
    detector_->process(buffer_.data(), block.numFrames, found_);
    fileFrames_ += block.numFrames;
    frames_.fetch_add(block.numFrames, std::memory_order_relaxed);
    logGlitches(found_);

    // Jumps are found up to a detector frame late; keep the blocks they may fall in.
    while (recent_.size() > 1 &&
           recent_.front().start + recent_.front().numFrames < fileFrames_ - 4 * GlitchDetector::frameSize)
        recent_.pop_front();

    if (fileFrames_ - lastHeaderFrames_ >= static_cast<std::int64_t>(fileRate_))
    {
        wav_.updateHeader();
        lastHeaderFrames_ = fileFrames_;
    }
}

void OutputTap::openFile(double sampleRate)
{
    closeFile();
    ++fileIndex_;
    fileName_ = fileIndex_ == 1 ? std::string("output.wav") : "output-" + std::to_string(fileIndex_) + ".wav";
    wav_.open(folder_ + "/" + fileName_, tapChannels, sampleRate);
    fileRate_ = sampleRate;
    fileFrames_ = 0;
    lastHeaderFrames_ = 0;
    recent_.clear();
    detector_ = std::make_unique<GlitchDetector>(tapChannels, sampleRate);
    if (started_.empty())
        started_ = nowAsText();
}

void OutputTap::closeFile()
{
    if (detector_ != nullptr)
    {
        // Silence flushes the last analysis frame through the detector without
        // being written.
        std::vector<float> flush(static_cast<size_t>(GlitchDetector::frameSize) * tapChannels, 0.0f);
        found_.clear();
        detector_->process(flush.data(), GlitchDetector::frameSize, found_);
        found_.erase(std::remove_if(found_.begin(),
                                    found_.end(),
                                    [this](const Glitch& g) { return g.frame >= fileFrames_; }),
                     found_.end());
        logGlitches(found_);
        detector_.reset();
    }
    if (wav_.isOpen())
        wav_.close();
}

int OutputTap::blockOffset(std::int64_t frame, int& blockFrames) const
{
    for (auto it = recent_.rbegin(); it != recent_.rend(); ++it)
        if (frame >= it->start && frame < it->start + it->numFrames)
        {
            blockFrames = it->numFrames;
            return static_cast<int>(frame - it->start);
        }
    blockFrames = 0;
    return -1;
}

void OutputTap::logGlitches(const std::vector<Glitch>& found)
{
    for (const auto& g : found)
    {
        int blockFrames = 0;
        const int offset = blockOffset(g.frame, blockFrames);
        const double seconds = static_cast<double>(g.frame) / fileRate_;
        if (g.kind == Glitch::Kind::jump)
        {
            jumps_.fetch_add(1, std::memory_order_relaxed);
            if (offset >= 0 && offset <= 1)
                jumpsAtBlockStart_.fetch_add(1, std::memory_order_relaxed);
            logEvent(seconds, "jump", g.channel, g.size, offset, blockFrames);
        }
        else
        {
            gaps_.fetch_add(1, std::memory_order_relaxed);
            logEvent(seconds, "gap", -1, g.size, offset, blockFrames);
        }
    }
}

void OutputTap::logEvent(
    double seconds, const char* kind, int channel, double size, int offset, int blockFrames)
{
    if (events_ == nullptr)
        return;
    std::fprintf(events_,
                 "%s,%.6f,%s,%s,%.6g,%d,%d\n",
                 fileName_.c_str(),
                 seconds,
                 kind,
                 channel < 0 ? "all" : (channel == 0 ? "left" : "right"),
                 size,
                 offset,
                 blockFrames);
}

void OutputTap::writeSummary()
{
    std::FILE* f = std::fopen((folder_ + "/summary.txt").c_str(), "w");
    if (f == nullptr)
        return;
    const auto t = totals();
    std::fprintf(f, "Northlight output tap\n\n");
    std::fprintf(f,
                 "Started %s; %lld frames in %d file(s), the last at %.0f Hz.\n\n",
                 started_.empty() ? "(no audio)" : started_.c_str(),
                 static_cast<long long>(t.frames),
                 fileIndex_,
                 fileRate_);
    std::fprintf(f, "In what Northlight handed the driver:\n");
    std::fprintf(f,
                 "  %lld jumps (%lld on the first two samples of a block)\n",
                 static_cast<long long>(t.jumps),
                 static_cast<long long>(t.jumpsAtBlockStart));
    std::fprintf(f, "  %lld gaps (short runs of digital silence)\n\n", static_cast<long long>(t.gaps));
    std::fprintf(f, "Callback timing:\n");
    std::fprintf(f,
                 "  %lld late callbacks (started half a block and 2 ms or more after they were due)\n",
                 static_cast<long long>(t.lateCallbacks));
    std::fprintf(f, "  %lld overruns (took longer than the block)\n", static_cast<long long>(t.overruns));
    if (t.droppedFrames > 0)
        std::fprintf(f,
                     "\nThe tap fell behind and left %lld frames out of the file (tap-dropped in "
                     "events.csv).\n",
                     static_cast<long long>(t.droppedFrames));
    std::fprintf(f,
                 "\nReading this (times are in events.csv, from the start of each file):\n"
                 "- A click you hear at the time of a jump or gap is in Northlight's own output: the\n"
                 "  engine or a plugin made it. Jumps mostly on the first samples of a block point at\n"
                 "  a block-boundary bug. Instant attacks (a drum hit, a synth note with no attack)\n"
                 "  are steps too, so jumps that fall on beats are usually the music.\n"
                 "- A click that is not in output.wav happened after Northlight handed the audio\n"
                 "  over: in the driver, the system or the hardware. Late callbacks at the same times\n"
                 "  point at system timing (a driver holding the CPU; LatencyMon shows which). None\n"
                 "  point at the driver, the USB connection or the device's clock settings.\n");
    std::fclose(f);
}

} // namespace northlight::engine
