#pragma once

#include <d3d9.h>
#include <d3dx9.h>
#include <array>
#include <vector>

#include "CVector.h"

class CPed;
class DxDrawPrimitives;

class GpsRenderer
{
public:
    struct Color
    {
        int red;
        int green;
        int blue;
        int alpha;
    };

    static void LoadSettings(const char* iniPath);

    void Render(DxDrawPrimitives* draw, CPed* player,
        const D3DXVECTOR3& cameraPos, const D3DXVECTOR3& cameraRot,
        float fov, float nearPlane, float farPlane, float aspect, float renderTargetHeight);
    void RenderDistance(DxDrawPrimitives* draw, float radarX, float radarY,
        float radarWidth, float radarHeight);

private:
    struct Route
    {
        int traceIndex = -1;
        unsigned short traceCounter = 0;
        bool waypoint = false;
        size_t progressSegment = 0;
        std::vector<CVector> points;
        Color color{};
        float distance = 0.0f;
    };

    void UpdateRoutes(CPed* player);

    std::vector<Route> m_routes;
    unsigned int m_nextUpdateTime = 0;
    float m_displayDistance = 0.0f;

    static bool s_respectTrafficLaneDirection;
    static float s_lineWidth;
    static bool s_outlineEnabled;
    static float s_outlineThickness;
    static Color s_outlineColor;
    static bool s_enableOnBicycles;
    static bool s_enableOnBoats;
    static bool s_trackMovingTargets;
    static float s_removeRadius;
    static bool s_displayDistance;
    static int s_distanceUnits;
    static bool s_customColorsEnabled;
    static std::array<Color, 8> s_customColors;
    static bool s_enableLog;
};
