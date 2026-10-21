#include "engine/WavFile.h"
#include "ui/MediaBrowser.h"
#include "ui/MediaFiles.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
void writeWav(const juce::File& file, int channels, double rate, int frames)
{
    engine::WavWriter writer;
    REQUIRE(writer.open(file.getFullPathName().toStdString(), channels, rate));
    std::vector<float> data(static_cast<size_t>(frames * channels), 0.1f);
    REQUIRE(writer.writeInterleaved(data.data(), frames));
    REQUIRE(writer.close());
}
} // namespace

TEST_CASE("The Files tab lists folders first, then audio files with their length and rate",
          "[ui][browser][files]")
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("northlight-media-browser-tests")
                          .getChildFile(juce::Uuid().toString());
    REQUIRE(root.getChildFile("Drums").createDirectory().wasOk());
    writeWav(root.getChildFile("b pad.wav"), 1, 44100.0, 44100 * 2);
    writeWav(root.getChildFile("A lead.wav"), 2, 48000.0, 24000);
    writeWav(root.getChildFile("Drums").getChildFile("Kick 01.wav"), 1, 48000.0, 4800);
    REQUIRE(root.getChildFile("notes.txt").replaceWithText("not audio"));
    const auto never = [] { return false; };

    const auto listed = ui::MediaBrowser::list(root, root, {}, never);
    REQUIRE(listed.size() == 3);
    CHECK(listed[0].folder);
    CHECK(listed[0].name == "Drums");
    CHECK(listed[1].name == "A lead.wav");
    CHECK(listed[1].seconds == Approx(0.5));
    CHECK(listed[1].sampleRate == 48000.0);
    CHECK(listed[1].channels == 2);
    CHECK(listed[2].name == "b pad.wav");
    CHECK(listed[2].seconds == Approx(2.0));

    // A search looks under the whole place, every word must match, and says where.
    const auto found = ui::MediaBrowser::list(root, root, "kick 01", never);
    REQUIRE(found.size() == 1);
    CHECK(found[0].name == "Kick 01.wav");
    CHECK(found[0].detail == "Drums");
    CHECK(ui::MediaBrowser::list(root, root, "snare", never).empty());
    // A cancelled listing gives nothing.
    CHECK(ui::MediaBrowser::list(root, root, {}, [] { return true; }).empty());

    // In-app drags name the file.
    const auto file = root.getChildFile("A lead.wav");
    CHECK(ui::fileFromDragDescription(ui::fileDragDescription(file)) == file);
    CHECK(ui::fileFromDragDescription("plugin:northlight.eq") == juce::File());
    CHECK(ui::isAudioFile(file));
    CHECK_FALSE(ui::isAudioFile(root.getChildFile("notes.txt")));
    root.getParentDirectory().deleteRecursively();
}
