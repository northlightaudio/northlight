#pragma once

#include "engine/GlitchDetector.h"
#include "engine/Recording.h"
#include "engine/WavFile.h"
#include "engine/rt/CallbackStats.h"
#include "engine/rt/SpscQueue.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The output tap (--output-tap <folder>): records exactly what the audio callback
// hands the driver, so a click can be placed on one side of the driver or the other.
//
//   audio thread --frames (RecordRing) + block records (SpscQueue)--> tap writer thread
//
// The writer puts the first two outputs in output.wav (32-bit float; a new file,
// output-2.wav and so on, when the sample rate changes), runs a GlitchDetector over
// them, and writes events.csv as it goes: every jump or gap found in the audio, and
// every callback that started late or overran, each with its time and its position
// in the device block. stop() finishes the files and writes summary.txt.
//
// Reading it: clicks you hear that are also in output.wav are made by Northlight
// (jumps at block offset 0 point at a block-boundary bug). Clicks that are not in the
// file happened after it, in the driver, the system or the hardware; late callbacks
// at the same times point at system timing.

namespace northlight::engine
{

class OutputTap
{
public:
    // Starts the writer; `folder` must exist. ringSeconds of audio at the highest
    // sample rate are buffered between the threads.
    explicit OutputTap(std::string folder, double ringSeconds = 4.0);
    ~OutputTap();
    OutputTap(const OutputTap&) = delete;
    OutputTap& operator=(const OutputTap&) = delete;

    // Audio thread, after the output is final. `lateness` is how late the callback
    // started and `load` how long it took, both as fractions of the block.
    void capture(const float* const* outputs,
                 int numOutputs,
                 int numFrames,
                 double sampleRate,
                 double lateness,
                 double load) noexcept;

    // Controller. Drains what is buffered, finishes the files and writes
    // summary.txt. Idempotent; the destructor calls it. capture() must not run
    // during or after it.
    void stop();

    struct Totals
    {
        std::int64_t jumps = 0, gaps = 0;
        std::int64_t jumpsAtBlockStart = 0; // jumps on the first two samples of a block
        std::int64_t lateCallbacks = 0, overruns = 0;
        std::int64_t droppedFrames = 0; // the writer fell behind; not in the file
        std::int64_t frames = 0;        // frames written to the files
    };
    // Any thread; what the writer has handled so far.
    Totals totals() const noexcept;

    const std::string& folder() const noexcept { return folder_; }

private:
    struct Block
    {
        int numFrames = 0;
        float lateness = 0.0f;
        float load = 0.0f;
        double sampleRate = 0.0;
        std::uint32_t droppedBefore = 0; // frames dropped since the previous block
        std::uint32_t orphanBefore = 0;  // frames in the ring whose block record was lost
    };
    struct RecentBlock
    {
        std::int64_t start = 0; // first frame in the current file
        int numFrames = 0;
    };

    void run();
    void drain();
    void handle(const Block& block);
    void openFile(double sampleRate);
    void closeFile();
    void logGlitches(const std::vector<Glitch>& found);
    void logEvent(double seconds, const char* kind, int channel, double size, int offset, int blockFrames);
    int blockOffset(std::int64_t frame, int& blockFrames) const;
    void writeSummary();

    std::string folder_;
    RecordRing ring_;
    rt::SpscQueue<Block> blocks_{65536};
    std::uint32_t pendingDropped_ = 0; // audio thread only
    std::uint32_t pendingOrphans_ = 0; // audio thread only

    // Writer thread.
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopRequested_ = false;
    bool stopped_ = false;
    WavWriter wav_;
    std::FILE* events_ = nullptr;
    double fileRate_ = 0.0;
    int fileIndex_ = 0;
    std::string fileName_;
    std::int64_t fileFrames_ = 0;
    std::int64_t lastHeaderFrames_ = 0;
    std::unique_ptr<GlitchDetector> detector_;
    std::deque<RecentBlock> recent_;
    std::vector<float> buffer_;
    std::vector<Glitch> found_;
    std::string started_; // wall-clock time of the first block

    std::atomic<std::int64_t> jumps_{0}, gaps_{0}, jumpsAtBlockStart_{0}, late_{0}, overruns_{0}, dropped_{0},
        frames_{0};
};

} // namespace northlight::engine
