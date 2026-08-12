#include "PresetManager.h"
#include "Params.h"
#include "BinaryData.h"

namespace vesper {

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& state, juce::AudioProcessor& proc)
    : apvts(state), processor(proc)
{
    ensureFactoryPresets();
    loadFavorites();
    loadRecents();
    rescan();
}

juce::File PresetManager::presetRoot()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sable Audio").getChildFile("Vesper").getChildFile("Presets");
}

void PresetManager::ensureFactoryPresets()
{
    // Bump when the embedded factory content changes: stale on-disk factory
    // presets are then refreshed (rev 2: stability + category audit,
    // 2026-07-18). User presets are never touched.
    // 3 (D-040): "808 Glue & Grit" and "Loud & Clear" shipped with a raw '&'
    // in the XML name attribute — malformed, so both silently failed to parse
    // and never appeared in the browser. Bumped so existing installs refresh.
    // 4 (D-047): 18 Showcase presets added and Acid Line retuned after the
    // filter level fix — installed copies must refresh to pick both up.
    constexpr int factoryRevision = 5; // D-058: author field -> Sable Audio

    auto factoryDir = presetRoot().getChildFile("Factory");
    auto userDir    = presetRoot().getChildFile("User");
    factoryDir.createDirectory();
    userDir.createDirectory();

    auto marker = factoryDir.getChildFile("factory.rev");
    const bool stale = marker.loadFileAsString().trim().getIntValue() != factoryRevision;

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const auto original = juce::String(BinaryData::originalFilenames[i]);
        if (! original.endsWith(".vpreset")) continue;

        auto target = factoryDir.getChildFile(original);
        if (! stale && target.existsAsFile()) continue; // respect user pruning within a revision

        int size = 0;
        if (const auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size))
            target.replaceWithData(data, (size_t) size);
    }
    if (stale)
        marker.replaceWithText(juce::String(factoryRevision));
}

// ---------------------------------------------------------------- scanning --
void PresetManager::rescan()
{
    list.clear();
    auto scanDir = [this] (const juce::File& dir, bool isFactory)
    {
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.vpreset"))
        {
            if (auto xml = juce::parseXML(f))
            {
                if (! xml->hasTagName("VesperPreset")) continue;
                PresetInfo info;
                info.file     = f;
                info.name     = xml->getStringAttribute("name", f.getFileNameWithoutExtension());
                info.category = xml->getStringAttribute("category", "Uncategorized");
                info.tags.addTokens(xml->getStringAttribute("tags"), ",", "");
                info.tags.trim();
                info.factory  = isFactory;
                info.favorite = favorites.contains(info.name);
                list.push_back(std::move(info));
            }
        }
    };
    scanDir(presetRoot().getChildFile("Factory"), true);
    scanDir(presetRoot().getChildFile("User"), false);

    std::sort(list.begin(), list.end(), [] (const PresetInfo& a, const PresetInfo& b)
              { return a.name.compareIgnoreCase(b.name) < 0; });
}

std::vector<PresetInfo> PresetManager::search(const juce::String& query,
                                              const juce::String& tagFilter,
                                              bool favoritesOnly) const
{
    std::vector<PresetInfo> out;
    const auto q = query.trim().toLowerCase();
    for (const auto& p : list)
    {
        if (favoritesOnly && ! p.favorite) continue;
        if (tagFilter.isNotEmpty() && ! p.tags.contains(tagFilter, true)
            && ! p.category.equalsIgnoreCase(tagFilter)) continue;
        if (q.isNotEmpty())
        {
            const bool hit = p.name.toLowerCase().contains(q)
                          || p.category.toLowerCase().contains(q)
                          || p.tags.joinIntoString(",").toLowerCase().contains(q);
            if (! hit) continue;
        }
        out.push_back(p);
    }
    return out;
}

juce::StringArray PresetManager::allTags() const
{
    juce::StringArray tags;
    for (const auto& p : list)
    {
        tags.addIfNotAlreadyThere(p.category);
        for (const auto& t : p.tags) tags.addIfNotAlreadyThere(t);
    }
    tags.sortNatural();
    return tags;
}

// ----------------------------------------------------------------- loading --
void PresetManager::applyPresetXml(const juce::XmlElement& xml)
{
    auto& um = *apvts.undoManager;
    um.beginNewTransaction("Load preset");

    // 1) everything to defaults
    for (auto* param : processor.getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            withID->setValueNotifyingHost(withID->getDefaultValue());

    // 2) stored values (plain, not normalized)
    for (auto* e : xml.getChildWithTagNameIterator("PARAM"))
    {
        const auto id = e->getStringAttribute("id");
        if (auto* param = apvts.getParameter(id))
            param->setValueNotifyingHost(
                param->convertTo0to1((float) e->getDoubleAttribute("value")));
    }

    // 3) chain order (empty/invalid falls back to the default order downstream)
    const auto* chain = xml.getChildByName("CHAIN");
    apvts.state.setProperty("chainOrder",
                            chain != nullptr ? chain->getStringAttribute("order")
                                             : juce::String(),
                            nullptr);
    if (onStateReloaded) onStateReloaded();
}

void PresetManager::loadPreset(const PresetInfo& info)
{
    if (auto xml = juce::parseXML(info.file))
    {
        applyPresetXml(*xml);
        currentName = info.name;
        pushRecent(info.name);
    }
}

void PresetManager::loadNext(bool forward)
{
    if (list.empty()) return;
    int idx = 0;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == currentName) { idx = i; break; }
    idx = (idx + (forward ? 1 : -1) + (int) list.size()) % (int) list.size();
    loadPreset(list[(size_t) idx]);
}

void PresetManager::loadInit()
{
    juce::XmlElement empty("VesperPreset");
    applyPresetXml(empty);
    currentName = "Init";
}

// ------------------------------------------------------------------ saving --
bool PresetManager::saveUserPreset(const juce::String& name, const juce::String& category,
                                   const juce::StringArray& tags)
{
    if (name.trim().isEmpty()) return false;

    juce::XmlElement xml("VesperPreset");
    xml.setAttribute("name", name.trim());
    xml.setAttribute("author", juce::SystemStats::getFullUserName());
    xml.setAttribute("category", category.isEmpty() ? "User" : category);
    xml.setAttribute("tags", tags.joinIntoString(","));
    xml.setAttribute("version", "1.0.0");

    for (auto* param : processor.getParameters())
    {
        auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*>(param);
        if (withID == nullptr) continue;
        if (std::abs(withID->getValue() - withID->getDefaultValue()) < 1.0e-5f) continue;

        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
        auto* e = xml.createNewChildElement("PARAM");
        e->setAttribute("id", withID->paramID);
        e->setAttribute("value", ranged != nullptr
                                     ? (double) ranged->convertFrom0to1(ranged->getValue())
                                     : (double) withID->getValue());
    }

    auto* chain = xml.createNewChildElement("CHAIN");
    chain->setAttribute("order", apvts.state.getProperty("chainOrder").toString());

    auto file = presetRoot().getChildFile("User")
                    .getChildFile(juce::File::createLegalFileName(name.trim()) + ".vpreset");
    const bool ok = xml.writeTo(file);
    if (ok) { currentName = name.trim(); rescan(); }
    return ok;
}

// --------------------------------------------------------------- favorites --
void PresetManager::setFavorite(const PresetInfo& info, bool fav)
{
    if (fav) favorites.addIfNotAlreadyThere(info.name);
    else     favorites.removeString(info.name);
    saveFavorites();
    for (auto& p : list)
        if (p.name == info.name) p.favorite = fav;
}

void PresetManager::loadFavorites()
{
    auto f = presetRoot().getChildFile("favorites.json");
    if (! f.existsAsFile()) return;
    const auto parsed = juce::JSON::parse(f);
    if (auto* arr = parsed.getArray())
        for (const auto& v : *arr)
            favorites.addIfNotAlreadyThere(v.toString());
}

void PresetManager::saveFavorites()
{
    juce::Array<juce::var> arr;
    for (const auto& n : favorites) arr.add(n);
    presetRoot().getChildFile("favorites.json")
        .replaceWithText(juce::JSON::toString(juce::var(arr)));
}

void PresetManager::loadRecents()
{
    auto f = presetRoot().getChildFile("recents.json");
    if (! f.existsAsFile()) return;
    const auto parsed = juce::JSON::parse(f);
    if (auto* arr = parsed.getArray())
        for (const auto& v : *arr)
            if (recentNames.size() < maxRecents)
                recentNames.addIfNotAlreadyThere(v.toString());
}

void PresetManager::pushRecent(const juce::String& name)
{
    if (name.isEmpty() || name == "Init") return;
    recentNames.removeString(name);   // moves an existing entry to the front
    recentNames.insert(0, name);
    while (recentNames.size() > maxRecents) recentNames.remove(recentNames.size() - 1);

    juce::Array<juce::var> arr;
    for (const auto& n : recentNames) arr.add(n);
    presetRoot().getChildFile("recents.json")
        .replaceWithText(juce::JSON::toString(juce::var(arr)));
}

} // namespace vesper
