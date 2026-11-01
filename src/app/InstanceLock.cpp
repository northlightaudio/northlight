#include "app/InstanceLock.h"

#if !JUCE_WINDOWS
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace northlight
{

#if JUCE_WINDOWS

std::unique_ptr<InstanceLock> InstanceLock::hold(const juce::File& file)
{
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    if (!stream->openedOk())
        return nullptr;
    std::unique_ptr<InstanceLock> lock(new InstanceLock(file));
    lock->stream_ = std::move(stream);
    return lock;
}

InstanceLock::~InstanceLock()
{
    stream_.reset();
    file_.deleteFile();
}

bool InstanceLock::isHeld(const juce::File& file)
{
    // A lock file that can be deleted is stale; one held open cannot be.
    return file.existsAsFile() && !file.deleteFile();
}

#else

namespace
{
int openForLock(const juce::File& file, int flags)
{
    return ::open(file.getFullPathName().toRawUTF8(), flags | O_RDWR | O_CLOEXEC, 0644);
}
} // namespace

std::unique_ptr<InstanceLock> InstanceLock::hold(const juce::File& file)
{
    // Another instance's isHeld() can find the new file before it is locked and
    // delete it as stale; the lock is then on a file no path reaches, so check
    // the path still names the locked file and start again if not.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const int fd = openForLock(file, O_CREAT);
        if (fd < 0)
            return nullptr;
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0)
        {
            ::close(fd);
            return nullptr;
        }
        struct stat locked{}, named{};
        if (::fstat(fd, &locked) == 0 && ::stat(file.getFullPathName().toRawUTF8(), &named) == 0 &&
            locked.st_dev == named.st_dev && locked.st_ino == named.st_ino)
        {
            std::unique_ptr<InstanceLock> lock(new InstanceLock(file));
            lock->fd_ = fd;
            return lock;
        }
        ::close(fd);
    }
    return nullptr;
}

InstanceLock::~InstanceLock()
{
    // Deleted while still locked, so no other instance can take it in between.
    file_.deleteFile();
    ::close(fd_);
}

bool InstanceLock::isHeld(const juce::File& file)
{
    const int fd = openForLock(file, 0);
    if (fd < 0)
        return false; // no file: nothing holds it
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        const bool held = errno == EWOULDBLOCK;
        ::close(fd);
        return held;
    }
    file.deleteFile(); // stale: its owner closed or crashed
    ::close(fd);
    return false;
}

#endif

} // namespace northlight
