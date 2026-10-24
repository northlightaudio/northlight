#include "plugins/Convolver.h"

#include "engine/rt/RealtimeGuard.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <signalsmith-linear/fft.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>

namespace northlight::plugins
{

struct Convolver::Fft
{
    signalsmith::linear::Pow2RealFFT<float> fft;
};

namespace
{
// Accumulates a * b into acc over a packed real spectrum (bin 0 holds DC and
// Nyquist, both real).
void multiplyAdd(std::complex<float>* acc,
                 const std::complex<float>* a,
                 const std::complex<float>* b,
                 int bins) noexcept
{
    acc[0] += std::complex<float>(a[0].real() * b[0].real(), a[0].imag() * b[0].imag());
    for (int i = 1; i < bins; ++i)
    {
        const float ar = a[i].real(), ai = a[i].imag(), br = b[i].real(), bi = b[i].imag();
        acc[i] += std::complex<float>(ar * br - ai * bi, ar * bi + ai * br);
    }
}

float dot(const float* a, const float* b, int n) noexcept
{
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += a[i] * b[i];
    return sum;
}
} // namespace

std::unique_ptr<ConvolverKernel>
makeConvolverKernel(const float* left, const float* right, int length, int blockSize)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("makeConvolverKernel");
    auto k = std::make_unique<ConvolverKernel>();
    k->blockSize = std::max(1, blockSize);
    const int b = k->blockSize;
    k->length = std::max(0, length);
    k->partitions = std::max(1, (k->length + b - 1) / b);
    k->channels = right != nullptr && right != left ? 2 : 1;
    if (k->channels == 2 && std::equal(left, left + length, right))
        k->channels = 1;
    k->head.assign(static_cast<size_t>(k->channels * b), 0.0f);
    k->spectra.assign(static_cast<size_t>(k->channels) * static_cast<size_t>(k->partitions - 1) *
                          static_cast<size_t>(b),
                      {});
    signalsmith::linear::Pow2RealFFT<float> fft(static_cast<size_t>(2 * b));
    std::vector<float> time(static_cast<size_t>(2 * b));
    std::vector<std::complex<float>> freq(static_cast<size_t>(b));
    const float scale = 1.0f / static_cast<float>(2 * b);
    for (int c = 0; c < k->channels; ++c)
    {
        const float* h = c == 0 ? left : right;
        float* head = k->head.data() + c * b;
        for (int j = 0; j < b; ++j)
        {
            const int tap = b - 1 - j;
            head[j] = tap < length ? h[tap] : 0.0f;
        }
        for (int p = 1; p < k->partitions; ++p)
        {
            std::fill(time.begin(), time.end(), 0.0f);
            for (int j = 0; j < b; ++j)
            {
                const int tap = p * b + j;
                time[static_cast<size_t>(j)] = tap < length ? h[tap] : 0.0f;
            }
            fft.fft(time.data(), freq.data());
            auto* out = k->spectra.data() + (static_cast<size_t>(c) * static_cast<size_t>(k->partitions - 1) +
                                             static_cast<size_t>(p - 1)) *
                                                static_cast<size_t>(b);
            for (int i = 0; i < b; ++i)
                out[i] = freq[static_cast<size_t>(i)] * scale;
        }
    }
    return k;
}

Convolver::Convolver() : fft_(std::make_unique<Fft>()) {}
Convolver::~Convolver() = default;

void Convolver::prepare(int blockSize, int maxPartitions)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("Convolver::prepare");
    blockSize_ = std::max(1, blockSize);
    maxPartitions_ = std::max(1, maxPartitions);
    fft_->fft.resize(static_cast<size_t>(2 * blockSize_));
    input_.assign(static_cast<size_t>(2 * blockSize_), 0.0f);
    history_.assign(static_cast<size_t>(maxPartitions_) * static_cast<size_t>(blockSize_), {});
    scratch_.assign(static_cast<size_t>(blockSize_), {});
    time_.assign(static_cast<size_t>(2 * blockSize_), 0.0f);
    for (auto* v : {&tail_[0], &tail_[1], &fadeTail_[0], &fadeTail_[1]})
        v->assign(static_cast<size_t>(blockSize_), 0.0f);
    reset();
}

void Convolver::reset() noexcept
{
    std::fill(input_.begin(), input_.end(), 0.0f);
    std::fill(history_.begin(), history_.end(), std::complex<float>{});
    for (auto* v : {&tail_[0], &tail_[1], &fadeTail_[0], &fadeTail_[1]})
        std::fill(v->begin(), v->end(), 0.0f);
    position_ = 0;
    blocks_ = 0;
    kernel_ = fading_ = nullptr;
    fadeLeft_ = 0;
}

// The tails due in the block being filled (index blocks_): the sum over the
// partitions past the head of each partition's spectrum times the input block
// that many blocks back.
void Convolver::tail(const ConvolverKernel& k, std::vector<float>* out) noexcept
{
    const int b = blockSize_;
    const int partitions = std::min(k.partitions, maxPartitions_);
    for (int c = 0; c < k.channels; ++c)
    {
        std::fill(scratch_.begin(), scratch_.end(), std::complex<float>{});
        for (int p = 1; p < partitions; ++p)
        {
            if (static_cast<std::uint64_t>(p) > blocks_)
                break; // no input that old yet
            const auto slot =
                (blocks_ - static_cast<std::uint64_t>(p)) % static_cast<std::uint64_t>(maxPartitions_);
            multiplyAdd(scratch_.data(),
                        k.spectrumOf(c, p),
                        history_.data() + slot * static_cast<std::uint64_t>(b),
                        b);
        }
        fft_->fft.ifft(scratch_.data(), time_.data());
        std::copy(time_.begin() + b, time_.end(), out[c].begin());
    }
    if (k.channels == 1)
        std::copy(out[0].begin(), out[0].end(), out[1].begin());
}

void Convolver::finishBlock() noexcept
{
    const int b = blockSize_;
    const auto slot = blocks_ % static_cast<std::uint64_t>(maxPartitions_);
    fft_->fft.fft(input_.data(), history_.data() + slot * static_cast<std::uint64_t>(b));
    ++blocks_;
    std::copy(input_.begin() + b, input_.end(), input_.begin());
    position_ = 0;
    if (kernel_ != nullptr)
        tail(*kernel_, tail_);
    if (fading_ != nullptr)
        tail(*fading_, fadeTail_);
}

void Convolver::process(const ConvolverKernel* kernel,
                        const ConvolverKernel* previous,
                        const float* in,
                        float* outLeft,
                        float* outRight,
                        int numSamples) noexcept
{
    if (kernel != nullptr && kernel->blockSize != blockSize_)
        kernel = nullptr;
    if (previous != nullptr && previous->blockSize != blockSize_)
        previous = nullptr;
    if (kernel != kernel_)
    {
        // The new kernel's tails for the block in progress come from the input
        // already heard; the old kernel's, already made, are faded out.
        if (kernel_ != nullptr && previous != nullptr && kernel != nullptr)
        {
            fading_ = previous;
            std::swap(tail_[0], fadeTail_[0]);
            std::swap(tail_[1], fadeTail_[1]);
            fadeLeft_ = fadeLength_;
        }
        else
        {
            fading_ = nullptr;
            fadeLeft_ = 0;
        }
        kernel_ = kernel;
        if (kernel_ != nullptr)
            tail(*kernel_, tail_);
    }

    const int b = blockSize_;
    for (int n = 0; n < numSamples; ++n)
    {
        input_[static_cast<size_t>(b + position_)] = in[n];
        if (kernel_ == nullptr)
        {
            outLeft[n] = outRight[n] = in[n];
        }
        else
        {
            const float* x = input_.data() + position_ + 1;
            float y[2];
            y[0] = dot(kernel_->headOf(0), x, b) + tail_[0][static_cast<size_t>(position_)];
            y[1] = kernel_->channels > 1
                       ? dot(kernel_->headOf(1), x, b) + tail_[1][static_cast<size_t>(position_)]
                       : y[0];
            if (fading_ != nullptr && fadeLeft_ > 0)
            {
                const float g = static_cast<float>(fadeLeft_) / static_cast<float>(fadeLength_);
                const float o0 = dot(fading_->headOf(0), x, b) + fadeTail_[0][static_cast<size_t>(position_)];
                const float o1 = fading_->channels > 1 ? dot(fading_->headOf(1), x, b) +
                                                             fadeTail_[1][static_cast<size_t>(position_)]
                                                       : o0;
                y[0] += (o0 - y[0]) * g;
                y[1] += (o1 - y[1]) * g;
                if (--fadeLeft_ == 0)
                    fading_ = nullptr;
            }
            outLeft[n] = y[0];
            outRight[n] = y[1];
        }
        if (++position_ == b)
            finishBlock();
    }
}

} // namespace northlight::plugins
