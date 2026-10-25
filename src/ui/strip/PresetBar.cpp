#include "ui/strip/PresetBar.h"

#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/Sheet.h"

namespace northlight::ui::strip
{

// ----- shared by every preset control -------------------------------------------------------------

void showPresetMenu(juce::Component& target,
                    const Theme& theme,
                    const std::string& type,
                    const std::vector<float>& params,
                    std::function<void(int index)> apply)
{
    auto& library = EffectPresetLibrary::instance();
    library.reload(type);
    const auto& list = library.presets(type);
    const int current = library.match(type, params);
    juce::PopupMenu menu;
    bool yoursHeader = false;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].user && !yoursHeader)
        {
            menu.addSectionHeader("Yours");
            yoursHeader = true;
        }
        menu.addItem(static_cast<int>(i) + 1, list[i].name, true, static_cast<int>(i) == current);
    }
    menu.addSeparator();
    constexpr int save = 10001, remove = 10002, folder = 10003;
    menu.addItem(save, juce::String::fromUTF8("Save preset\xe2\x80\xa6"));
    const bool canDelete = current >= 0 && list[static_cast<size_t>(current)].user;
    menu.addItem(remove,
                 canDelete ? "Delete \"" + list[static_cast<size_t>(current)].name + "\""
                           : juce::String("Delete preset"),
                 canDelete);
    menu.addItem(folder, "Show presets folder");
    juce::Component::SafePointer<juce::Component> safe(&target);
    const auto entry = canDelete ? list[static_cast<size_t>(current)] : PresetEntry{};
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                       [safe, &theme, type, params, entry, apply = std::move(apply)](int choice)
                       {
                           if (safe == nullptr || choice <= 0)
                               return;
                           auto& lib = EffectPresetLibrary::instance();
                           if (choice == save)
                               showSavePresetSheet(*safe, theme, type, params);
                           else if (choice == remove)
                               lib.remove(type, entry);
                           else if (choice == folder)
                           {
                               const auto dir = lib.folderFor(type);
                               dir.createDirectory();
                               dir.revealToUser();
                           }
                           else if (apply)
                               apply(choice - 1);
                       });
}

void showSavePresetSheet(juce::Component& from,
                         const Theme& theme,
                         const std::string& type,
                         const std::vector<float>& params)
{
    auto* window = from.getTopLevelComponent();
    if (window == nullptr)
        return;
    auto& library = EffectPresetLibrary::instance();
    const int current = library.match(type, params);
    auto field = std::make_unique<juce::TextEditor>();
    field->setText(current >= 0 ? library.presets(type)[static_cast<size_t>(current)].name
                                : juce::String("My preset"));
    field->selectAll();
    auto* raw = field.get();
    Sheet::show(
        *window,
        theme,
        "Save preset",
        "The effect's settings as they are now. It appears under Yours in the presets list; a preset "
        "of yours with the same name is replaced.",
        {{"Cancel", false, false}, {"Save", true, false}},
        [raw, type, params](int result)
        {
            if (result == 1)
                EffectPresetLibrary::instance().save(type, raw->getText(), params);
        },
        std::move(field),
        theme.sizes.control);
}

// ----- the bar ------------------------------------------------------------------------------------

PresetBar::PresetBar(const Theme& theme, std::string type, bool inFace)
    : theme_(theme), type_(std::move(type)), inFace_(inFace),
      previous_(juce::String::fromUTF8("\xe2\x80\xb9")), next_(juce::String::fromUTF8("\xe2\x80\xba"))
{
    for (auto* b : {&previous_, &next_, &name_})
    {
        b->getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
        addAndMakeVisible(b);
    }
    if (!inFace_)
        name_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    previous_.setTooltip("Previous preset");
    next_.setTooltip("Next preset");
    name_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    previous_.onClick = [this] { step(-1); };
    next_.onClick = [this] { step(1); };
    name_.onClick = [this] { showMenu(); };
    EffectPresetLibrary::instance().addChangeListener(this);
    update();
}

PresetBar::~PresetBar()
{
    EffectPresetLibrary::instance().removeChangeListener(this);
}

void PresetBar::setValues(const std::vector<float>& params)
{
    if (params == params_)
        return;
    params_ = params;
    update();
}

void PresetBar::update()
{
    auto& library = EffectPresetLibrary::instance();
    current_ = library.match(type_, params_);
    const auto& list = library.presets(type_);
    const auto name = current_ >= 0 ? list[static_cast<size_t>(current_)].name : juce::String("Custom");
    name_.setButtonText(name);
    name_.setTooltip("Presets");
    const bool any = !list.empty();
    previous_.setEnabled(any);
    next_.setEnabled(any);
    repaint();
}

void PresetBar::step(int direction)
{
    const auto& list = EffectPresetLibrary::instance().presets(type_);
    const int n = static_cast<int>(list.size());
    if (n == 0)
        return;
    const int next = current_ < 0 ? (direction > 0 ? 0 : n - 1) : (current_ + direction + n) % n;
    apply(next);
}

void PresetBar::apply(int index)
{
    const auto& list = EffectPresetLibrary::instance().presets(type_);
    if (index < 0 || static_cast<size_t>(index) >= list.size())
        return;
    // Copies: applying sets the model, whose refresh comes back through setValues.
    const auto values = list[static_cast<size_t>(index)].values;
    const auto name = list[static_cast<size_t>(index)].name;
    if (onApply)
        onApply(values, name);
    setValues(values);
}

void PresetBar::showMenu()
{
    juce::Component::SafePointer<PresetBar> safe(this);
    showPresetMenu(name_,
                   theme_,
                   type_,
                   params_,
                   [safe](int index)
                   {
                       if (safe != nullptr)
                           safe->apply(index);
                   });
}

void PresetBar::paint(juce::Graphics& g)
{
    if (inFace_)
        return; // the face draws the well
    const auto& c = theme_.colours;
    const auto r = getLocalBounds().toFloat();
    g.setColour(c.surface0.withAlpha(0.8f));
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    g.setColour(c.lineSubtle);
    g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    const int arrow = getHeight();
    previous_.setBounds(r.removeFromLeft(arrow));
    next_.setBounds(r.removeFromRight(arrow));
    name_.setBounds(r.reduced(2, 0));
}

} // namespace northlight::ui::strip
