#pragma once

#include <atlbase.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <codecvt>
#include <fstream>
#include <iostream>
#include <locale>
#include <string>
#include <thread>
#include <vector>

static const LPCWSTR PROG_BRAND_NAME = L"Auto-Duck BGM";
static const std::wstring SETTINGS_FILENAME = L"settings.ini";
static const std::wstring CMD_START = L"cmd.exe /C ";

static const std::wstring SETTINGS_DEFAULT = LR"([Performance]
; Controls how frequently the program queries volume information when idle.
fTickIdleMS=1000.0

; Controls how frequently the program queries volume information when transitioning. Higher values mean a smoother transition.
fTickTransitionsMS=50.0



[General]
; Control the fade speed of the audio.
fFadeSpeedMS=1000.0

; Number of consecutive samples that the volume needs to be above the fVolumeMinimumToTrigger to trigger the duck. 1 will trigger the duck immediately.
iConsecutiveMinimumsToTrigger=1

; Number of consecutive samples that the volume needs to be below the fVolumeMinimumToTrigger to end the duck.
iConsecutiveMinimumsToEnd=3

; Minimum volume of programs not excluded or controlled to trigger the duck.
fVolumeMinimumToTrigger=0.0

; The minimum volume the controlled program will be lowered to. 0.0 is muted.
fVolumeMin=0.0

; The maximum volume the controlled program will be raised to. For background music, set to a lower value.
fVolumeMax=0.2

; The volume to restore the controlled program to when this program is closed or bypassed.
fVolumeRestore=1.0

; Excluded executable names that are ignored when calculating whether to trigger. Separated by a "/" character.
sExcludedExecutables=nvcontainer.exe/amdow.exe/amddvr.exe

; The program that is targeted.
sControlledExecutable=foobar2000.exe

; Run a Windows command when ducked or unducked. Leave empty for no commands.
sCommandOnDuck=
sCommandOnUnduck=
)";

// audiosession store info about a single IAudioSessionControl within a single
// tick. interfaces other than IAudioSessionControl are not requested/created
// until they are accessed.
// the result of all function calls are cached in the appropriate private
// variables.
class AudioSession {
  private:
    CComPtr<IAudioSessionControl> session = nullptr;
    CComPtr<IAudioSessionControl2> session2 = nullptr;
    CComPtr<ISimpleAudioVolume> simpleAudioVolume = nullptr;
    CComPtr<IAudioMeterInformation> audioMeterInformation = nullptr;
    std::unique_ptr<std::wstring> name = nullptr;
    std::unique_ptr<float> volume = nullptr;
    std::unique_ptr<float> volumePeak = nullptr;

  public:
    AudioSession(CComPtr<IAudioSessionControl> session);
    ~AudioSession();

    IAudioSessionControl *getSession();
    IAudioSessionControl2 *getSession2();
    ISimpleAudioVolume *getSimpleAudioVolume();
    IAudioMeterInformation *getAudioMeterInformation();

    // attempts to extract the executable name from the session identifier in
    // the form of "ABC.exe"
    std::wstring getExecutableName();

    // session volume is volume level set on the mixer (Sndvol)
    float getSessionVolume();
    void setSessionVolume(float &newVolume);

    // peak audio level is the max of any channel of the current audio session.
    // this has NO averaging of peak levels (RMS loudness)
    float getPeakAudioLevel();
};

// singleton engine class accessible via Engine::get().
// the running() function blocks until the engine is requested to quit via
// requestQuit() or an error occurs. if the engine encountered an error, use
// hasError() to check and getErrorString() to fetch the error string.
class Engine {
  private:
    static std::unique_ptr<Engine> engine; // singleton

    std::vector<std::shared_ptr<AudioSession>> sessions;

    std::wstring errorString;
    std::wstring shortStatusString = L"";
    void handleError(const std::exception &exception);

    CComPtr<IMMDeviceEnumerator> deviceEnumerator = nullptr;
    CComPtr<IMMDevice> device = nullptr;
    CComPtr<IAudioSessionManager2> sessionManager2 = nullptr;

    // params set by ini
    float paramFadeSpeedMS;
    float paramTickIdleMS;
    float paramTickTransitionMS;
    float paramVolumeMinimumToTrigger;
    float paramVolumeMax;
    float paramVolumeMin;
    float paramVolumeRestore;
    int paramConsecutiveMinimumsToEnd;
    int paramConsecutiveMinimumsToTrigger;
    std::vector<std::wstring> paramExcludedExecutables;
    std::wstring paramControlledExecutable;
    std::wstring paramCommandOnDuck;
    std::wstring paramCommandOnUnduck;

    int currentConsecutiveMinimumsToTrigger = 0;
    int currentConsecutiveMinimumsToEnd = 0;

    bool quitRequested = false;
    bool bypassed = false;

    // string conversion functions
    std::string wStringToString(const std::wstring &wstr);
    std::wstring stringToWString(const std::string &str);

    // create a default ini settings file if one is not found
    void tryCreateDefaultSettingsINI();

    std::wstring getAbsoluteExecutablePath();
    std::wstring getSettingsINIPath();

    // get the max peak audio level while ignoring any executables with names in
    // the excludedExecutables vector
    float
    getMaxPeakAudioLevel(const std::vector<std::wstring> &excludedExecutables);

    // initialise COM objects, etc...
    bool init();

    // returns all audio sessions at the current point as reported by the
    // session manager
    std::vector<std::shared_ptr<AudioSession>> getAudioSessions();

    // find an audio session instance with the given executable name in the
    // format of "abc.exe"
    std::shared_ptr<AudioSession>
    getAudioSessionByExecutableName(const std::wstring &executableName);

    // run a windows command (i.e., "cmd.exe /c ...") silently in the
    // background.
    void runCommandSilent(std::wstring &command);

    // ### INI TEMPLATE FUNCTIONS ###
    // a series of template functions to read a value from the ini and convert
    // it to the appropriate type and store it in the given variable

    // read a value from the ini as a string
    std::wstring readINIValueStringW(const std::wstring &section,
                                     const std::wstring &key);

    // generic
    template <typename T>
    inline void readINIValue(const std::wstring &section,
                             const std::wstring &key, T &value) {
        value = readINIValueStringW(section, key);
    }

    // int
    template <>
    inline void readINIValue(const std::wstring &section,
                             const std::wstring &key, int &value) {
        value = std::stoi(readINIValueStringW(section, key));
    }

    // float
    template <>
    inline void readINIValue(const std::wstring &section,
                             const std::wstring &key, float &value) {
        value = std::stof(readINIValueStringW(section, key));
    }

    // list of values separated by '/'s
    template <>
    inline void readINIValue(const std::wstring &section,
                             const std::wstring &key,
                             std::vector<std::wstring> &value) {
        value.clear();
        auto str = readINIValueStringW(section, key);

        size_t start = 0;
        size_t end = str.find(L"/");

        while (end != std::wstring::npos) {
            value.push_back(str.substr(start, end - start));
            start = end + 1;
            end = str.find(L"/", start);
        }

        value.push_back(str.substr(start));
    }

  public:
    Engine();
    ~Engine();

    // get or create singleton
    static Engine *get();

    // runs the engine and blocks until it quit is requested or an error occurs.
    // returns whether or not the engine quit because of an error.
    bool running();

    bool hasError() const;
    std::wstring &getErrorString();
    std::wstring &getShortStatusString();

    // open the settings ini with the default windows application for opening
    // .ini files (usually notepad). returns if successfully opened
    bool openSettingsINI();

    // read the settings ini and updates param variables. returns if read all
    // successfully. this function can also be used to reload the ini during
    // execution.
    bool readSettingsINI();

    // tell the engine to quit on the next tick. (running() will return)
    void requestQuit();

    void setBypassed(bool newBypassed);
    bool getBypassed() const;
};
