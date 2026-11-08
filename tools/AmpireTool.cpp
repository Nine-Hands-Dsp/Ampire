// Outil de verification Linux : captures PNG de l'interface + rendu audio hors ligne + mesures.
//   AmpireTool render <out.png> <scale> [namA] [namB] [-] [instrument 0/1] [library 0/1]
//   AmpireTool process <in.wav|sine> <out.wav> <rate> <block> <namA> [namB] [series 0/1] [os 0/1/2] [size 0/1]
//   AmpireTool bench <nam> <rate> <block> <os> <size>
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include "../ColourPicker.h"

static void setP (AmpireProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) return 1;
    const juce::String mode (argv[1]);
    auto arg = [&] (int i) { return i < argc ? juce::String (argv[i]) : juce::String(); };

    if (mode == "render")
    {
        AmpireProcessor proc;
        const float scale = arg (3).isNotEmpty() ? arg (3).getFloatValue() : 2.0f;
        proc.prepareToPlay (48000.0, 256);
        if (arg (4).isNotEmpty()) proc.loadCapture (0, juce::File (arg (4)));
        if (arg (5).isNotEmpty() && arg (5) != "-") proc.loadCapture (1, juce::File (arg (5)));
        proc.waitForLoads (20000);
        setP (proc, "mix", -20.0f);
        // un peu de signal pour les vu-metres
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer mb;
        for (int k = 0; k < 40; ++k)
        {
            for (int i = 0; i < 256; ++i) { const float v = 0.3f * std::sin ((float) (k * 256 + i) * 0.05f); buf.setSample (0, i, v); buf.setSample (1, i, v); }
            proc.processBlock (buf, mb);
        }
        if (arg (9) == "0") setP (proc, "active", 0.0f);     // rendu POWER OFF
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        if (arg (8) == "1") if (auto* ae = dynamic_cast<AmpireEditor*> (ed.get())) ae->openLibrary (0);
        if (arg (10).isNotEmpty()) if (auto* ae = dynamic_cast<AmpireEditor*> (ed.get())) ae->previewStackButton (arg (10) != "na", arg (10) == "on");   // apparence du bouton IMPULSE
        // signal entretenu pendant l'affichage : vu-metres allumes sur la capture
        for (int k = 0; k < 24; ++k)
        {
            for (int i = 0; i < 256; ++i) { const float v = 0.5f * std::sin ((float) (k * 256 + i) * 0.05f); buf.setSample (0, i, v); buf.setSample (1, i, v); }
            proc.processBlock (buf, mb);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        juce::File out (arg (2));
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("editor %d x %d -> %s\n", ed->getWidth(), ed->getHeight(), out.getFullPathName().toRawUTF8());
        for (int s = 0; s < 2; ++s)
        {
            const auto si = proc.getSlotInfo (s);
            std::printf ("slot %d: loaded=%d err=%s fmt=%s\n", s, (int) si.loaded, si.error.toRawUTF8(), si.meta.formatLine().toRawUTF8());
        }
        ed.reset();
        return 0;
    }

    if (mode == "process" || mode == "bench")
    {
        const bool bench = mode == "bench";
        const double rate = (bench ? arg (3) : arg (4)).getDoubleValue();
        const int block = (bench ? arg (4) : arg (5)).getIntValue();
        AmpireProcessor proc;
        setP (proc, "oversample", (bench ? arg (5) : arg (9)).isNotEmpty() ? (bench ? arg (5) : arg (9)).getFloatValue() : 1.0f);
        setP (proc, "size", 1.0f - (bench ? arg (6) : arg (10)).getFloatValue());
        proc.setPlayConfigDetails (2, 2, rate, block);
        proc.prepareToPlay (rate, block);
        proc.loadCaptureNow (0, juce::File (bench ? arg (2) : arg (6)));
        
        proc.prepareToPlay (rate, block);      // voix construites a la bonne frequence
        const auto si = proc.getSlotInfo (0);
        std::printf ("A: loaded=%d %s | os=%d err=%s\n", (int) si.loaded, si.meta.formatLine().toRawUTF8(), si.osFactor, si.error.toRawUTF8());

        // signal : fichier wav ou guitare synthetique (corde pincee)
        juce::AudioBuffer<float> input;
        const juce::String src = bench ? juce::String ("sine") : arg (2);
        if (src != "sine")
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (src)));
            if (r == nullptr) { std::printf ("cannot read input\n"); return 2; }
            input.setSize (1, (int) r->lengthInSamples);
            r->read (&input, 0, (int) r->lengthInSamples, 0, true, false);
        }
        else
        {
            const int n = (int) (rate * (bench ? 4.0 : 3.0));
            input.setSize (1, n);
            juce::Random rnd (1);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / rate, tn = std::fmod (t, 0.75);
                double v = 0.0;
                for (int h = 1; h <= 8; ++h) v += std::sin (juce::MathConstants<double>::twoPi * 82.41 * h * t) / h * std::exp (-tn * (2.0 + h));
                input.setSample (0, i, (float) (0.25 * v));
            }
        }
        const int n = input.getNumSamples();
        juce::AudioBuffer<float> out (2, n);
        juce::AudioBuffer<float> io (2, block);
        juce::MidiBuffer mb;
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int pos = 0; pos < n; pos += block)
        {
            const int len = juce::jmin (block, n - pos);
            io.setSize (2, len, false, false, true);
            io.copyFrom (0, 0, input, 0, pos, len);
            io.copyFrom (1, 0, input, 0, pos, len);
            proc.processBlock (io, mb);
            out.copyFrom (0, pos, io, 0, 0, len);
            out.copyFrom (1, pos, io, 1, 0, len);
        }
        const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        float peak = 0, rms = 0; bool finite = true;
        for (int i = 0; i < n; ++i) { const float v = out.getSample (0, i); if (! std::isfinite (v)) finite = false; peak = juce::jmax (peak, std::abs (v)); rms += v * v; }
        rms = std::sqrt (rms / (float) n);
        std::printf ("processed %.2f s in %.3f s  -> realtime x%.1f  (CPU %.1f %%)  peak %.2f dBFS  rms %.2f dBFS  finite=%d  latency=%d\n",
                     n / rate, secs, (n / rate) / secs, 100.0 * secs / (n / rate),
                     juce::Decibels::gainToDecibels (peak), juce::Decibels::gainToDecibels (rms), (int) finite, proc.getReportedLatency());
        if (! bench)
        {
            juce::File f (arg (3)); f.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), rate, 2, 24, {}, 0));
            if (w) w->writeFromAudioSampleBuffer (out, 0, n);
        }
        return finite ? 0 : 3;
    }
    return 1;
}
