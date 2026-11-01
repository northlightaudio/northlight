#include "app/InstanceLock.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

namespace
{
struct LockFolder
{
    LockFolder() { REQUIRE(folder.createDirectory().wasOk()); }
    ~LockFolder() { folder.deleteRecursively(); }

    juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("northlight-instance-lock-tests")
                            .getChildFile(juce::Uuid().toString());
};
} // namespace

TEST_CASE("An instance lock is held while its owner lives, even by another holder in the same process",
          "[app][autosave][lock]")
{
    LockFolder f;
    const auto file = f.folder.getChildFile("owner.lock");
    auto lock = InstanceLock::hold(file);
    REQUIRE(lock != nullptr);
    CHECK(file.existsAsFile());

    // Asking twice must not free it: the check never takes a live lock away.
    CHECK(InstanceLock::isHeld(file));
    CHECK(InstanceLock::isHeld(file));
    CHECK(file.existsAsFile());

    lock.reset();
    CHECK_FALSE(file.existsAsFile());
    CHECK_FALSE(InstanceLock::isHeld(file));
}

TEST_CASE("A lock file nobody holds is stale and is cleared", "[app][autosave][lock]")
{
    LockFolder f;
    const auto file = f.folder.getChildFile("crashed.lock");
    REQUIRE(file.create().wasOk()); // what a crashed owner leaves
    CHECK_FALSE(InstanceLock::isHeld(file));
    CHECK_FALSE(file.existsAsFile());
}
