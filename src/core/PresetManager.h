#pragma once
/*  PresetManager — file-based presets with search, tags and favorites.

    Format (.vpreset, XML): only non-default parameters are stored, so factory
    presets stay tiny, hand-editable, and forward-compatible — unknown ids are
    ignored, missing ids fall back to defaults.

    Factory presets ship inside the binary (BinaryData) and are written to
    disk on first run; user presets live next to them:
        <userAppData>/Sable Audio/Vesper/Presets/{Factory,User}
    Favorites are a lightweight JSON sidecar, never written into the preset
    files themselves (so factory content stays pristine). */

#include <juce_audio_processors/juce_audio_processors.h>

namespace vesper {

struct PresetInfo
{
    juce::File   file;
    juce::String name, category;
    juce::StringArray tags;
    bool factory  = false;
    bool favorite = false;
};

class PresetManager
{
public:
    PresetManager(juce::AudioProcessorValueTreeState& state, juce::AudioProcessor& proc);

    // ------------------------------------------------------------- loading
    void loadPreset(const PresetInfo& info);
    void loadNext(bool forward);
    void loadInit();

    // -------------------------------------------------------------- saving
    bool saveUserPreset(const juce::String& name, const juce::String& category,
                        const juce::StringArray& tags);

    // ------------------------------------------------------------ browsing
    void rescan();
    const std::vector<PresetInfo>& getAll() const { return list; }
    std::vector<PresetInfo> search(const juce::String& query,
                                   const juce::String& tagFilter,
                                   bool favoritesOnly) const;
    juce::StringArray allTags() const;

    void setFavorite(const PresetInfo& info, bool fav);

    /* Most-recently-loaded preset names, newest first (D-042). Persisted
       alongside favorites; capped so it stays a shortlist. */
    const juce::StringArray& recents() const { return recentNames; }

    juce::String getCurrentName() const { return currentName; }
    void setCurrentName(const juce::String& n) { currentName = n; }

    /* Fired after a preset (or Init) fully replaces the state — the processor
       uses this to re-pack the lock-free chain order. */
    std::function<void()> onStateReloaded;

    static juce::File presetRoot();

private:
    void ensureFactoryPresets();
    void loadFavorites();
    void saveFavorites();
    void loadRecents();
    void pushRecent(const juce::String& name);
    void applyPresetXml(const juce::XmlElement& xml);

    static constexpr int maxRecents = 8;

    juce::AudioProcessorValueTreeState& apvts;
    juce::AudioProcessor& processor;
    std::vector<PresetInfo> list;
    juce::StringArray favorites;   // preset names
    juce::StringArray recentNames; // newest first, D-042
    juce::String currentName { "Init" };
};

} // namespace vesper
