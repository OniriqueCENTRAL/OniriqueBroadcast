#include "CableInstaller.h"
#include "../Platform.h"

void CableInstaller::run()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OniriqueBroadcast_VBCABLE");
    dir.deleteRecursively();
    if (! dir.createDirectory()) { fail ("Cannot create a temporary folder."); return; }

    // ---- 1. téléchargement (pack officiel, ~1.3 Mo)
    const auto zipFile = dir.getChildFile ("VBCABLE_Driver_Pack.zip");
    {
        auto in = juce::URL (kPackUrl).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                              .withConnectionTimeoutMs (20000)
                                                              .withNumRedirectsToFollow (5));
        if (in == nullptr) { fail ("Download failed: check the internet connection."); return; }

        juce::FileOutputStream out (zipFile);
        if (! out.openedOk()) { fail ("Cannot write the download."); return; }
        const auto total = in->getTotalLength();
        juce::HeapBlock<char> buf (32768);
        juce::int64 got = 0;
        while (! in->isExhausted())
        {
            if (threadShouldExit()) return;
            const int n = in->read (buf.getData(), 32768);
            if (n <= 0) break;
            out.write (buf.getData(), (size_t) n);
            got += n;
            if (total > 0) progress = (float) got / (float) total;
        }
        out.flush();
        if (got < 100000) { fail ("Download incomplete."); return; }
    }

    // ---- 2. extraction
    state = State::verifying;
    const auto packDir = dir.getChildFile ("pack");
    {
        juce::ZipFile zip (zipFile);
        if (zip.getNumEntries() == 0 || zip.uncompressTo (packDir).failed()) { fail ("The downloaded package is invalid."); return; }
    }

    // ---- 3. programme d'installation adapté au PC + vérification de la signature de l'éditeur
    juce::File setup;
    const bool arm = isArm64Machine();
    for (const auto& f : packDir.findChildFiles (juce::File::findFiles, true, "*.exe"))
    {
        const auto n = f.getFileName().toLowerCase();
        if (! n.contains ("setup")) continue;
        if (arm ? n.contains ("arm64") : n.contains ("x64")) { setup = f; break; }
    }
    if (! setup.existsAsFile()) { fail ("No installer for this PC in the package."); return; }
    if (! verifySignedBy (setup.getFullPathName().toWideCharPointer(), L"BUREL VINCENT"))
    {
        fail ("The installer is not signed by VB-Audio: installation cancelled for safety.");
        return;
    }

    // ---- 4. installation (Windows demande l'autorisation administrateur)
    state = State::installing;
    const int code = runElevatedAndWait (setup.getFullPathName().toWideCharPointer(), L"-i -h",
                                         setup.getParentDirectory().getFullPathName().toWideCharPointer());
    if (code < 0) { fail ("Installation cancelled (administrator permission was not granted)."); return; }

    progress = 1.0f;
    state = State::done;
}
