
#include "driver_installer.h"
#define INITGUID
#include <windows.h>
#include <mmreg.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <setupapi.h>
#include <newdev.h>
#include <devguid.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <algorithm>

#ifndef MAX_CLASS_NAME_LEN
#define MAX_CLASS_NAME_LEN 32
#endif

#ifndef GUID_NULL
const GUID GUID_NULL = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
#endif

typedef enum DeviceShareMode { DeviceShareModeShared, DeviceShareModeExclusive } DeviceShareMode;

interface IPolicyConfig : public IUnknown {
  public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

static const IID IID_IPolicyConfig = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};
static const CLSID CLSID_PolicyConfig = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

namespace moonmic {

DriverInstaller::DriverInstaller() {

    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    driver_path_ = (exe_dir / "driver").string();
}

bool DriverInstaller::isRunningAsAdmin() {
    BOOL is_admin = FALSE;
    PSID admin_group = NULL;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                 0, &admin_group)) {

        CheckTokenMembership(NULL, admin_group, &is_admin);
        FreeSid(admin_group);
    }

    return is_admin == TRUE;
}

bool DriverInstaller::restartAsAdmin() {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);

    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas";
    sei.lpFile = exe_path;
    sei.hwnd = NULL;
    sei.nShow = SW_NORMAL;

    if (!ShellExecuteExA(&sei)) {
        DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            std::cerr << "[DriverInstaller] User cancelled UAC prompt" << std::endl;
        }
        return false;
    }

    return true;
}

bool DriverInstaller::isVBCableInstalled() {

    std::string input = getVBCableInputDevice();
    std::string output = getVBCableOutputDevice();

    return !input.empty() && !output.empty();
}

std::string DriverInstaller::getVBCableInputDevice() {
    HRESULT hr;
    IMMDeviceEnumerator* enumerator = NULL;
    IMMDeviceCollection* collection = NULL;

    CoInitialize(NULL);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void**)&enumerator);

    if (FAILED(hr)) {
        CoUninitialize();
        return "";
    }

    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        enumerator->Release();
        CoUninitialize();
        return "";
    }

    UINT count;
    collection->GetCount(&count);

    std::string result;

    for (UINT i = 0; i < count; i++) {
        IMMDevice* device = NULL;
        collection->Item(i, &device);

        if (device) {
            IPropertyStore* props = NULL;
            device->OpenPropertyStore(STGM_READ, &props);

            if (props) {
                PROPVARIANT var_name;
                PropVariantInit(&var_name);

                props->GetValue(PKEY_Device_FriendlyName, &var_name);

                if (var_name.vt == VT_LPWSTR) {
                    std::wstring wname(var_name.pwszVal);
                    std::string name(wname.begin(), wname.end());

                    if (name.find("CABLE Input") != std::string::npos) {
                        result = name;
                    }
                }

                PropVariantClear(&var_name);
                props->Release();
            }

            device->Release();
        }

        if (!result.empty()) break;
    }

    collection->Release();
    enumerator->Release();
    CoUninitialize();

    return result;
}

std::string DriverInstaller::getVBCableOutputDevice() {
    HRESULT hr;
    IMMDeviceEnumerator* enumerator = NULL;
    IMMDeviceCollection* collection = NULL;

    CoInitialize(NULL);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void**)&enumerator);

    if (FAILED(hr)) {
        CoUninitialize();
        return "";
    }

    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        enumerator->Release();
        CoUninitialize();
        return "";
    }

    UINT count;
    collection->GetCount(&count);

    std::string result;

    for (UINT i = 0; i < count; i++) {
        IMMDevice* device = NULL;
        collection->Item(i, &device);

        if (device) {
            IPropertyStore* props = NULL;
            device->OpenPropertyStore(STGM_READ, &props);

            if (props) {
                PROPVARIANT var_name;
                PropVariantInit(&var_name);

                props->GetValue(PKEY_Device_FriendlyName, &var_name);

                if (var_name.vt == VT_LPWSTR) {
                    std::wstring wname(var_name.pwszVal);
                    std::string name(wname.begin(), wname.end());

                    if (name.find("CABLE Output") != std::string::npos) {
                        result = name;
                    }
                }

                PropVariantClear(&var_name);
                props->Release();
            }

            device->Release();
        }

        if (!result.empty()) break;
    }

    collection->Release();
    enumerator->Release();
    CoUninitialize();

    return result;
}

bool DriverInstaller::installVBCable() {
    if (!isRunningAsAdmin()) {
        std::cerr << "[DriverInstaller] Administrator privileges required" << std::endl;
        return false;
    }

    return installEmbeddedVBCable();
}

bool DriverInstaller::uninstallVBCable() {
    if (!isRunningAsAdmin()) {
        std::cerr << "[DriverInstaller] Administrator privileges required" << std::endl;
        return false;
    }

    return removeVBCableDriver();
}

bool DriverInstaller::isAnyDriverInstalled() {
    return isVBCableInstalled() || isSteamMicrophoneInstalled();
}

bool DriverInstaller::extractResourceToFile(const char* resource_name, const std::string& output_path) {

    HRSRC hResource = FindResourceA(NULL, resource_name, RT_RCDATA);
    if (!hResource) {
        std::cerr << "[DriverInstaller] Resource not found: " << resource_name << std::endl;
        return false;
    }

    HGLOBAL hLoadedResource = LoadResource(NULL, hResource);
    if (!hLoadedResource) {
        return false;
    }

    LPVOID pResourceData = LockResource(hLoadedResource);
    if (!pResourceData) {
        return false;
    }

    DWORD dwResourceSize = SizeofResource(NULL, hResource);
    if (dwResourceSize == 0) {
        return false;
    }

    HANDLE hFile = CreateFileA(output_path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "[DriverInstaller] Failed to create: " << output_path << std::endl;
        return false;
    }

    DWORD dwBytesWritten;
    BOOL bResult = WriteFile(hFile, pResourceData, dwResourceSize, &dwBytesWritten, NULL);
    CloseHandle(hFile);

    return (bResult && dwBytesWritten == dwResourceSize);
}

bool DriverInstaller::extractEmbeddedVBCable(const std::string& temp_dir) {
    std::cout << "[DriverInstaller] Extracting embedded VB-CABLE driver files..." << std::endl;

    std::error_code error;
    std::filesystem::remove_all(temp_dir, error);
    error.clear();
    if (!std::filesystem::create_directories(temp_dir, error) || error) {
        std::cerr << "[DriverInstaller] Failed to create temporary directory: " << temp_dir << std::endl;
        return false;
    }

    struct ResourceFile {
        const char* resource;
        const char* filename;
    };

    ResourceFile files[] = {{"IDR_DRIVER_WIN10_INF", "vbMmeCable64_win10.inf"},
                            {"IDR_DRIVER_WIN10_SYS", "vbaudio_cable64_win10.sys"},
                            {"IDR_DRIVER_WIN10_CAT", "vbaudio_cable64_win10.cat"},
                            {"IDR_DRIVER_WIN10_ARM_SYS", "vbaudio_cable64arm_win10.sys"},

                            {"IDR_DRIVER_WIN7_64_INF", "vbMmeCable64_win7.inf"},
                            {"IDR_DRIVER_WIN7_64_SYS", "vbaudio_cable64_win7.sys"},
                            {"IDR_DRIVER_WIN7_64_CAT", "vbaudio_cable64_win7.cat"},

                            {"IDR_DRIVER_VISTA_64_INF", "vbMmeCable64_vista.inf"},
                            {"IDR_DRIVER_VISTA_64_SYS", "vbaudio_cable64_vista.sys"},
                            {"IDR_DRIVER_VISTA_64_CAT", "vbaudio_cable64_vista.cat"},

                            {"IDR_DRIVER_2003_64_INF", "vbMmeCable64_2003.inf"},
                            {"IDR_DRIVER_2003_64_SYS", "vbaudio_cable64_2003.sys"},
                            {"IDR_DRIVER_2003_64_CAT", "vbaudio_cable64_2003.cat"},

                            {"IDR_DRIVER_WIN7_32_INF", "vbMmeCable_win7.inf"},
                            {"IDR_DRIVER_WIN7_32_SYS", "vbaudio_cable_win7.sys"},
                            {"IDR_DRIVER_WIN7_32_CAT", "vbaudio_cable_win7.cat"},

                            {"IDR_DRIVER_VISTA_32_INF", "vbMmeCable_vista.inf"},
                            {"IDR_DRIVER_VISTA_32_SYS", "vbaudio_cable_vista.sys"},
                            {"IDR_DRIVER_VISTA_32_CAT", "vbaudio_cable_vista.cat"},

                            {"IDR_DRIVER_XP_INF", "vbMmeCable_xp.inf"},
                            {"IDR_DRIVER_XP_SYS", "vbaudio_cable_xp.sys"},
                            {"IDR_DRIVER_XP_CAT", "vbaudio_cable_xp.cat"},

                            {"IDR_DRIVER_2003_32_INF", "vbMmeCable_2003.inf"},
                            {"IDR_DRIVER_2003_32_SYS", "vbaudio_cable_2003.sys"},
                            {"IDR_DRIVER_2003_32_CAT", "vbaudio_cable_2003.cat"},

                            {"IDR_ICON_PIN_IN", "pin_in.ico"},
                            {"IDR_ICON_PIN_OUT", "pin_out.ico"},
                            {"IDR_README_TXT", "readme.txt"}};

    int extracted = 0;
    for (const auto& file : files) {
        std::string output_path = temp_dir + "\\" + file.filename;
        if (extractResourceToFile(file.resource, output_path)) {
            extracted++;
        }
    }

    std::cout << "[DriverInstaller] Extracted " << extracted << " files to: " << temp_dir << std::endl;
    return extracted == static_cast<int>(sizeof(files) / sizeof(files[0]));
}

void DriverInstaller::cleanupTempDir(const std::string& temp_dir) {
    try {
        if (std::filesystem::exists(temp_dir)) {
            std::filesystem::remove_all(temp_dir);
            std::cout << "[DriverInstaller] Cleaned up temporary files" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[DriverInstaller] Failed to cleanup temp dir: " << e.what() << std::endl;
    }
}

bool DriverInstaller::installEmbeddedVBCable() {
    char temp_path[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, temp_path)) {
        return false;
    }

    const std::string temp_dir = std::string(temp_path) + "moonmic_vbcable";
    if (!extractEmbeddedVBCable(temp_dir)) {
        cleanupTempDir(temp_dir);
        return false;
    }

    SYSTEM_INFO system_info;
    GetNativeSystemInfo(&system_info);
    const bool use_64_bit_package = system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
                                    system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
    const char* inf_name = use_64_bit_package ? "vbMmeCable64_win10.inf" : "vbMmeCable_win7.inf";
    const std::filesystem::path inf_path = std::filesystem::path(temp_dir) / inf_name;
    const bool installed = createRootDevice("VBAudioVACWDM", inf_path.string());
    cleanupTempDir(temp_dir);
    return installed;
}

bool DriverInstaller::removeVBCableDriver() {
    removeDevicesByHardwareId("VBAudioVACWDM");

    const char* script =
        "$drivers = Get-WindowsDriver -Online -All | Where-Object { $_.OriginalFileName -like '*vbMmeCable*.inf*' }; "
        "$failed = $false; "
        "foreach ($driver in $drivers) { pnputil /delete-driver $driver.Driver /uninstall /force | Out-Host; "
        "if ($LASTEXITCODE -ne 0) { $failed = $true } }; "
        "if ($failed) { exit 1 }";
    std::string arguments = "-NoProfile -ExecutionPolicy Bypass -Command \"";
    arguments += script;
    arguments += "\"";

    SHELLEXECUTEINFOA process = {};
    process.cbSize = sizeof(process);
    process.fMask = SEE_MASK_NOCLOSEPROCESS;
    process.lpVerb = "runas";
    process.lpFile = "powershell.exe";
    process.lpParameters = arguments.c_str();
    process.nShow = SW_HIDE;
    if (!ShellExecuteExA(&process)) {
        return false;
    }

    if (!process.hProcess) {
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(process.hProcess, 60000);
    DWORD exit_code = 1;
    const bool completed = wait_result == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    return completed && exit_code == 0;
}

bool DriverInstaller::isSteamMicrophoneInstalled() {

    std::wstring targetName = L"Steam Streaming Microphone";

    bool found = false;

    HRESULT hr;
    IMMDeviceEnumerator* enumerator = NULL;

    hr = CoInitialize(NULL);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void**)&enumerator);

    if (SUCCEEDED(hr)) {

        EDataFlow flows[] = {eCapture, eRender};

        for (EDataFlow flow : flows) {
            IMMDeviceCollection* collection = NULL;

            hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATEMASK_ALL, &collection);

            if (SUCCEEDED(hr)) {
                UINT count;
                collection->GetCount(&count);

                for (UINT i = 0; i < count; i++) {
                    IMMDevice* device = NULL;
                    collection->Item(i, &device);

                    if (device) {
                        IPropertyStore* props = NULL;
                        device->OpenPropertyStore(STGM_READ, &props);

                        if (props) {
                            PROPVARIANT varName;

                            PropVariantInit(&varName);
                            props->GetValue(PKEY_Device_FriendlyName, &varName);
                            if (varName.vt == VT_LPWSTR) {
                                std::wstring wname(varName.pwszVal);

                                if (wname.find(targetName) != std::wstring::npos) {
                                    found = true;
                                }
                            }
                            PropVariantClear(&varName);

                            if (!found) {
                                PropVariantInit(&varName);
                                props->GetValue(PKEY_Device_DeviceDesc, &varName);
                                if (varName.vt == VT_LPWSTR) {
                                    std::wstring wdesc(varName.pwszVal);
                                    if (wdesc.find(targetName) != std::wstring::npos) {
                                        found = true;
                                    }
                                }
                                PropVariantClear(&varName);
                            }

                            props->Release();
                        }
                        device->Release();
                    }
                    if (found) break;
                }
                collection->Release();
            }
            if (found) break;
        }
        enumerator->Release();
    } else {
        std::cerr << "[DriverInstaller] Failed to create MMDeviceEnumerator: " << std::hex << hr << std::endl;
    }

    if (!found) {

        HDEVINFO hDevInfo = SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
        if (hDevInfo != INVALID_HANDLE_VALUE) {
            SP_DEVINFO_DATA devInfoData;
            devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

            for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
                char buffer[4096];

                if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_HARDWAREID, NULL, (PBYTE)buffer,
                                                      sizeof(buffer), NULL)) {

                    char* p = buffer;
                    while (p < buffer + sizeof(buffer) && *p) {
                        std::string hwId = p;

                        std::transform(hwId.begin(), hwId.end(), hwId.begin(),
                                       [](unsigned char c) { return std::tolower(c); });

                        if (hwId.find("steamstreamingmicrophone") != std::string::npos) {
                            found = true;
                            break;
                        }
                        p += strlen(p) + 1;
                    }
                }

                if (found) break;

                if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)buffer,
                                                      sizeof(buffer), NULL)) {
                    std::string name = buffer;
                    if (name.find("Steam Streaming Microphone") != std::string::npos) {
                        found = true;
                        break;
                    }
                }

                if (found) break;
            }
            SetupDiDestroyDeviceInfoList(hDevInfo);
        }
    }

    CoUninitialize();

    return found;
}

bool DriverInstaller::extractEmbeddedSteamDriver(const std::string& temp_dir, bool is_x64) {
    std::cout << "[DriverInstaller] Extracting embedded Steam driver files (" << (is_x64 ? "x64" : "x86") << ")..."
              << std::endl;

    std::filesystem::create_directories(temp_dir);

    struct ResourceFile {
        const char* resource;
        const char* filename;
    };

    std::vector<ResourceFile> files;

    if (is_x64) {
        files = {{"IDR_STEAM_MIC_X64_INF", "SteamStreamingMicrophone.inf"},
                 {"IDR_STEAM_MIC_X64_SYS", "SteamStreamingMicrophone.sys"},
                 {"IDR_STEAM_MIC_X64_CAT", "steamstreamingmicrophone.cat"}};
    } else {
        files = {{"IDR_STEAM_MIC_X86_INF", "SteamStreamingMicrophone.inf"},
                 {"IDR_STEAM_MIC_X86_SYS", "SteamStreamingMicrophone.sys"},
                 {"IDR_STEAM_MIC_X86_CAT", "steamstreamingmicrophone.cat"}};
    }

    int extracted = 0;
    for (const auto& file : files) {
        std::string output_path = temp_dir + "\\" + file.filename;
        if (extractResourceToFile(file.resource, output_path)) {
            extracted++;
        }
    }

    return extracted > 0;
}

bool DriverInstaller::installSteamMicrophone() {
    if (isSteamMicrophoneInstalled()) {
        std::cout << "[DriverInstaller] Steam Streaming Microphone is already installed. Skipping installation."
                  << std::endl;
        return true;
    }

    if (!isRunningAsAdmin()) {
        std::cerr << "[DriverInstaller] Administrator privileges required for Steam Driver" << std::endl;
        return false;
    }

    SYSTEM_INFO sys_info;
    GetNativeSystemInfo(&sys_info);
    const bool is_x64 = sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;

    char temp_path[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, temp_path)) {
        return false;
    }
    std::string temp_dir_path = std::string(temp_path) + "moonmic_steam_" + (is_x64 ? "x64" : "x86");

    const bool extracted = extractEmbeddedSteamDriver(temp_dir_path, is_x64);
    if (!extracted) {
        std::cerr << "[DriverInstaller] Failed to extract embedded Steam drivers." << std::endl;
        std::string arch_dir = is_x64 ? "x64" : "x86";
        std::filesystem::path base_driver_dir =
            std::filesystem::path(driver_path_).parent_path() / "drivers" / "SVACDriver";
        temp_dir_path = (base_driver_dir / arch_dir).string();

        std::cout << "[DriverInstaller] Fallback: Looking in " << temp_dir_path << std::endl;
    }

    std::filesystem::path driver_dir(temp_dir_path);

    std::filesystem::path mic_inf_path = driver_dir / "SteamStreamingMicrophone.inf";

    if (!std::filesystem::exists(mic_inf_path)) {
        std::cerr << "[DriverInstaller] Steam Microphone INF not found at: " << mic_inf_path << std::endl;
        if (extracted) {
            cleanupTempDir(temp_dir_path);
        }
        return false;
    }

    std::cout << "[DriverInstaller] Installing Steam Microphone from: " << mic_inf_path << std::endl;

    std::string cmd = "pnputil /add-driver \"" + mic_inf_path.string() + "\"";
    std::cout << "[DriverInstaller] Adding driver to store: " << cmd << std::endl;
    system(cmd.c_str());

    if (isSteamMicrophoneInstalled()) {
        std::cout << "[DriverInstaller] Device detected after adding driver." << std::endl;
        if (extracted) {
            cleanupTempDir(temp_dir_path);
        }

        disableSteamStreamingSpeakers();
        return true;
    }

    removeDevicesByHardwareId("STEAMSTREAMINGMICROPHONE");
    std::cout << "[DriverInstaller] Creating root device node..." << std::endl;
    const bool success = createRootDevice("STEAMSTREAMINGMICROPHONE", mic_inf_path.string());

    if (extracted) {
        cleanupTempDir(temp_dir_path);
    }

    if (success) {
        std::cout << "[DriverInstaller] Steam Driver installed successfully." << std::endl;

        disableSteamStreamingSpeakers();
        return true;
    } else {
        std::cerr << "[DriverInstaller] Installation failed." << std::endl;
        return false;
    }
}

bool DriverInstaller::uninstallSteamMicrophone() {
    if (!isRunningAsAdmin()) {
        std::cerr << "[DriverInstaller] Administrator privileges required" << std::endl;
        return false;
    }

    std::cout << "[DriverInstaller] Uninstalling Steam Streaming Drivers..." << std::endl;

    std::cout << "[DriverInstaller] Removing PnP Device Nodes..." << std::endl;
    removeDevicesByHardwareId("STEAMSTREAMINGMICROPHONE");

    // Installed packages use an OEM INF name, which must be resolved before removal.
    const char* ps_script = "$drivers = Get-WindowsDriver -Online -All | Where-Object { $_.OriginalFileName -like "
                            "'*SteamStreamingMicrophone.inf*' }; "
                            "if ($drivers) { "
                            "  foreach ($d in $drivers) { "
                            "    Write-Host 'Removing driver: ' $d.Driver; "
                            "    pnputil /delete-driver $d.Driver /uninstall /force | Out-Host "
                            "  } "
                            "} else { Write-Host 'No Steam Microphone drivers found.' }";

    std::string ps_args;
    ps_args.reserve(1024);
    ps_args = "-NoProfile -ExecutionPolicy Bypass -Command \"";
    ps_args += ps_script;
    ps_args += "\"";

    std::cout << "[DriverInstaller] Executing uninstall script..." << std::endl;

    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "runas";
    sei.lpFile = "powershell.exe";
    sei.lpParameters = ps_args.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExA(&sei)) {
        std::cerr << "[DriverInstaller] Failed to execute uninstall command" << std::endl;
        return false;
    }

    if (sei.hProcess) {
        DWORD waitResult = WaitForSingleObject(sei.hProcess, 60000);

        if (waitResult == WAIT_TIMEOUT) {
            std::cout << "[DriverInstaller] Uninstall command timed out (still running in background)" << std::endl;
            CloseHandle(sei.hProcess);
            return true;
        }

        DWORD exit_code;
        GetExitCodeProcess(sei.hProcess, &exit_code);
        CloseHandle(sei.hProcess);

        if (exit_code == 0) {
            std::cout << "[DriverInstaller] Uninstall completed successfully" << std::endl;
            return true;
        }
    }

    return false;
}

bool DriverInstaller::createRootDevice(const std::string& hardwareId, const std::string& infPath) {
    GUID classGuid = GUID_NULL;
    char className[MAX_CLASS_NAME_LEN];

    if (!SetupDiGetINFClassA(infPath.c_str(), &classGuid, className, MAX_CLASS_NAME_LEN, NULL)) {
        std::cerr << "[DriverInstaller] Failed to get class GUID from INF. Error: " << GetLastError() << std::endl;
        return false;
    }

    HDEVINFO hDevInfo = SetupDiCreateDeviceInfoList(&classGuid, NULL);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        std::cerr << "[DriverInstaller] Failed to create device info list. Error: " << GetLastError() << std::endl;
        return false;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    if (!SetupDiCreateDeviceInfoA(hDevInfo, className, &classGuid, NULL, NULL, DICD_GENERATE_ID, &devInfoData)) {
        std::cerr << "[DriverInstaller] Failed to create device info. Error: " << GetLastError() << std::endl;
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    // Hardware ID must be a REG_MULTI_SZ (double null terminated)
    std::vector<char> hwIdBuffer(hardwareId.length() + 2, 0);
    memcpy(hwIdBuffer.data(), hardwareId.c_str(), hardwareId.length());

    if (!SetupDiSetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_HARDWAREID, (const BYTE*)hwIdBuffer.data(),
                                           (DWORD)hwIdBuffer.size())) {
        std::cerr << "[DriverInstaller] Failed to set Hardware ID. Error: " << GetLastError() << std::endl;
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, hDevInfo, &devInfoData)) {
        std::cerr << "[DriverInstaller] Failed to register device. Error: " << GetLastError() << std::endl;
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    Sleep(1000);

    BOOL rebootRequired = FALSE;
    BOOL result = UpdateDriverForPlugAndPlayDevicesA(NULL, hardwareId.c_str(), infPath.c_str(), INSTALLFLAG_FORCE,
                                                     &rebootRequired);

    if (!result) {
        DWORD err = GetLastError();
        std::cerr << "[DriverInstaller] UpdateDriverForPlugAndPlayDevices failed: " << err << std::endl;

        std::cerr << "[DriverInstaller] Cleaning up failed device creation..." << std::endl;
        SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, &devInfoData);
    } else {
        std::cout << "[DriverInstaller] Root device created and driver installed." << std::endl;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return result == TRUE;
}

void DriverInstaller::removeDevicesByHardwareId(const std::string& hardwareId) {
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE) return;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        char buffer[4096];
        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_HARDWAREID, NULL, (PBYTE)buffer,
                                              sizeof(buffer), NULL)) {
            char* p = buffer;
            bool match = false;
            while (p < buffer + sizeof(buffer) && *p) {
                std::string id = p;

                std::transform(id.begin(), id.end(), id.begin(), ::tolower);
                std::string target = hardwareId;
                std::transform(target.begin(), target.end(), target.begin(), ::tolower);

                if (id == target) {
                    match = true;
                    break;
                }
                p += strlen(p) + 1;
            }

            if (match) {
                std::cout << "[DriverInstaller] Removing existing device with ID: " << hardwareId << std::endl;
                SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, &devInfoData);

                i = -1;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
}

bool DriverInstaller::disableSteamStreamingSpeakers() {
    HRESULT hr;
    IMMDeviceEnumerator* enumerator = NULL;
    IPolicyConfig* policyConfig = NULL;
    bool found = false;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void**)&enumerator);
    if (FAILED(hr)) return false;

    hr = CoCreateInstance(CLSID_PolicyConfig, NULL, CLSCTX_ALL, IID_IPolicyConfig, (void**)&policyConfig);
    if (FAILED(hr)) {
        enumerator->Release();
        std::cerr << "[DriverInstaller] Failed to create IPolicyConfig" << std::endl;
        return false;
    }

    IMMDeviceCollection* collection = NULL;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);

    if (SUCCEEDED(hr)) {
        UINT count;
        collection->GetCount(&count);

        for (UINT i = 0; i < count; i++) {
            IMMDevice* device = NULL;
            collection->Item(i, &device);

            if (device) {
                IPropertyStore* props = NULL;
                device->OpenPropertyStore(STGM_READ, &props);

                if (props) {
                    PROPVARIANT varName;
                    PropVariantInit(&varName);
                    props->GetValue(PKEY_Device_FriendlyName, &varName);

                    if (varName.vt == VT_LPWSTR) {
                        std::wstring wname(varName.pwszVal);

                        if (wname.find(L"Steam Streaming Microphone") != std::wstring::npos) {
                            LPWSTR id = NULL;
                            device->GetId(&id);
                            if (id) {
                                std::wcout << L"[DriverInstaller] Disabling Playback endpoint: " << wname << std::endl;

                                hr = policyConfig->SetEndpointVisibility(id, 0);
                                if (SUCCEEDED(hr)) {
                                    std::cout << "[DriverInstaller] Successfully disabled playback endpoint."
                                              << std::endl;
                                    found = true;
                                } else {
                                    std::cerr << "[DriverInstaller] Failed to disable endpoint. HR=" << std::hex << hr
                                              << std::endl;
                                }
                                CoTaskMemFree(id);
                            }
                        }
                    }
                    PropVariantClear(&varName);
                    props->Release();
                }
                device->Release();
            }
            if (found) break;
        }
        collection->Release();
    }

    policyConfig->Release();
    enumerator->Release();
    return found;
}

} // namespace moonmic
