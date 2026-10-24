#include "ui/Theme.h"
#include "ui/shell/Panels.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

TEST_CASE("The dock never shrinks below what a panel shown in it needs", "[ui][panels]")
{
    const auto& theme = ui::Theme::dark();
    ui::PanelHost dock(theme, ui::HostKind::dock), side(theme, ui::HostKind::side);
    ui::PanelManager panels(theme, dock, side);
    juce::Component editor;
    int needed = 500;
    ui::PanelInfo info{ui::PanelId::editor, "Editor", ui::Icon::plugin, ui::HostKind::dock};
    info.minimumHeight = [&needed] { return needed; };
    panels.registerPanel(info, editor);

    // Hidden, the panel asks nothing of the dock.
    panels.setDockHeight(100);
    CHECK(panels.dockOpenHeight() == theme.sizes.dockMin);

    panels.show(ui::PanelId::editor);
    CHECK(panels.dockMinimum() == dock.headerHeight() + 500);
    CHECK(panels.dockOpenHeight() == dock.headerHeight() + 500);
    panels.setDockHeight(100);
    CHECK(panels.dockOpenHeight() == dock.headerHeight() + 500);
    panels.setDockHeight(900);
    CHECK(panels.dockOpenHeight() == 900);

    // What it shows grows past the dock: the dock grows with it.
    needed = 1000;
    panels.enforceDockMinimum();
    CHECK(panels.dockOpenHeight() == dock.headerHeight() + 1000);
}
