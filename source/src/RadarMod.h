#pragma once

#include <memory>

class Radar;

class RadarMod final {
public:
    RadarMod();
    ~RadarMod();

    RadarMod(const RadarMod&) = delete;
    RadarMod& operator=(const RadarMod&) = delete;

    void DrawRadar();

private:
    void Initialize();
    void Shutdown();
    void OnDeviceLost();
    void OnDeviceReset();
    bool IsEnabled() const;

    std::unique_ptr<Radar> m_radar;
    bool m_enabled{};
    bool m_drawHookInstalled{};
};
