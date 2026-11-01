#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace northlight
{

// A lock file that says its owner is still running: held for as long as the
// owner lives and freed when it closes or crashes. Several instances sharing a
// folder use it to tell a running instance's files from ones a closed instance
// left. Two holders in one process see each other too.
//
// Windows: the file is kept open, and Windows refuses to delete a file another
// handle holds open. Elsewhere: an flock() on the open file, which belongs to
// that open file rather than the process.
class InstanceLock
{
public:
    // Creates the file and holds it; nullptr when it cannot.
    static std::unique_ptr<InstanceLock> hold(const juce::File& file);
    // Releases the lock and deletes the file.
    ~InstanceLock();

    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    const juce::File& file() const noexcept { return file_; }

    // Whether a live holder has `file`. A stale lock file (its owner closed or
    // crashed) is deleted.
    static bool isHeld(const juce::File& file);

private:
    explicit InstanceLock(juce::File file) : file_(std::move(file)) {}

    juce::File file_;
#if JUCE_WINDOWS
    std::unique_ptr<juce::FileOutputStream> stream_;
#else
    int fd_ = -1;
#endif
};

} // namespace northlight
