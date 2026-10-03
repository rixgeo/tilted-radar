#include "RadarMod.h"

#include <Windows.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <Events.h>
#include <Patch.h>
#include <rwcore.h>

#include "Radar.h"
#include "radar/map/BlipManager.h"
#include "radar/render/GpsRenderer.h"
#include "render/RadarRenderer.h"

void _rwD3D9RenderStateFlushCache(void);

namespace {
constexpr std::uintptr_t kHudRadarFunction = 0x58A330;
RadarMod* g_radarMod = nullptr;

void __cdecl DrawRadarReplacement() {
    if (g_radarMod != nullptr) {
        g_radarMod->DrawRadar();
    }
}

void Log(const char* message) {
    OutputDebugStringA("[TiltedRadar] ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

std::string GetIniPath() {
    std::array<char, 32768> executablePath{};
    const DWORD length = GetModuleFileNameA(nullptr, executablePath.data(),
                                            static_cast<DWORD>(executablePath.size()));
    if (length == 0 || length >= executablePath.size()) {
        return ".\\TiltedRadar.ini";
    }

    std::string path(executablePath.data(), length);
    const std::size_t separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".\\TiltedRadar.ini";
    }
    path.resize(separator + 1);
    path += "TiltedRadar.ini";
    return path;
}
}

namespace Ui {
HWND GameWindow() {
    return GetActiveWindow();
}

void PoisonRwShaderCache() {
    _rwD3D9RenderStateFlushCache();
}
}

void RadarRenderer::RenderRadioText() {
}

RadarMod::RadarMod() : m_radar(std::make_unique<Radar>()) {
    g_radarMod = this;
    plugin::Events::initRwEvent += [this] {
        Initialize();
    };
    plugin::Events::d3dLostEvent += [this] {
        OnDeviceLost();
    };
    plugin::Events::d3dResetEvent += [this] {
        OnDeviceReset();
    };
    plugin::Events::shutdownRwEvent += [this] {
        Shutdown();
    };
}

RadarMod::~RadarMod() {
    if (g_radarMod == this) {
        g_radarMod = nullptr;
    }
}

bool RadarMod::IsEnabled() const {
    char value[16]{};
    const DWORD length = GetPrivateProfileStringA(
        "TiltedRadar", "EnableMod", "1", value, static_cast<DWORD>(sizeof(value)),
        GetIniPath().c_str());
    return length > 0 && value[0] == '1' && value[1] == '\0';
}

namespace {
float ReadTiltOffset() {
    char value[32]{};
    GetPrivateProfileStringA("TiltedRadar", "TiltAngle", "0.0", value,
                             static_cast<DWORD>(sizeof(value)), GetIniPath().c_str());
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || *end != '\0' || !std::isfinite(parsed) ||
        parsed < -15.0F || parsed > 15.0F) {
        Log("TiltAngle must be a finite pitch offset from -15 to 15 degrees; using 0 degrees.");
        return 0.0F;
    }
    return parsed;
}

int ReadRadarSizeOption(const char* key) {
    char value[32]{};
    GetPrivateProfileStringA("TiltedRadar", key, "-1", value,
                             static_cast<DWORD>(sizeof(value)), GetIniPath().c_str());
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(value, &end, 10);
    if (end == value || *end != '\0' || errno == ERANGE ||
        (parsed != -1 && parsed <= 0) || parsed > 2147483647LL) {
        char message[128]{};
        sprintf_s(message, "%s must be -1 (default) or a positive integer; using -1.",
                  key);
        Log(message);
        return -1;
    }
    return static_cast<int>(parsed);
}

int ReadRadarRingThickness() {
    char value[32]{};
    GetPrivateProfileStringA("TiltedRadar", "RadarRingThickness", "-1", value,
                             static_cast<DWORD>(sizeof(value)), GetIniPath().c_str());
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(value, &end, 10);
    if (end == value || *end != '\0' || errno == ERANGE ||
        parsed < -1 || parsed > 2147483647LL) {
        Log("RadarRingThickness must be -1 (default) or a non-negative integer; using -1.");
        return -1;
    }
    return static_cast<int>(parsed);
}

std::uint32_t ReadWaterColor() {
    char value[16]{};
    GetPrivateProfileStringA("TiltedRadar", "WaterColor", "6f8aa9", value,
                             static_cast<DWORD>(sizeof(value)), GetIniPath().c_str());
    const char* digits = value;
    if (digits[0] == '#') {
        ++digits;
    }
    if (std::strlen(digits) != 6) {
        Log("WaterColor must be a six-digit RGB hex value; using 6f8aa9.");
        return 0x6F8AA9;
    }

    std::uint32_t color = 0;
    for (const char* digit = digits; *digit != '\0'; ++digit) {
        color <<= 4;
        if (*digit >= '0' && *digit <= '9') {
            color |= static_cast<std::uint32_t>(*digit - '0');
        } else if (*digit >= 'a' && *digit <= 'f') {
            color |= static_cast<std::uint32_t>(*digit - 'a' + 10);
        } else if (*digit >= 'A' && *digit <= 'F') {
            color |= static_cast<std::uint32_t>(*digit - 'A' + 10);
        } else {
            Log("WaterColor must be a six-digit RGB hex value; using 6f8aa9.");
            return 0x6F8AA9;
        }
    }
    return color;
}

int ReadWaterAlpha() {
    char value[16]{};
    GetPrivateProfileStringA("TiltedRadar", "WaterAlpha", "255", value,
                             static_cast<DWORD>(sizeof(value)), GetIniPath().c_str());
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || errno == ERANGE || parsed < 0 || parsed > 255) {
        Log("WaterAlpha must be an integer from 0 to 255; using 255.");
        return 255;
    }
    return static_cast<int>(parsed);
}

float ReadMarkerSize() {
    char value[32]{};
    GetPrivateProfileStringA("TiltedRadar", "MarkerSize", "4.4", value,
                             static_cast<DWORD>(sizeof(value)), GetIniPath().c_str());
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || *end != '\0' || !std::isfinite(parsed)
        || parsed < 0.1F || parsed > 100.0F) {
        Log("MarkerSize must be a finite value from 0.1 to 100; using 4.4.");
        return 4.4F;
    }
    return parsed;
}

bool ReadMarkerOutline() {
    char value[16]{};
    GetPrivateProfileStringA("TiltedRadar", "MarkerOutline", "1", value,
                             static_cast<DWORD>(sizeof(value)), GetIniPath().c_str());
    if (std::strcmp(value, "0") == 0) {
        return false;
    }
    if (std::strcmp(value, "1") == 0) {
        return true;
    }
    Log("MarkerOutline must be 0 or 1; using 1.");
    return true;
}
}

void RadarMod::Initialize() {
    m_enabled = IsEnabled();
    if (!m_enabled) {
        Log("Radar-only renderer disabled by TiltedRadar.ini.");
        return;
    }

    if (m_radar->IsInitialized()) {
        m_radar->Shutdown();
    }
    RadarConfig::SetRadar3D(true, false);
    RadarConfig::SetCustomRadarTxd(false, false);
    RadarConfig::SetRadarSizeX(ReadRadarSizeOption("RadarSizeX"));
    RadarConfig::SetRadarSizeY(ReadRadarSizeOption("RadarSizeY"));
    RadarConfig::SetRadarRingThickness(ReadRadarRingThickness());
    BlipManager::SetMissionMarkerOptions(ReadMarkerSize(), ReadMarkerOutline());
    GpsRenderer::LoadSettings(GetIniPath().c_str());
    const std::uint32_t waterColor = ReadWaterColor();
    RadarConfig::SetBackgroundColor(
        static_cast<int>((waterColor >> 16) & 0xFF),
        static_cast<int>((waterColor >> 8) & 0xFF),
        static_cast<int>(waterColor & 0xFF), ReadWaterAlpha(), false);
    const float pitchOffset = ReadTiltOffset();
    RadarConfig::SetCamPitchDeg(RadarConfig::RadarCamContext::Ped, pitchOffset, false);
    RadarConfig::SetCamPitchDeg(RadarConfig::RadarCamContext::Vehicle, pitchOffset, false);
    RadarConfig::SetCamPitchDeg(RadarConfig::RadarCamContext::Plane, pitchOffset, false);
    RadarConfig::SetCamPitchDeg(RadarConfig::RadarCamContext::Heli, pitchOffset, false);

    auto* device = static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice());
    if (device == nullptr) {
        Log("RenderWare has no active Direct3D 9 device; radar initialization is deferred.");
        return;
    }

    if (!m_radar->Initialize(device)) {
        Log("Radar renderer initialization failed; native radar remains active.");
        return;
    }

    if (!m_drawHookInstalled) {
        plugin::patch::RedirectJump(kHudRadarFunction, &DrawRadarReplacement);
        m_drawHookInstalled = true;
    }
    Log("Radar-only renderer initialized.");
}

void RadarMod::Shutdown() {
    if (m_radar != nullptr && m_radar->IsInitialized()) {
        m_radar->Shutdown();
    }
}

void RadarMod::OnDeviceLost() {
    Shutdown();
}

void RadarMod::OnDeviceReset() {
    if (m_enabled) {
        Initialize();
    }
}

void RadarMod::DrawRadar() {
    if (m_enabled && m_radar != nullptr && m_radar->IsInitialized()) {
        m_radar->Render();
    }
}
