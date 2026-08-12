#pragma once
/*  PresetBrowser — a two-pane library (D-049).

    Waves-style: a category sidebar on the left (All · Recents · Favorites,
    then every category with its count), the presets of the selected
    category on the right. Search spans the whole library and switches the
    sidebar to All so results are never hidden behind a folder.

    Type to search, Return or double-click to load, Esc to leave. Audition
    keeps the sheet open so you can walk a category by ear. Saving is
    inline — no modals. */

#include "../VesperProcessor.h"
#include "Motion.h"
#include "Theme.h"
#include <set>

namespace vesper {

class PresetBrowser : public juce::Component
{
public:
    explicit PresetBrowser(VesperProcessor& procIn) : proc(procIn)
    {
        auto initEditor = [this] (juce::TextEditor& ed, const juce::String& hint)
        {
            ed.setTextToShowWhenEmpty(hint, theme::palette().textFaint);
            ed.setFont(theme::font(14.0f));
            addAndMakeVisible(ed);
        };

        initEditor(search, "Search all presets");
        search.setFont(theme::font(15.0f));
        search.onTextChange = [this]
        {
            if (search.getText().isNotEmpty()) selectedCat = 0; // All
            refresh();
        };
        search.setEscapeAndReturnKeysConsumed(true);
        search.onEscapeKey = [this] { dismiss(); };
        search.onReturnKey = [this] { loadRow(firstRow()); };

        tagFilter.setTextWhenNothingSelected("All tags");
        tagFilter.onChange = [this] { refresh(); };
        addAndMakeVisible(tagFilter);

        favToggle.setButtonText("Favs");
        favToggle.setClickingTogglesState(true);
        favToggle.setTooltip("Show favorites only");
        favToggle.onClick = [this] { refresh(); };
        addAndMakeVisible(favToggle);

        auditionBtn.setButtonText("Audition");
        auditionBtn.setClickingTogglesState(true);
        auditionBtn.setTooltip("Audition: click a preset to hear it without closing; "
                               "Return or double-click keeps it and closes");
        addAndMakeVisible(auditionBtn);

        catList.setModel(&catModel);
        catList.setRowHeight(30);
        catList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(catList);

        presetList.setModel(&presetModel);
        presetList.setRowHeight(34);
        presetList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(presetList);

        initEditor(saveName, "Preset name");
        initEditor(saveTags, "tags, comma, separated");
        saveBtn.setButtonText("Save");
        saveBtn.onClick = [this]
        {
            juce::StringArray tags;
            tags.addTokens(saveTags.getText(), ",", "");
            tags.trim();
            if (proc.presets.saveUserPreset(saveName.getText(), "User", tags))
            {
                saveName.clear();
                refreshTags();
                refresh();
            }
        };
        addAndMakeVisible(saveBtn);

        closeBtn.setButtonText("Close");
        closeBtn.onClick = [this] { dismiss(); };
        addAndMakeVisible(closeBtn);

        refreshTags();
        refresh();
    }

    std::function<void()> onClose;

    void open()
    {
        setVisible(true);
        toFront(true);
        if (getParentComponent() != nullptr)
        {
            const auto home = getBounds();
            tween.start(*this, home.translated(0, 24), home, 0.0f, 1.0f, motion::move);
        }
        proc.presets.rescan();
        refreshTags();
        selectCategoryOfCurrent();
        refresh();
        search.grabKeyboardFocus();
    }

    /* The header's "+" promises "save as user preset" — so land the user in
       the save-name field, not the search box (D-055 audit fix). */
    void openForSaving()
    {
        open();
        saveName.grabKeyboardFocus();
    }

    void dismiss()
    {
        const auto home = getBounds();
        tween.start(*this, home, home.translated(0, 16), 1.0f, 0.0f, motion::settle,
                    [this, home]
                    {
                        setBounds(home);
                        setAlpha(1.0f);
                        if (onClose) onClose();
                    });
    }

    void paint(juce::Graphics& g) override
    {
        auto& p = theme::palette();
        g.fillAll(p.night.withAlpha(0.985f));
        g.setColour(p.ink(0.07f));
        g.fillRect(0, 0, getWidth(), 1);

        auto r = content();
        auto titleRow = r.removeFromTop(44);
        g.setFont(theme::labelFont(12.0f));
        g.setColour(p.text);
        g.drawText("PRESETS", titleRow.removeFromLeft(90), juce::Justification::centredLeft);
        g.setColour(p.textFaint);
        g.setFont(theme::microFont(9.5f));
        g.drawText(juce::String(proc.presets.getAll().size()) + " in library",
                   titleRow.removeFromLeft(120), juce::Justification::centredLeft);

        // The two panes, each a contained Surface (D-022 containment)
        theme::drawSurface(g, sidebarRect.toFloat(), theme::radiusMd);
        theme::drawSurface(g, listRect.toFloat(), theme::radiusMd);

        if (shown.empty())
        {
            g.setColour(p.textFaint);
            g.setFont(theme::font(13.0f));
            g.drawText(search.getText().isNotEmpty() ? "No presets match that search"
                                                     : "Nothing here yet",
                       listRect, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = content();
        auto top = r.removeFromTop(44);
        closeBtn.setBounds(top.removeFromRight(64).reduced(0, 8));

        auto filters = r.removeFromTop(38);
        favToggle.setBounds(filters.removeFromRight(58).reduced(0, 5));
        filters.removeFromRight(theme::gap / 2);
        auditionBtn.setBounds(filters.removeFromRight(92).reduced(0, 5));
        filters.removeFromRight(theme::gap);
        tagFilter.setBounds(filters.removeFromRight(128).reduced(0, 5));
        filters.removeFromRight(theme::gap);
        search.setBounds(filters.reduced(0, 5));

        auto save = r.removeFromBottom(40);
        saveBtn.setBounds(save.removeFromRight(64).reduced(0, 6));
        save.removeFromRight(theme::gap);
        saveTags.setBounds(save.removeFromRight(juce::jmax(150, save.getWidth() / 2)).reduced(0, 6));
        save.removeFromRight(theme::gap);
        saveName.setBounds(save.reduced(0, 6));
        r.removeFromBottom(theme::gap / 2);
        r.removeFromTop(theme::gap / 2);

        // two panes: sidebar (fixed) | list (rest)
        sidebarRect = r.removeFromLeft(juce::jmax(150, r.getWidth() / 4));
        r.removeFromLeft(theme::gap);
        listRect = r;
        catList.setBounds(sidebarRect.reduced(6));
        presetList.setBounds(listRect.reduced(6));
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { dismiss(); return true; }
        if (k == juce::KeyPress::returnKey) { loadRow(firstRow()); return true; }
        return false;
    }

private:
    // ------------------------------------------------------- sidebar model --
    struct CategoryModel : juce::ListBoxModel
    {
        explicit CategoryModel(PresetBrowser& o) : owner(o) {}
        int getNumRows() override { return owner.categories.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override
        {
            if (row < 0 || row >= owner.categories.size()) return;
            auto& p = theme::palette();
            const bool sel = (row == owner.selectedCat);
            const auto name = owner.categories[row];

            if (sel)
            {
                g.setColour(p.accent.withAlpha(0.14f));
                g.fillRoundedRectangle(3.0f, 2.0f, (float) w - 6.0f, (float) h - 4.0f, theme::radiusXs);
                g.setColour(p.accent);
                g.fillRoundedRectangle(3.0f, 6.0f, 2.5f, (float) h - 12.0f, 1.25f); // active rule
            }
            g.setColour(sel ? p.text : p.textDim);
            g.setFont(sel ? theme::font(13.0f, true) : theme::font(13.0f));
            g.drawText(name, 14, 0, w - 52, h, juce::Justification::centredLeft);

            g.setColour(p.textFaint);
            g.setFont(theme::microFont(9.0f));
            g.drawText(juce::String(owner.countFor(name)), w - 40, 0, 26, h,
                       juce::Justification::centredRight);
        }

        void listBoxItemClicked(int row, const juce::MouseEvent&) override
        {
            if (row < 0 || row >= owner.categories.size()) return;
            owner.selectedCat = row;
            owner.refresh();
        }
        PresetBrowser& owner;
    };

    // -------------------------------------------------------- preset model --
    struct PresetModel : juce::ListBoxModel
    {
        explicit PresetModel(PresetBrowser& o) : owner(o) {}
        int getNumRows() override { return (int) owner.shown.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (row < 0 || row >= (int) owner.shown.size()) return;
            auto& p = theme::palette();
            const auto& info = owner.shown[(size_t) row];
            const float cy = h * 0.5f;
            const juce::String sep (juce::CharPointer_UTF8(" \xc2\xb7 "));

            if (selected)
            {
                g.setColour(p.ink(0.06f));
                g.fillRoundedRectangle(2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, theme::radiusXs);
            }
            theme::drawGlowDot(g, { 20.0f, cy }, 2.5f,
                               info.favorite ? p.accent : p.ink(0.18f), 0.0f);
            g.setColour(selected ? p.text : p.textDim);
            g.setFont(theme::font(14.0f));
            g.drawText(info.name, 38, 0, w * 5 / 10, h, juce::Justification::centredLeft);

            if (! info.tags.isEmpty())
            {
                g.setColour(p.textFaint);
                g.setFont(theme::font(11.5f));
                g.drawText(info.tags.joinIntoString(sep), w / 2, 0, w / 2 - 14, h,
                           juce::Justification::centredRight);
            }
        }

        void listBoxItemClicked(int row, const juce::MouseEvent& e) override
        {
            if (row < 0 || row >= (int) owner.shown.size()) return;
            if (e.x < 34) // favorite column
            {
                auto& info = owner.shown[(size_t) row];
                owner.proc.presets.setFavorite(info, ! info.favorite);
                info.favorite = ! info.favorite;
                owner.presetList.repaintRow(row);
                return;
            }
            if (owner.auditionBtn.getToggleState())
            {
                owner.proc.presets.loadPreset(owner.shown[(size_t) row]);
                owner.presetList.selectRow(row);
            }
        }
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
        {
            owner.loadRow(row);
        }
        PresetBrowser& owner;
    };

    juce::Rectangle<int> content() const
    {
        return getLocalBounds().reduced(juce::jmax(5 * theme::unit, getWidth() / 10),
                                        juce::jmax(4 * theme::unit, getHeight() / 12));
    }

    static juce::String categoryOf(const PresetInfo& i)
    {
        return i.category.isEmpty() ? juce::String("Other") : i.category;
    }

    int countFor(const juce::String& cat) const
    {
        const auto all = proc.presets.search({}, {}, false);
        if (cat == "All") return (int) all.size();
        if (cat == "Favorites")
        {
            int n = 0;
            for (const auto& i : all) if (i.favorite) ++n;
            return n;
        }
        if (cat == "Recents") return proc.presets.recents().size();
        int n = 0;
        for (const auto& i : all) if (categoryOf(i) == cat) ++n;
        return n;
    }

    int firstRow() const
    {
        const int sel = presetList.getSelectedRow();
        if (sel >= 0 && sel < (int) shown.size()) return sel;
        return shown.empty() ? -1 : 0;
    }

    void loadRow(int row)
    {
        if (row < 0 || row >= (int) shown.size()) return;
        proc.presets.loadPreset(shown[(size_t) row]);
        dismiss();
    }

    void refreshTags()
    {
        const auto sel = tagFilter.getText();
        tagFilter.clear(juce::dontSendNotification);
        int id = 1;
        tagFilter.addItem("All tags", id++);
        for (const auto& t : proc.presets.allTags())
            tagFilter.addItem(t, id++);
        if (sel.isNotEmpty()) tagFilter.setText(sel, juce::dontSendNotification);
    }

    /* Arrive on the category holding the loaded preset. */
    void selectCategoryOfCurrent()
    {
        const auto current = proc.presets.getCurrentName();
        if (current.isEmpty()) return;
        rebuildCategories();
        for (const auto& info : proc.presets.search({}, {}, false))
            if (info.name == current)
            {
                const int idx = categories.indexOf(categoryOf(info));
                if (idx >= 0) selectedCat = idx;
                return;
            }
    }

    void rebuildCategories()
    {
        const auto all = proc.presets.search({}, {}, false);
        std::set<juce::String> cats;
        for (const auto& i : all) cats.insert(categoryOf(i));

        categories.clear();
        categories.add("All");
        if (! proc.presets.recents().isEmpty()) categories.add("Recents");
        categories.add("Favorites");
        for (const auto& c : cats) categories.add(c);
        selectedCat = juce::jlimit(0, juce::jmax(0, categories.size() - 1), selectedCat);
    }

    void refresh()
    {
        rebuildCategories();

        const auto tag = tagFilter.getSelectedId() <= 1 ? juce::String() : tagFilter.getText();
        auto found = proc.presets.search(search.getText(), tag, favToggle.getToggleState());

        const auto cat = categories.isEmpty() ? juce::String("All")
                                              : categories[selectedCat];
        shown.clear();
        if (cat == "Recents")
        {
            for (const auto& name : proc.presets.recents())
                for (const auto& i : found)
                    if (i.name == name) { shown.push_back(i); break; }
        }
        else
        {
            for (const auto& i : found)
                if (cat == "All"
                    || (cat == "Favorites" && i.favorite)
                    || categoryOf(i) == cat)
                    shown.push_back(i);
            std::sort(shown.begin(), shown.end(),
                      [] (const PresetInfo& a, const PresetInfo& b)
                      { return a.name.compareIgnoreCase(b.name) < 0; });
        }

        catList.updateContent();
        catList.repaint();
        presetList.updateContent();
        presetList.repaint();
        repaint();
    }

    VesperProcessor& proc;
    juce::TextEditor search, saveName, saveTags;
    juce::ComboBox tagFilter;
    juce::TextButton favToggle, auditionBtn, saveBtn, closeBtn;

    CategoryModel catModel { *this };
    PresetModel   presetModel { *this };
    juce::ListBox catList, presetList;

    juce::StringArray categories;
    int selectedCat = 0;
    std::vector<PresetInfo> shown;
    juce::Rectangle<int> sidebarRect, listRect;
    motion::Tween tween;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};

} // namespace vesper
