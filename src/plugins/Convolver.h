#pragma once

#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

// Zero-latency convolution of a mono signal with a mono or stereo impulse
// response. The response is split into partitions of `blockSize` taps: the
// first runs directly, sample by sample, so the output never waits; the rest
// run as FFT blocks (uniform partitions, overlap-save, a frequency-domain delay
// line of the input's spectra), and because partition p only needs input at
// least p blocks old, each block's FFT work finishes before its output is due.
// The result equals direct convolution to float precision with nothing added
// to the latency.
//
// A kernel (the response's head and partition spectra) is built off the
// real-time thread and is immutable. The convolver crossfades from one kernel
// to the next when the kernel it is given changes, so a new response never
// clicks. JUCE-free; process() is real-time safe after prepare().

namespace northlight::plugins
{

struct ConvolverKernel
{
    std::uint64_t epoch = 0; // for rt::SnapshotPublisher when published alone
    int blockSize = 64;
    int length = 0;     // taps
    int partitions = 0; // blocks of taps, the head included
    int channels = 1;   // 1 (both outputs alike) or 2
    // Per channel: the head reversed (blockSize taps), and the spectra of
    // partitions 1.. (blockSize bins each, bin 0 packing DC and Nyquist as real
    // and imaginary), scaled for the inverse FFT.
    std::vector<float> head;
    std::vector<std::complex<float>> spectra;

    const float* headOf(int channel) const noexcept { return head.data() + channel * blockSize; }
    const std::complex<float>* spectrumOf(int channel, int partition) const noexcept
    {
        return spectra.data() + (static_cast<size_t>(channel) * static_cast<size_t>(partitions - 1) +
                                 static_cast<size_t>(partition - 1)) *
                                    static_cast<size_t>(blockSize);
    }
};

// Builds a kernel from a response (`right` null or equal to `left` for mono).
// Not real-time.
std::unique_ptr<ConvolverKernel>
makeConvolverKernel(const float* left, const float* right, int length, int blockSize = 64);

class Convolver
{
public:
    Convolver();
    ~Convolver();
    Convolver(const Convolver&) = delete;
    Convolver& operator=(const Convolver&) = delete;

    // Sizes the input history for kernels of up to `maxPartitions` partitions of
    // `blockSize` taps. Not real-time.
    void prepare(int blockSize, int maxPartitions);
    void reset() noexcept;

    // Samples a kernel change crossfades over.
    void setCrossfadeLength(int samples) noexcept { fadeLength_ = samples > 0 ? samples : 1; }

    // Convolves `in` into the two outputs. `kernel` may differ from the last
    // call: the outputs then fade from the old one, `previous` (which must still
    // be alive: the caller keeps it, e.g. in the published slot), to the new.
    // A null kernel passes the input to both outputs.
    void process(const ConvolverKernel* kernel,
                 const ConvolverKernel* previous,
                 const float* in,
                 float* outLeft,
                 float* outRight,
                 int numSamples) noexcept;

private:
    struct Fft;
    void finishBlock() noexcept;
    void tail(const ConvolverKernel& k, std::vector<float>* out) noexcept;

    std::unique_ptr<Fft> fft_;
    int blockSize_ = 64, maxPartitions_ = 1;
    int position_ = 0;         // within the block being filled
    std::uint64_t blocks_ = 0; // blocks finished
    std::vector<float> input_; // the last two blocks (2 x blockSize)
    std::vector<std::complex<float>> history_, scratch_;
    std::vector<float> time_;
    std::vector<float> tail_[2], fadeTail_[2]; // per output: the tails due in this block
    const ConvolverKernel* kernel_ = nullptr;  // the kernel the tails were made with
    const ConvolverKernel* fading_ = nullptr;  // the kernel being faded from
    int fadeLeft_ = 0, fadeLength_ = 1024;
};

} // namespace northlight::plugins
