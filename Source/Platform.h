#pragma once

// Fonctions Windows (WindowsPlatform.cpp)
void applyOniriqueWindowStyle (void* nativeHandle);
bool renameRecordingDevice (const wchar_t* currentName, const wchar_t* newName);

// micro virtuel : format des points de terminaison
int getEndpointSampleRate (const wchar_t* friendlyName, bool capture);
bool setEndpointFormat48k (const wchar_t* friendlyName, bool capture);

// installation assistée
bool verifySignedBy (const wchar_t* filePath, const wchar_t* expectedSigner);
int runElevatedAndWait (const wchar_t* exe, const wchar_t* params, const wchar_t* workingDir);
bool isArm64Machine();

// capture de tout ce que joue le PC sauf Onirique Broadcast (référence de la garde d'écho), Windows 10 2004+
struct ProcessLoopback;
ProcessLoopback* startProcessLoopback (void (*callback) (void* ctx, const float* mono, int numSamples), void* ctx, int sampleRate);
bool isProcessLoopbackRunning (ProcessLoopback*);
void stopProcessLoopback (ProcessLoopback*);
ProcessLoopback* startProcessCapture (unsigned long processId, void (*callback) (void* ctx, const float* mono, int numSamples), void* ctx, int sampleRate);
