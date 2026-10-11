// Fonctions propres à Windows, isolées ici : windows.h n'est inclus nulle part ailleurs.
//  - style de la fenêtre sans barre native (coins arrondis + ombre Windows 11) ;
//  - micro virtuel : renommage et format (48 kHz) des points de terminaison, comme le panneau Son de Windows ;
//  - installation assistée de VB-CABLE : vérification de la signature, lancement avec droits administrateur.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <string>
#include <softpub.h>
#include <wintrust.h>
#include <shellapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <atomic>
#pragma comment (lib, "dwmapi.lib")
#pragma comment (lib, "ole32.lib")
#pragma comment (lib, "propsys.lib")
#pragma comment (lib, "wintrust.lib")
#pragma comment (lib, "crypt32.lib")
#pragma comment (lib, "shell32.lib")
#pragma comment (lib, "mmdevapi.lib")

void applyOniriqueWindowStyle (void* nativeHandle)
{
    auto hwnd = static_cast<HWND> (nativeHandle);
    if (hwnd == nullptr) return;

    const BOOL dark = TRUE;
    DwmSetWindowAttribute (hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof (dark));
    const int round = 2;   // DWMWCP_ROUND (Windows 11 ; ignoré sans erreur sur Windows 10)
    DwmSetWindowAttribute (hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof (round));
    const COLORREF border = RGB (0x5C, 0x00, 0x08);
    DwmSetWindowAttribute (hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof (border));
    // ombre portée de Windows sous une fenêtre sans cadre
    const MARGINS m { 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea (hwnd, &m);
}

namespace
{
// Interface (non documentée mais stable depuis Windows 7) utilisée par le panneau Son pour modifier un
// périphérique sans droits administrateur : la modification passe par le service audio de Windows.
struct __declspec (uuid ("f8679f50-850a-41cf-9c72-430f290290c8")) IPolicyConfig : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat (PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat (PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat (PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat (PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod (PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod (PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue (PCWSTR, BOOL, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue (PCWSTR, BOOL, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint (PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility (PCWSTR, BOOL) = 0;
};
const CLSID CLSID_PolicyConfigClient { 0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };

template <class T> struct ComPtr
{
    T* p = nullptr;
    ~ComPtr() { if (p != nullptr) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};
} // namespace

namespace
{
// COM initialisé le temps d'un appel (déjà initialisé par JUCE sur le fil de l'interface : sans effet dans ce cas)
struct ComScope
{
    HRESULT hr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    ~ComScope() { if (SUCCEEDED (hr)) CoUninitialize(); }
};

// point de terminaison actif dont le nom complet est `name` (capture = périphérique d'enregistrement)
bool findEndpoint (const wchar_t* name, bool capture, IMMDevice** out)
{
    ComPtr<IMMDeviceEnumerator> en;
    ComPtr<IMMDeviceCollection> list;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))
        || FAILED (en->EnumAudioEndpoints (capture ? eCapture : eRender, DEVICE_STATE_ACTIVE, &list)))
        return false;

    UINT count = 0;
    list->GetCount (&count);
    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* dev = nullptr;
        if (FAILED (list->Item (i, &dev))) continue;
        ComPtr<IPropertyStore> props;
        bool match = false;
        if (SUCCEEDED (dev->OpenPropertyStore (STGM_READ, &props)))
        {
            PROPVARIANT v;
            PropVariantInit (&v);
            match = SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && std::wstring (v.pwszVal) == name;
            PropVariantClear (&v);
        }
        if (match) { *out = dev; return true; }
        dev->Release();
    }
    return false;
}

// PKEY_AudioEngine_DeviceFormat (format par défaut du point de terminaison)
const PROPERTYKEY kDeviceFormatKey { { 0xf19f064d, 0x082c, 0x4e27, { 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c } }, 0 };

struct DeviceId
{
    LPWSTR id = nullptr;
    ~DeviceId() { if (id != nullptr) CoTaskMemFree (id); }
};
} // namespace

// Renomme le périphérique d'enregistrement dont le nom complet est currentName (ex. "CABLE Output (VB-Audio
// Virtual Cable)") : sa description devient newName, Windows l'affiche alors "newName (VB-Audio Virtual Cable)".
bool renameRecordingDevice (const wchar_t* currentName, const wchar_t* newName)
{
    ComScope com;
    ComPtr<IMMDevice> dev;
    DeviceId id;
    if (! findEndpoint (currentName, true, &dev) || FAILED (dev->GetId (&id.id))) return false;

    PROPVARIANT value;
    if (FAILED (InitPropVariantFromString (newName, &value))) return false;

    // 1) comme le panneau Son (sans droits administrateur)
    bool ok = false;
    ComPtr<IPolicyConfig> policy;
    if (SUCCEEDED (CoCreateInstance (CLSID_PolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof (IPolicyConfig), (void**) &policy)))
        ok = SUCCEEDED (policy->SetPropertyValue (id.id, FALSE, PKEY_Device_DeviceDesc, &value));

    // 2) sinon, écriture directe (fonctionne si l'application a les droits administrateur)
    if (! ok)
    {
        ComPtr<IPropertyStore> rw;
        if (SUCCEEDED (dev->OpenPropertyStore (STGM_READWRITE, &rw)))
            ok = SUCCEEDED (rw->SetValue (PKEY_Device_DeviceDesc, value)) && SUCCEEDED (rw->Commit());
    }
    PropVariantClear (&value);
    return ok;
}

// Fréquence du format par défaut d'un point de terminaison (celle que Windows utilise en mode partagé), 0 si inconnue.
int getEndpointSampleRate (const wchar_t* name, bool capture)
{
    ComScope com;
    ComPtr<IMMDevice> dev;
    ComPtr<IPropertyStore> props;
    if (! findEndpoint (name, capture, &dev) || FAILED (dev->OpenPropertyStore (STGM_READ, &props))) return 0;

    PROPVARIANT v;
    PropVariantInit (&v);
    int rate = 0;
    if (SUCCEEDED (props->GetValue (kDeviceFormatKey, &v)) && v.vt == VT_BLOB && v.blob.cbSize >= sizeof (WAVEFORMATEX))
        rate = (int) reinterpret_cast<const WAVEFORMATEX*> (v.blob.pBlobData)->nSamplesPerSec;
    PropVariantClear (&v);
    return rate;
}

// Format par défaut d'un point de terminaison -> 48 kHz / 24 bits (nombre de canaux conservé), comme
// Propriétés > Avancé > Format par défaut dans le panneau Son. 48 kHz = la fréquence de Discord, d'OBS et des jeux :
// plus aucun rééchantillonnage par Windows entre l'application et eux.
bool setEndpointFormat48k (const wchar_t* name, bool capture)
{
    ComScope com;
    ComPtr<IMMDevice> dev;
    DeviceId id;
    if (! findEndpoint (name, capture, &dev) || FAILED (dev->GetId (&id.id))) return false;

    ComPtr<IPolicyConfig> policy;
    if (FAILED (CoCreateInstance (CLSID_PolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof (IPolicyConfig), (void**) &policy)))
        return false;

    WORD channels = 2;
    WAVEFORMATEX* current = nullptr;
    if (SUCCEEDED (policy->GetDeviceFormat (id.id, FALSE, &current)) && current != nullptr)
    {
        channels = std::max<WORD> (1, current->nChannels);
        CoTaskMemFree (current);
    }

    auto make = [channels] (bool isFloat, WORD bits)
    {
        WAVEFORMATEXTENSIBLE f {};
        f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        f.Format.nChannels = channels;
        f.Format.nSamplesPerSec = 48000;
        f.Format.wBitsPerSample = bits;
        f.Format.nBlockAlign = (WORD) (channels * bits / 8);
        f.Format.nAvgBytesPerSec = 48000u * f.Format.nBlockAlign;
        f.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
        f.Samples.wValidBitsPerSample = bits;
        f.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
        f.SubFormat = isFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
        return f;
    };
    auto endpoint = make (false, 24), mix = make (true, 32);
    if (SUCCEEDED (policy->SetDeviceFormat (id.id, &endpoint.Format, &mix.Format))) return true;
    endpoint = make (false, 16);   // pilote limité au 16 bits
    return SUCCEEDED (policy->SetDeviceFormat (id.id, &endpoint.Format, &mix.Format));
}

// Signature Authenticode valide ET signataire dont le nom contient `expectedSigner` (insensible à la casse).
bool verifySignedBy (const wchar_t* filePath, const wchar_t* expectedSigner)
{
    WINTRUST_FILE_INFO file {};
    file.cbStruct = sizeof (file);
    file.pcwszFilePath = filePath;

    WINTRUST_DATA data {};
    data.cbStruct = sizeof (data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust (nullptr, &action, &data);
    bool ok = false;
    if (status == ERROR_SUCCESS)
    {
        if (auto* prov = WTHelperProvDataFromStateData (data.hWVTStateData))
            if (auto* signer = WTHelperGetProvSignerFromChain (prov, 0, FALSE, 0))
                if (signer->csCertChain > 0 && signer->pasCertChain != nullptr)
                {
                    wchar_t subject[512] {};
                    CertGetNameStringW (signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, subject, 512);
                    std::wstring s (subject), e (expectedSigner);
                    for (auto& c : s) c = (wchar_t) towupper (c);
                    for (auto& c : e) c = (wchar_t) towupper (c);
                    ok = s.find (e) != std::wstring::npos;
                }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust (nullptr, &action, &data);
    return ok;
}

// Lance un programme avec les droits administrateur (Windows affiche sa demande d'autorisation) et attend sa fin.
// Renvoie le code de sortie, ou -1 si l'utilisateur a refusé / le lancement a échoué.
int runElevatedAndWait (const wchar_t* exe, const wchar_t* params, const wchar_t* workingDir)
{
    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof (info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = exe;
    info.lpParameters = params;
    info.lpDirectory = workingDir;
    info.nShow = SW_SHOWNORMAL;
    if (! ShellExecuteExW (&info) || info.hProcess == nullptr) return -1;
    WaitForSingleObject (info.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess (info.hProcess, &code);
    CloseHandle (info.hProcess);
    return (int) code;
}

bool isArm64Machine()
{
    USHORT process = 0, native = 0;
    using Fn = BOOL (WINAPI*) (HANDLE, USHORT*, USHORT*);
    if (auto fn = (Fn) GetProcAddress (GetModuleHandleW (L"kernel32.dll"), "IsWow64Process2"))
        if (fn (GetCurrentProcess(), &process, &native)) return native == IMAGE_FILE_MACHINE_ARM64;
    return false;
}

//==============================================================================
// Capture de ce que joue le PC (Discord, jeux, musique...) SAUF Onirique Broadcast lui-même : "process loopback"
// de Windows 10 2004+ en mode exclusion de notre arbre de processus. Sert de référence à la garde d'écho : notre
// propre retour casque n'y figure jamais (sinon la garde confondrait la voix de l'utilisateur avec de l'écho).
namespace
{
class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
public:
    ActivationHandler() : done (CreateEventW (nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivationHandler() { if (done != nullptr) CloseHandle (done); }

    STDMETHODIMP ActivateCompleted (IActivateAudioInterfaceAsyncOperation* op) override
    {
        HRESULT activateHr = E_FAIL;
        IUnknown* unk = nullptr;
        if (SUCCEEDED (op->GetActivateResult (&activateHr, &unk)) && SUCCEEDED (activateHr) && unk != nullptr)
            hr = unk->QueryInterface (__uuidof (IAudioClient), (void**) &client);
        else
            hr = FAILED (activateHr) ? activateHr : E_FAIL;
        if (unk != nullptr) unk->Release();
        SetEvent (done);
        return S_OK;
    }

    STDMETHODIMP QueryInterface (REFIID riid, void** ppv) override
    {
        if (riid == __uuidof (IUnknown) || riid == __uuidof (IActivateAudioInterfaceCompletionHandler))
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*> (this);
        else if (riid == __uuidof (IAgileObject))
            *ppv = static_cast<IAgileObject*> (this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override  { return (ULONG) InterlockedIncrement (&refs); }
    STDMETHODIMP_(ULONG) Release() override { const LONG r = InterlockedDecrement (&refs); if (r == 0) delete this; return (ULONG) r; }

    HANDLE done = nullptr;
    HRESULT hr = E_FAIL;
    IAudioClient* client = nullptr;

private:
    LONG refs = 1;
};
} // namespace

struct ProcessLoopback
{
    std::thread thread;
    std::atomic<bool> stop { false }, running { false };
    void (*callback) (void*, const float*, int) = nullptr;
    void* ctx = nullptr;
    int rate = 48000;
    DWORD pid = 0;
    bool include = false;   // false : tout sauf ce processus ; true : uniquement ce processus

    void run()
    {
        const HRESULT co = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        IAudioClient* client = nullptr;
        IAudioCaptureClient* capture = nullptr;
        HANDLE ready = CreateEventW (nullptr, FALSE, FALSE, nullptr);

        AUDIOCLIENT_ACTIVATION_PARAMS params {};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = include ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE
                                                                   : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT pv {};
        pv.vt = VT_BLOB;
        pv.blob.cbSize = sizeof (params);
        pv.blob.pBlobData = (BYTE*) &params;

        auto* handler = new ActivationHandler();
        IActivateAudioInterfaceAsyncOperation* op = nullptr;
        if (SUCCEEDED (ActivateAudioInterfaceAsync (VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof (IAudioClient), &pv, handler, &op))
            && WaitForSingleObject (handler->done, 5000) == WAIT_OBJECT_0 && SUCCEEDED (handler->hr))
        {
            client = handler->client;
            handler->client = nullptr;
        }
        if (op != nullptr) op->Release();
        handler->Release();

        WAVEFORMATEX fmt {};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = (DWORD) rate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 4;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

        if (client != nullptr
            && SUCCEEDED (client->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                              200000, 0, &fmt, nullptr))
            && SUCCEEDED (client->SetEventHandle (ready))
            && SUCCEEDED (client->GetService (__uuidof (IAudioCaptureClient), (void**) &capture))
            && SUCCEEDED (client->Start()))
        {
            running = true;
            std::vector<float> mono (4096);
            while (! stop.load())
            {
                WaitForSingleObject (ready, 100);
                UINT32 packet = 0;
                while (! stop.load() && SUCCEEDED (capture->GetNextPacketSize (&packet)) && packet > 0)
                {
                    BYTE* data = nullptr;
                    UINT32 frames = 0;
                    DWORD flags = 0;
                    if (FAILED (capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr))) break;
                    const auto* s = reinterpret_cast<const short*> (data);
                    for (UINT32 pos = 0; pos < frames;)
                    {
                        const UINT32 n = std::min<UINT32> ((UINT32) mono.size(), frames - pos);
                        for (UINT32 i = 0; i < n; ++i)
                            mono[i] = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 ? 0.0f
                                    : (float) (s[2 * (pos + i)] + s[2 * (pos + i) + 1]) * (0.5f / 32768.0f);
                        callback (ctx, mono.data(), (int) n);
                        pos += n;
                    }
                    capture->ReleaseBuffer (frames);
                }
            }
            client->Stop();
        }
        running = false;
        if (capture != nullptr) capture->Release();
        if (client != nullptr) client->Release();
        CloseHandle (ready);
        if (SUCCEEDED (co)) CoUninitialize();
    }
};

ProcessLoopback* startProcessLoopback (void (*callback) (void*, const float*, int), void* ctx, int sampleRate)
{
    auto* lb = new ProcessLoopback();
    lb->callback = callback;
    lb->ctx = ctx;
    lb->rate = sampleRate;
    lb->pid = GetCurrentProcessId();
    lb->thread = std::thread ([lb] { lb->run(); });
    return lb;
}

bool isProcessLoopbackRunning (ProcessLoopback* lb) { return lb != nullptr && lb->running.load(); }

void stopProcessLoopback (ProcessLoopback* lb)
{
    if (lb == nullptr) return;
    lb->stop = true;
    if (lb->thread.joinable()) lb->thread.join();
    delete lb;
}

// capture du son d'UN processus (et de ses enfants), comme le partage d'écran de Discord : sert aux vérifications
ProcessLoopback* startProcessCapture (unsigned long processId, void (*callback) (void*, const float*, int), void* ctx, int sampleRate)
{
    auto* lb = new ProcessLoopback();
    lb->callback = callback;
    lb->ctx = ctx;
    lb->rate = sampleRate;
    lb->pid = (DWORD) processId;
    lb->include = true;
    lb->thread = std::thread ([lb] { lb->run(); });
    return lb;
}
