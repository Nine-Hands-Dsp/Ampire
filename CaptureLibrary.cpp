#include "CaptureLibrary.h"

juce::File CaptureLibrary::settingsDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Nine Hands DSP").getChildFile ("Ampire");
}

namespace
{
    juce::String keyOf (const juce::File& f)
    {
        return f.getFullPathName() + "|" + juce::String (f.getSize()) + "|" + juce::String (f.getLastModificationTime().toMilliseconds());
    }
    juce::File cacheFile() { return CaptureLibrary::settingsDir().getChildFile ("library_cache.xml"); }
    juce::File listsFile() { return CaptureLibrary::settingsDir().getChildFile ("library.xml"); }

    void metaToXml (const CaptureMeta& m, juce::XmlElement& e)
    {
        e.setAttribute ("path", m.path);         e.setAttribute ("file", m.fileName);
        e.setAttribute ("title", m.title);       e.setAttribute ("arch", m.architecture);
        e.setAttribute ("family", m.family);     e.setAttribute ("type", m.gearType);
        e.setAttribute ("make", m.gearMake);     e.setAttribute ("model", m.gearModel);
        e.setAttribute ("tone", m.toneType);     e.setAttribute ("by", m.modeledBy);
        e.setAttribute ("rate", m.sampleRate);   e.setAttribute ("kb", m.fileKB);
        e.setAttribute ("a2", m.isA2 ? 1 : 0);
        if (m.hasInputLevel) e.setAttribute ("inLevel", m.inputLevel);
        if (m.hasLoudness) e.setAttribute ("loud", m.loudness);
    }
    CaptureMeta metaFromXml (const juce::XmlElement& e)
    {
        CaptureMeta m;
        m.path = e.getStringAttribute ("path");    m.fileName = e.getStringAttribute ("file");
        m.title = e.getStringAttribute ("title");  m.architecture = e.getStringAttribute ("arch");
        m.family = e.getStringAttribute ("family"); m.gearType = e.getStringAttribute ("type");
        m.gearMake = e.getStringAttribute ("make"); m.gearModel = e.getStringAttribute ("model");
        m.toneType = e.getStringAttribute ("tone"); m.modeledBy = e.getStringAttribute ("by");
        m.sampleRate = e.getDoubleAttribute ("rate", 48000.0); m.fileKB = e.getDoubleAttribute ("kb");
        m.isA2 = e.getIntAttribute ("a2") != 0;
        m.hasInputLevel = e.hasAttribute ("inLevel"); m.inputLevel = e.getDoubleAttribute ("inLevel");
        m.hasLoudness = e.hasAttribute ("loud");      m.loudness = e.getDoubleAttribute ("loud");
        return m;
    }
}

CaptureLibrary::CaptureLibrary() : juce::Thread ("Ampire library")
{
    loadLists();
}

CaptureLibrary::~CaptureLibrary()
{
    stopThread (4000);
}

juce::File CaptureLibrary::getRoot() const { const juce::ScopedLock sl (lock); return root; }

void CaptureLibrary::setRoot (const juce::File& dir)
{
    { const juce::ScopedLock sl (lock); root = dir; }
    saveLists();
    rescan();
}

void CaptureLibrary::rescan()
{
    stopThread (4000);
    if (getRoot().isDirectory())
    {
        scanning = true;
        startThread (juce::Thread::Priority::low);
    }
    else
    {
        { const juce::ScopedLock sl (lock); entries.clear(); }
        version++;
    }
}

juce::Array<CaptureMeta> CaptureLibrary::getEntries() const { const juce::ScopedLock sl (lock); return entries; }

bool CaptureLibrary::isFavourite (const juce::String& path) const { const juce::ScopedLock sl (lock); return favourites.contains (path); }

void CaptureLibrary::toggleFavourite (const juce::String& path)
{
    {
        const juce::ScopedLock sl (lock);
        if (favourites.contains (path)) favourites.removeString (path);
        else favourites.add (path);
    }
    saveLists();
    version++;
}

juce::StringArray CaptureLibrary::getRecents() const { const juce::ScopedLock sl (lock); return recents; }

void CaptureLibrary::addRecent (const juce::String& path)
{
    {
        const juce::ScopedLock sl (lock);
        recents.removeString (path);
        recents.insert (0, path);
        while (recents.size() > 30) recents.remove (recents.size() - 1);
    }
    saveLists();
    version++;
}

void CaptureLibrary::loadLists()
{
    if (auto x = juce::XmlDocument::parse (listsFile()))
    {
        const juce::ScopedLock sl (lock);
        root = juce::File (x->getStringAttribute ("root"));
        favourites.clear(); recents.clear();
        for (auto* e : x->getChildWithTagNameIterator ("FAV")) favourites.add (e->getStringAttribute ("path"));
        for (auto* e : x->getChildWithTagNameIterator ("RECENT")) recents.add (e->getStringAttribute ("path"));
    }
}

void CaptureLibrary::saveLists()
{
    juce::XmlElement x ("AmpireLibrary");
    {
        const juce::ScopedLock sl (lock);
        x.setAttribute ("root", root.getFullPathName());
        for (auto& f : favourites) x.createNewChildElement ("FAV")->setAttribute ("path", f);
        for (auto& r : recents)    x.createNewChildElement ("RECENT")->setAttribute ("path", r);
    }
    settingsDir().createDirectory();
    x.writeTo (listsFile());
}

void CaptureLibrary::loadCache()
{
    cache.clear();
    if (auto x = juce::XmlDocument::parse (cacheFile()))
        for (auto* e : x->getChildWithTagNameIterator ("C"))
            cache.set (e->getStringAttribute ("key"), metaFromXml (*e));
}

void CaptureLibrary::saveCache (const juce::Array<CaptureMeta>& list)
{
    juce::XmlElement x ("AmpireLibraryCache");
    for (auto& m : list)
    {
        auto* e = x.createNewChildElement ("C");
        e->setAttribute ("key", keyOf (juce::File (m.path)));
        metaToXml (m, *e);
    }
    settingsDir().createDirectory();
    x.writeTo (cacheFile());
}

void CaptureLibrary::run()
{
    scanning = true;
    scanCount = 0;
    loadCache();
    const auto dir = getRoot();
    juce::Array<CaptureMeta> found;
    int sinceUpdate = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (dir, true, "*.nam", juce::File::findFiles))
    {
        if (threadShouldExit()) { scanning = false; return; }
        const auto f = entry.getFile();
        const auto key = keyOf (f);
        CaptureMeta m;
        if (cache.contains (key)) m = cache[key];
        else if (! CaptureBuilder::readMeta (f, m)) continue;
        found.add (m);
        scanCount = found.size();
        if (++sinceUpdate >= 200)
        {
            sinceUpdate = 0;
            { const juce::ScopedLock sl (lock); entries = found; }
            version++;
        }
    }
    std::sort (found.begin(), found.end(), [] (const CaptureMeta& a, const CaptureMeta& b)
               { return a.path.compareNatural (b.path) < 0; });
    { const juce::ScopedLock sl (lock); entries = found; }
    saveCache (found);
    scanning = false;
    version++;
}
