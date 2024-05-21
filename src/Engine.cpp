#include "Engine.h"

AudioSession::AudioSession(CComPtr<IAudioSessionControl> session) {
    this->session = session;
}

AudioSession::~AudioSession() {}

IAudioSessionControl *AudioSession::getSession() { return session; }

IAudioSessionControl2 *AudioSession::getSession2() {
    if (!session2) {
        HRESULT hr = getSession()->QueryInterface(
            __uuidof(IAudioSessionControl2), (void **)&session2);
        if (FAILED(hr))
            throw std::runtime_error(
                "Failed to get session control 2 interface");
    }
    return session2;
}

ISimpleAudioVolume *AudioSession::getSimpleAudioVolume() {
    if (!simpleAudioVolume) {
        HRESULT hr = getSession()->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                  (void **)&simpleAudioVolume);
        if (FAILED(hr))
            throw std::runtime_error(
                "Failed to get simple audio volume interface");
    }
    return simpleAudioVolume;
}

std::wstring AudioSession::getExecutableName() {
    if (!name) {
        LPWSTR wName;
        HRESULT hr = getSession2()->GetSessionIdentifier(&wName);
        if (FAILED(hr))
            throw std::runtime_error(
                "Failed to get session identifier/executable name");
        std::wstring wideString(wName);
        LocalFree(wName);

        // extract "abc.exe" from the returned string
        name = std::make_unique<std::wstring>();
        auto endOfPathBackslash = wideString.rfind(L"\\");
        if (endOfPathBackslash != std::wstring::npos) {
            std::wstring executableRegion =
                wideString.substr(endOfPathBackslash + 1);
            auto endOfPathPercentage = executableRegion.find(L"%");
            if (endOfPathPercentage != std::wstring::npos) {
                *name = executableRegion.substr(0, endOfPathPercentage);
            }
        }
    }
    return *name;
}

float AudioSession::getSessionVolume() {
    if (!volume) {
        volume = std::make_unique<float>();
        HRESULT hr = getSimpleAudioVolume()->GetMasterVolume(volume.get());
        if (FAILED(hr))
            throw std::runtime_error("Failed to get volume");
    }
    return *volume;
}

void AudioSession::setSessionVolume(float &newVolume) {
    HRESULT hr = getSimpleAudioVolume()->SetMasterVolume(newVolume, NULL);
    if (FAILED(hr))
        throw std::runtime_error("Failed to set volume");
    std::cout << "Volume set to: " << newVolume << std::endl;
}

IAudioMeterInformation *AudioSession::getAudioMeterInformation() {
    if (!audioMeterInformation) {
        HRESULT hr = getSession()->QueryInterface(
            __uuidof(IAudioMeterInformation), (void **)&audioMeterInformation);
        if (FAILED(hr))
            throw std::runtime_error("Failed to get audio meter interface");
    }
    return audioMeterInformation;
}

float AudioSession::getPeakAudioLevel() {
    if (!volumePeak) {
        volumePeak = std::make_unique<float>();
        HRESULT hr = getAudioMeterInformation()->GetPeakValue(volumePeak.get());
        if (FAILED(hr))
            throw std::runtime_error("Failed to get peak audio level");
    }
    return *volumePeak;
}

float Engine::getMaxPeakAudioLevel(
    const std::vector<std::wstring> &excludedExecutables) {
    float maxVolume = 0.0f;
    for (auto &session : sessions) {
        if (std::find(excludedExecutables.begin(), excludedExecutables.end(),
                      session->getExecutableName()) !=
            excludedExecutables.end()) {
            continue;
        }

        float volume = session->getPeakAudioLevel();
        if (volume > maxVolume)
            maxVolume = volume;
    }

    return maxVolume;
}

bool Engine::init() {
    try {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (FAILED(hr))
            throw std::runtime_error("Failed to initialize COM");

        hr = deviceEnumerator.CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                               nullptr, CLSCTX_ALL);
        if (FAILED(hr))
            throw std::runtime_error("Failed to create device enumerator");

        hr = deviceEnumerator->GetDefaultAudioEndpoint(
            EDataFlow::eRender, ERole::eConsole, &device);
        if (FAILED(hr))
            throw std::runtime_error("Failed to get default audio endpoint");

        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL,
                              (void **)&sessionManager2);
        if (FAILED(hr))
            throw std::runtime_error("Failed to activate session manager");
    } catch (std::exception &exception) {
        handleError(exception);
        return false;
    }
    return true;
}

std::vector<std::shared_ptr<AudioSession>> Engine::getAudioSessions() {
    if (sessionManager2 == nullptr)
        throw std::runtime_error(
            "Failed to get audio session (engine uninitialised)");

    HRESULT hr;

    std::vector<std::shared_ptr<AudioSession>> sessions;
    CComPtr<IAudioSessionEnumerator> sessionEnumerator;

    hr = sessionManager2->GetSessionEnumerator(&sessionEnumerator);
    if (FAILED(hr))
        throw std::runtime_error("Failed to get session enumerator");

    int count;
    hr = sessionEnumerator->GetCount(&count);
    if (FAILED(hr))
        throw std::runtime_error("Failed to get session count");

    for (int i = 0; i < count; i++) {
        CComPtr<IAudioSessionControl> pSessionControl;
        hr = sessionEnumerator->GetSession(i, &pSessionControl);
        if (FAILED(hr))
            throw std::runtime_error("Failed to get session " +
                                     std::to_string(i));

        auto session = std::make_shared<AudioSession>(pSessionControl);
        sessions.push_back(session);
    }

    return sessions;
}

std::shared_ptr<AudioSession>
Engine::getAudioSessionByExecutableName(const std::wstring &executableName) {
    for (auto &session : sessions) {
        std::wstring processName = session->getExecutableName();
        if (!processName.empty() && processName == executableName)
            return session;
    }
    return nullptr;
}

void Engine::requestQuit() { quitRequested = true; }

std::wstring Engine::getAbsoluteExecutablePath() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);

    std::wstring exePath(buffer);
    size_t lastSlashPos = exePath.find_last_of(L"\\/");
    std::wstring exeDirectory = exePath.substr(0, lastSlashPos + 1);

    return exeDirectory;
}

std::wstring Engine::getSettingsINIPath() {
    auto exeDirectory = getAbsoluteExecutablePath();
    return exeDirectory + SETTINGS_FILENAME;
}

bool Engine::openSettingsINI() {
    try {
        ShellExecuteW(NULL, L"open", getSettingsINIPath().c_str(), NULL, NULL,
                      SW_SHOWNORMAL);
    } catch (std::exception &exception) {
        handleError(exception);
        return false;
    }
    return true;
}

bool Engine::readSettingsINI() {
    try {
        tryCreateDefaultSettingsINI();

        readINIValue(L"Performance", L"fTickIdleMS", paramTickIdleMS);
        readINIValue(L"Performance", L"fTickTransitionsMS",
                     paramTickTransitionMS);
        readINIValue(L"General", L"fFadeSpeedMS", paramFadeSpeedMS);
        readINIValue(L"General", L"fVolumeMinimumToTrigger",
                     paramVolumeMinimumToTrigger);
        readINIValue(L"General", L"fVolumeMax", paramVolumeMax);
        readINIValue(L"General", L"iConsecutiveMinimumsToTrigger",
                     paramConsecutiveMinimumsToTrigger);
        readINIValue(L"General", L"iConsecutiveMinimumsToEnd",
                     paramConsecutiveMinimumsToEnd);

        readINIValue(L"General", L"sExcludedExecutables",
                     paramExcludedExecutables);
        readINIValue(L"General", L"sControlledExecutable",
                     paramControlledExecutable);

        paramExcludedExecutables.push_back(paramControlledExecutable);

        readINIValue(L"General", L"fVolumeRestore", paramVolumeRestore);

        readINIValue(L"General", L"sCommandOnDuck", paramCommandOnDuck);
        readINIValue(L"General", L"sCommandOnUnduck", paramCommandOnUnduck);
    } catch (std::exception &exception) {
        handleError(exception);
        return false;
    }
    return true;
}

void Engine::tryCreateDefaultSettingsINI() {
    auto settingsPath = getSettingsINIPath();

    std::wifstream checkFile(settingsPath);
    if (!checkFile) {
        std::wofstream file(settingsPath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to create default INI file");
        }
        file << SETTINGS_DEFAULT;
        file.close();
    }
    checkFile.close();
}

std::wstring Engine::readINIValueStringW(const std::wstring &section,
                                         const std::wstring &key) {
    auto settingsPath = getSettingsINIPath();
    wchar_t buffer[1024];
    DWORD bytesRead = GetPrivateProfileStringW(
        section.c_str(), key.c_str(), NULL, buffer, 1024, settingsPath.c_str());

    if (bytesRead == 0) {
        DWORD error = GetLastError();
        if (error != ERROR_SUCCESS)
            throw std::runtime_error(
                "Failed to read value from INI file\n"
                "If the program has just updated, "
                "there may be new settings not present in your INI file.\n"
                "Try deleting the INI file and opening the program again.\n"
                "Key: " +
                wStringToString(key));
    }

    std::wstring value(buffer);
    return value;
}

void Engine::runCommandSilent(std::wstring &command) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring formattedCommand = CMD_START + command;

    if (!CreateProcessW(NULL, const_cast<LPWSTR>(formattedCommand.c_str()),
                        NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi))
        throw std::runtime_error("Failed to create process for command");

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void Engine::setBypassed(bool newBypassed) { bypassed = newBypassed; }

bool Engine::getBypassed() const { return bypassed; }

std::unique_ptr<Engine> Engine::engine; // singleton
Engine *Engine::get() {
    if (!engine)
        engine = std::make_unique<Engine>();
    return engine.get();
}

bool Engine::running() {
    try {
        if (!readSettingsINI())
            return hasError();

        if (!init())
            return hasError();

        while (!hasError()) {

            sessions = getAudioSessions();

            float maxVolume = getMaxPeakAudioLevel(paramExcludedExecutables);

            auto sessionControls =
                getAudioSessionByExecutableName(paramControlledExecutable);

            float volumeTarget = (maxVolume > paramVolumeMinimumToTrigger)
                                     ? paramVolumeMin
                                     : paramVolumeMax;

            float sleepNeeded = paramTickIdleMS;

            if (getBypassed())
                volumeTarget = paramVolumeRestore;

            // if found controlling program...
            if (sessionControls) {
                shortStatusString =
                    L"Found and controlling " + paramControlledExecutable;

                float volumeCurrent = sessionControls->getSessionVolume();
                bool shouldTransition =
                    std::abs(volumeCurrent - volumeTarget) > 0.001;

                if (shouldTransition && getBypassed()) {
                    sessionControls->setSessionVolume(paramVolumeRestore);
                    // run unduck command if bypassing and currently ducked...
                    if (volumeCurrent == paramVolumeMin)
                        runCommandSilent(paramCommandOnUnduck);
                }

                if (shouldTransition && !getBypassed()) {
                    // increase consecutive minimums if at minimum
                    if (volumeTarget == paramVolumeMin) {
                        currentConsecutiveMinimumsToTrigger =
                            min(currentConsecutiveMinimumsToTrigger + 1,
                                paramConsecutiveMinimumsToTrigger);
                    } else {
                        currentConsecutiveMinimumsToEnd =
                            min(currentConsecutiveMinimumsToEnd + 1,
                                paramConsecutiveMinimumsToEnd);
                    }

                    // if either minimum is at the target value
                    if ((currentConsecutiveMinimumsToTrigger ==
                         paramConsecutiveMinimumsToTrigger) ||
                        (currentConsecutiveMinimumsToEnd ==
                         paramConsecutiveMinimumsToEnd)) {

                        float directionMult =
                            ((volumeCurrent - volumeTarget) > 0) ? -1.0f : 1.0f;

                        float step =
                            (paramVolumeMax - paramVolumeMin) *
                            (paramTickTransitionMS / paramFadeSpeedMS) *
                            directionMult;

                        float newVolume =
                            min(max(volumeCurrent + step, paramVolumeMin),
                                paramVolumeMax);

                        sessionControls->setSessionVolume(newVolume);

                        sleepNeeded = paramTickTransitionMS;

                        // if transitioning, set both values to max to ensure
                        // smooth transitioning
                        currentConsecutiveMinimumsToEnd =
                            paramConsecutiveMinimumsToEnd;
                        currentConsecutiveMinimumsToTrigger =
                            paramConsecutiveMinimumsToTrigger;

                        // try duck command
                        if (newVolume == paramVolumeMin && directionMult < 0.0)
                            runCommandSilent(paramCommandOnDuck);

                        // try unduck command
                        if (volumeCurrent == paramVolumeMin &&
                            directionMult > 0.0)
                            runCommandSilent(paramCommandOnUnduck);
                    }

                } else {
                    currentConsecutiveMinimumsToEnd = 0;
                    currentConsecutiveMinimumsToTrigger = 0;
                }
            } else {
                // failure to file controlled executable is not fatal.
                std::cout
                    << "Cannot find controlled executable, will keep looking."
                    << std::endl;
                shortStatusString = L"Controlled executable not found";
            }

            // cleanup
            sessions.clear();

            Sleep((DWORD)sleepNeeded);

            if (quitRequested)
                break;
        }
    } catch (std::runtime_error &error) {
        handleError(error);
    }

    // try resetting volume to restore value
    try {
        sessions = getAudioSessions();
        auto s = getAudioSessionByExecutableName(paramControlledExecutable);
        if (s)
            s->setSessionVolume(paramVolumeRestore);

        sessions.clear();
    } catch (std::runtime_error &error) {
        handleError(error);
    }

    return hasError();
}

bool Engine::hasError() const { return !errorString.empty(); }

std::wstring &Engine::getErrorString() { return errorString; }

std::wstring &Engine::getShortStatusString() { return shortStatusString; }

void Engine::handleError(const std::exception &exception) {
    errorString = stringToWString(exception.what());
    if (errorString.empty())
        errorString = L"Unknown error";
    shortStatusString = L"An error has occurred";
}

std::string Engine::wStringToString(const std::wstring &wstr) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
    return converter.to_bytes(wstr);
}

std::wstring Engine::stringToWString(const std::string &str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
    return converter.from_bytes(str);
}

Engine::Engine() {}

Engine::~Engine() {
    sessions.clear();
    // explicitly free CComPtrs before CoUninitialize()
    deviceEnumerator = nullptr;
    device = nullptr;
    sessionManager2 = nullptr;
    CoUninitialize();
}
