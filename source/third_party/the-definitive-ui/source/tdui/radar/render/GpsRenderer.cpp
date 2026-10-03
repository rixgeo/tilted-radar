#include "GpsRenderer.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "BlipManager.h"
#include "CModelInfo.h"
#include "CNodeAddress.h"
#include "CPathFind.h"
#include "CPathNode.h"
#include "CPed.h"
#include "CPools.h"
#include "CRadar.h"
#include "CTheScripts.h"
#include "CTimer.h"
#include "CVector.h"
#include "CVehicle.h"
#include "CEntryExit.h"
#include "CMenuManager.h"
#include "ColorUtils.h"
#include "DxDrawPrimitives.h"
#include "Patch.h"
#include "RadarGeometry.h"
#include "common.h"

namespace
{
    constexpr int kMaxNodes = 5000;
    constexpr unsigned int kRouteRefreshMs = 150;
    constexpr size_t kMaxRoutes = 6;
    constexpr size_t kProgressSearchWindow = 8;
    std::array<char, 1024> g_pathNodesToStream{};
    std::array<int, 50000> g_pathNodes{};
    bool g_pathfindingPatchesInstalled = false;

    void InstallPathfindingPatches()
    {
        if (g_pathfindingPatchesInstalled)
            return;
        g_pathNodesToStream.fill(1);
        g_pathNodes.fill(-1);
        plugin::patch::SetPointer(0x44DE3C, g_pathNodesToStream.data());
        plugin::patch::SetPointer(0x450D03, g_pathNodesToStream.data());
        plugin::patch::SetPointer(0x451782, g_pathNodes.data());
        plugin::patch::SetPointer(0x451904, g_pathNodes.data());
        plugin::patch::SetPointer(0x451AC3, g_pathNodes.data());
        plugin::patch::SetPointer(0x451B33, g_pathNodes.data());
        plugin::patch::SetUInt(0x4518F8, static_cast<unsigned int>(g_pathNodes.size()));
        plugin::patch::SetUInt(0x4519B0, static_cast<unsigned int>(g_pathNodes.size() - 50));
        g_pathfindingPatchesInstalled = true;
    }

    void LogConfigError(const char* option)
    {
        char message[160]{};
        sprintf_s(message, "[TiltedRadar] Invalid GPS setting '%s'; using its default.\n", option);
        OutputDebugStringA(message);
    }

    bool ReadBool(const char* iniPath, const char* name, bool fallback)
    {
        char value[24]{};
        const char* defaultValue = fallback ? "1" : "0";
        GetPrivateProfileStringA("GPS", name, defaultValue, value, sizeof(value), iniPath);
        if (std::strcmp(value, "0") == 0)
            return false;
        if (std::strcmp(value, "1") == 0)
            return true;
        LogConfigError(name);
        return fallback;
    }

    float ReadFloat(const char* iniPath, const char* name, float fallback,
        float minimum, float maximum)
    {
        char value[32]{};
        char defaultValue[32]{};
        sprintf_s(defaultValue, "%.3f", fallback);
        GetPrivateProfileStringA("GPS", name, defaultValue, value, sizeof(value), iniPath);
        char* end = nullptr;
        const float parsed = std::strtof(value, &end);
        if (end == value || *end != '\0' || !std::isfinite(parsed)
            || parsed < minimum || parsed > maximum)
        {
            LogConfigError(name);
            return fallback;
        }
        return parsed;
    }

    int ReadInt(const char* iniPath, const char* name, int fallback, int minimum, int maximum)
    {
        char value[24]{};
        char defaultValue[24]{};
        sprintf_s(defaultValue, "%d", fallback);
        GetPrivateProfileStringA("GPS", name, defaultValue, value, sizeof(value), iniPath);
        char* end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        if (end == value || *end != '\0' || parsed < minimum || parsed > maximum)
        {
            LogConfigError(name);
            return fallback;
        }
        return static_cast<int>(parsed);
    }

    GpsRenderer::Color ReadColor(const char* iniPath, const char* name,
        GpsRenderer::Color fallback)
    {
        char value[64]{};
        GetPrivateProfileStringA("GPS", name, "", value, sizeof(value), iniPath);
        int channels[4]{};
        char* current = value;
        for (int i = 0; i < 4; ++i)
        {
            while (*current == ' ' || *current == '\t')
                ++current;
            char* end = nullptr;
            const long parsed = std::strtol(current, &end, 10);
            if (end == current || parsed < 0 || parsed > 255)
            {
                LogConfigError(name);
                return fallback;
            }
            channels[i] = static_cast<int>(parsed);
            current = end;
            while (*current == ' ' || *current == '\t')
                ++current;
            if (i < 3)
            {
                if (*current != ',')
                {
                    LogConfigError(name);
                    return fallback;
                }
                ++current;
            }
        }
        while (*current == ' ' || *current == '\t')
            ++current;
        if (*current != '\0')
        {
            LogConfigError(name);
            return fallback;
        }
        return { channels[0], channels[1], channels[2], channels[3] };
    }

    bool GetTraceWorldPos(const tRadarTrace& trace, CVector& out)
    {
        if (trace.m_nBlipType == BLIP_CHAR && trace.m_nEntityHandle)
        {
            if (CPed* ped = CPools::GetPed(static_cast<int>(trace.m_nEntityHandle)))
            {
                out = ped->GetPosition();
                return true;
            }
        }
        else if (trace.m_nBlipType == BLIP_CAR && trace.m_nEntityHandle)
        {
            if (CVehicle* vehicle = CPools::GetVehicle(static_cast<int>(trace.m_nEntityHandle)))
            {
                out = vehicle->GetPosition();
                return true;
            }
        }
        out = trace.m_vecPos;
        if (trace.m_pEntryExit)
            trace.m_pEntryExit->GetPositionRelativeToOutsideWorld(out);
        return true;
    }

    DWORD PackColor(const GpsRenderer::Color& color)
    {
        return tocolor(color.red, color.green, color.blue, color.alpha);
    }

    size_t CustomColorIndex(const tRadarTrace& trace)
    {
        switch (trace.m_nColour)
        {
        case BLIP_COLOUR_RED: return 1;
        case BLIP_COLOUR_GREEN: return 2;
        case BLIP_COLOUR_BLUE: return 3;
        case BLIP_COLOUR_WHITE: return 4;
        case BLIP_COLOUR_YELLOW:
        case BLIP_COLOUR_DESTINATION: return 5;
        case BLIP_COLOUR_REDCOPY: return 6;
        case BLIP_COLOUR_BLUECOPY: return 7;
        case BLIP_COLOUR_THREAT: return trace.m_bFriendly ? 3 : 1;
        default: return 5;
        }
    }

    float Distance2D(const CVector& a, const CVector& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }
}

bool GpsRenderer::s_respectTrafficLaneDirection = true;
float GpsRenderer::s_lineWidth = 2.5f;
bool GpsRenderer::s_outlineEnabled = true;
float GpsRenderer::s_outlineThickness = 90.0f;
GpsRenderer::Color GpsRenderer::s_outlineColor = { 255, 255, 255, 255 };
bool GpsRenderer::s_enableOnBicycles = true;
bool GpsRenderer::s_enableOnBoats = true;
bool GpsRenderer::s_trackMovingTargets = false;
float GpsRenderer::s_removeRadius = 40.0f;
bool GpsRenderer::s_displayDistance = false;
int GpsRenderer::s_distanceUnits = 0;
bool GpsRenderer::s_customColorsEnabled = false;
std::array<GpsRenderer::Color, 8> GpsRenderer::s_customColors = {{
    { 180, 24, 24, 255 },
    { 255, 0, 0, 255 },
    { 0, 255, 0, 255 },
    { 0, 0, 255, 255 },
    { 255, 255, 255, 255 },
    { 255, 255, 0, 255 },
    { 255, 0, 255, 255 },
    { 0, 255, 255, 255 }
}};
bool GpsRenderer::s_enableLog = false;

void GpsRenderer::LoadSettings(const char* iniPath)
{
    if (!iniPath || !*iniPath)
        return;
    InstallPathfindingPatches();
    s_respectTrafficLaneDirection = ReadBool(iniPath, "respectTrafficLaneDirection", true);
    s_lineWidth = ReadFloat(iniPath, "lineWidth", 2.5f, 0.25f, 50.0f);
    s_outlineEnabled = ReadBool(iniPath, "GPSOutline", true);
    s_outlineThickness = ReadFloat(iniPath, "GPSOutlineThickness", 90.0f, 0.0f, 1000.0f);
    s_outlineColor = {
        ReadInt(iniPath, "GPSOutlineR", 255, 0, 255),
        ReadInt(iniPath, "GPSOutlineG", 255, 0, 255),
        ReadInt(iniPath, "GPSOutlineB", 255, 0, 255),
        ReadInt(iniPath, "GPSOutlineAlpha", 255, 0, 255)
    };
    s_enableOnBicycles = ReadBool(iniPath, "enableOnBicycles", true);
    s_enableOnBoats = ReadBool(iniPath, "enableOnBoats", true);
    s_trackMovingTargets = ReadBool(iniPath, "trackMovingTargets", false);
    s_removeRadius = ReadFloat(iniPath, "removeRadius", 40.0f, 0.0f, 10000.0f);
    s_displayDistance = ReadBool(iniPath, "displayDistance", false);
    s_distanceUnits = ReadInt(iniPath, "distanceUnits", 0, 0, 1);
    s_customColorsEnabled = ReadBool(iniPath, "enabled", false);
    s_customColors = {{
        ReadColor(iniPath, "waypoint", { 180, 24, 24, 255 }),
        ReadColor(iniPath, "red", { 255, 0, 0, 255 }),
        ReadColor(iniPath, "green", { 0, 255, 0, 255 }),
        ReadColor(iniPath, "blue", { 0, 0, 255, 255 }),
        ReadColor(iniPath, "white", { 255, 255, 255, 255 }),
        ReadColor(iniPath, "yellow", { 255, 255, 0, 255 }),
        ReadColor(iniPath, "purple", { 255, 0, 255, 255 }),
        ReadColor(iniPath, "cyan", { 0, 255, 255, 255 })
    }};
    s_enableLog = ReadBool(iniPath, "enableLog", false);
}

void GpsRenderer::UpdateRoutes(CPed* player)
{
    m_routes.clear();
    m_displayDistance = 0.0f;
    if (!player || !CRadar::ms_RadarTrace)
        return;

    const CVector playerPos = FindPlayerCoors(0);
    const int targetHandle = FrontEndMenuManager.m_nTargetBlipIndex;
    int waypointIndex = -1;
    if (targetHandle)
    {
        waypointIndex = CRadar::GetActualBlipArrayIndex(targetHandle);
        if (waypointIndex >= 0 && waypointIndex < static_cast<int>(MAX_RADAR_TRACES))
        {
            tRadarTrace& waypoint = CRadar::ms_RadarTrace[waypointIndex];
            CVector destination{};
            if (waypoint.m_bInUse && GetTraceWorldPos(waypoint, destination)
                && s_removeRadius > 0.0f && Distance2D(playerPos, destination) <= s_removeRadius)
            {
                CRadar::ClearBlip(targetHandle);
                waypointIndex = -1;
                if (s_enableLog)
                    OutputDebugStringA("[TiltedRadar] Waypoint cleared inside removeRadius.\n");
            }
        }
    }

    if (!player->bInVehicle || !player->m_pVehicle)
        return;
    CVehicle* vehicle = player->m_pVehicle;
    const int vehicleClass = vehicle->m_nVehicleSubClass;
    if (CModelInfo::IsPlaneModel(vehicle->m_nModelIndex)
        || CModelInfo::IsHeliModel(vehicle->m_nModelIndex)
        || (vehicleClass == VEHICLE_BMX && !s_enableOnBicycles)
        || (vehicleClass == VEHICLE_BOAT && !s_enableOnBoats))
        return;

    auto addRoute = [&](int traceIndex, const tRadarTrace& trace, bool waypoint) {
        if (m_routes.size() >= kMaxRoutes)
            return;
        CVector destination{};
        if (!GetTraceWorldPos(trace, destination))
            return;

        Route route;
        route.traceIndex = traceIndex;
        route.traceCounter = trace.m_nCounter;
        route.waypoint = waypoint;
        if (waypoint && s_customColorsEnabled)
            route.color = s_customColors[0];
        else if (s_customColorsEnabled)
            route.color = s_customColors[CustomColorIndex(trace)];
        else
        {
            const CRGBA native = CRadar::GetRadarTraceColour(
                trace.m_nColour, trace.m_bBright ? 1 : 0, trace.m_bFriendly ? 1 : 0);
            route.color = { native.r, native.g, native.b, native.a };
            if (waypoint)
                route.color = { 180, 24, 24, 255 };
        }

        std::array<CNodeAddress, kMaxNodes> nodes{};
        short nodeCount = 0;
        float pathDistance = 0.0f;
        ThePaths.DoPathSearch(
            0, playerPos, CNodeAddress(), destination,
            nodes.data(), &nodeCount, kMaxNodes, &pathDistance,
            999999.0f, nullptr, 999999.0f, s_respectTrafficLaneDirection,
            CNodeAddress(), false, vehicleClass == VEHICLE_BOAT);
        if (nodeCount <= 0)
            return;

        route.points.reserve(static_cast<size_t>(nodeCount) + 2);
        route.points.push_back(playerPos);
        for (short i = 0; i < nodeCount; ++i)
        {
            CPathNode* node = ThePaths.GetPathNode(nodes[static_cast<size_t>(i)]);
            if (node)
                route.points.push_back(node->GetNodeCoors());
        }
        route.points.push_back(destination);
        if (route.points.size() < 2)
            return;

        route.distance = pathDistance;
        if (route.distance <= 0.0f)
        {
            for (size_t i = 1; i < route.points.size(); ++i)
                route.distance += Distance2D(route.points[i - 1], route.points[i]);
        }
        if (waypoint || m_displayDistance == 0.0f || route.distance < m_displayDistance)
            m_displayDistance = route.distance;
        m_routes.push_back(std::move(route));
    };

    if (waypointIndex >= 0 && waypointIndex < static_cast<int>(MAX_RADAR_TRACES))
        addRoute(waypointIndex, CRadar::ms_RadarTrace[waypointIndex], true);

    for (unsigned int i = 0; i < MAX_RADAR_TRACES && m_routes.size() < kMaxRoutes; ++i)
    {
        const tRadarTrace& trace = CRadar::ms_RadarTrace[i];
        if (!trace.m_bInUse || static_cast<int>(i) == waypointIndex)
            continue;
        if ((trace.m_nBlipType == BLIP_COORD || trace.m_nBlipType == BLIP_CONTACTPOINT)
            && BlipManager::IsMissionCheckpointSprite(trace.m_nRadarSprite))
        {
            addRoute(static_cast<int>(i), trace, false);
        }
        else if (s_trackMovingTargets && CTheScripts::IsPlayerOnAMission()
            && (trace.m_nBlipType == BLIP_CHAR || trace.m_nBlipType == BLIP_CAR))
        {
            addRoute(static_cast<int>(i), trace, false);
        }
    }

    if (s_enableLog && !m_routes.empty())
        OutputDebugStringA("[TiltedRadar] Navigation routes refreshed.\n");
}

void GpsRenderer::Render(DxDrawPrimitives* draw, CPed* player,
    const D3DXVECTOR3& cameraPos, const D3DXVECTOR3& cameraRot,
    float fov, float nearPlane, float farPlane, float aspect, float renderTargetHeight)
{
    if (!draw)
        return;

    const unsigned int now = CTimer::m_snTimeInMilliseconds;
    if (m_nextUpdateTime == 0 || static_cast<int>(now - m_nextUpdateTime) >= 0)
    {
        UpdateRoutes(player);
        m_nextUpdateTime = now + kRouteRefreshMs;
    }

    const CVector playerPosition = player ? player->GetPosition() : CVector(0.0f, 0.0f, 0.0f);
    m_displayDistance = 0.0f;
    for (Route& route : m_routes)
    {
        if (route.points.size() < 2)
            continue;

        const size_t firstSegment = (std::min)(route.progressSegment, route.points.size() - 2);
        const size_t lastSegment = (std::min)(
            firstSegment + kProgressSearchWindow, route.points.size() - 2);
        size_t closestSegment = firstSegment;
        float closestT = 0.0f;
        D3DXVECTOR3 closestPoint{};
        float closestDistanceSquared = FLT_MAX;
        for (size_t segment = firstSegment; segment <= lastSegment; ++segment)
        {
            const CVector& a = route.points[segment];
            const CVector& b = route.points[segment + 1];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float lengthSquared = dx * dx + dy * dy;
            if (lengthSquared <= 0.001f)
                continue;
            const float t = (std::max)(0.0f, (std::min)(1.0f,
                ((playerPosition.x - a.x) * dx + (playerPosition.y - a.y) * dy) / lengthSquared));
            const float projectedX = a.x + t * dx;
            const float projectedY = a.y + t * dy;
            const float errorX = playerPosition.x - projectedX;
            const float errorY = playerPosition.y - projectedY;
            const float distanceSquared = errorX * errorX + errorY * errorY;
            if (distanceSquared < closestDistanceSquared)
            {
                closestDistanceSquared = distanceSquared;
                closestSegment = segment;
                closestT = t;
                closestPoint = D3DXVECTOR3(projectedX, projectedY, 0.04f);
            }
        }
        route.progressSegment = closestSegment;

        std::vector<D3DXVECTOR3> visiblePoints;
        visiblePoints.reserve(route.points.size() - closestSegment + 1);
        visiblePoints.emplace_back(playerPosition.x + 3000.0f, playerPosition.y - 3000.0f, 0.04f);
        if (closestDistanceSquared < FLT_MAX
            && closestDistanceSquared > 0.25f
            && closestT > 0.01f)
        {
            closestPoint.x += 3000.0f;
            closestPoint.y -= 3000.0f;
            visiblePoints.push_back(closestPoint);
        }
        for (size_t pointIndex = closestSegment + 1; pointIndex < route.points.size(); ++pointIndex)
        {
            const CVector& point = route.points[pointIndex];
            visiblePoints.emplace_back(point.x + 3000.0f, point.y - 3000.0f, 0.04f);
        }

        float visibleDistance = 0.0f;
        for (size_t pointIndex = 1; pointIndex < visiblePoints.size(); ++pointIndex)
        {
            const D3DXVECTOR3 delta = visiblePoints[pointIndex] - visiblePoints[pointIndex - 1];
            visibleDistance += std::sqrt(delta.x * delta.x + delta.y * delta.y);
        }
        if (route.waypoint || m_displayDistance == 0.0f || visibleDistance < m_displayDistance)
            m_displayDistance = visibleDistance;

        const float worldUnitsPerPixel = renderTargetHeight > 0.0f
            ? (2.0f * std::fabs(cameraPos.z) * std::tan(fov * 0.5f) / renderTargetHeight)
            : 1.0f;
        const float lineWidth = s_lineWidth * worldUnitsPerPixel;
        if (s_outlineEnabled && s_outlineThickness > 0.0f)
        {
            const float outlineWidth = lineWidth * (1.0f + 2.0f * s_outlineThickness / 100.0f);
            draw->dxDrawPolyline3D(visiblePoints, outlineWidth, PackColor(s_outlineColor),
                cameraPos, cameraRot, fov, nearPlane, farPlane, aspect);
        }
        draw->dxDrawPolyline3D(visiblePoints, lineWidth, PackColor(route.color),
            cameraPos, cameraRot, fov, nearPlane, farPlane, aspect);
    }
}

void GpsRenderer::RenderDistance(DxDrawPrimitives* draw, float radarX, float radarY,
    float radarWidth, float radarHeight)
{
    if (!draw || !s_displayDistance || m_displayDistance <= 0.0f)
        return;

    char label[64]{};
    if (s_distanceUnits == 0)
    {
        if (m_displayDistance >= 1000.0f)
            sprintf_s(label, "GPS: %.1f km", m_displayDistance / 1000.0f);
        else
            sprintf_s(label, "GPS: %.0f m", m_displayDistance);
    }
    else
    {
        const float miles = m_displayDistance / 1609.344f;
        if (miles >= 0.1f)
            sprintf_s(label, "GPS: %.1f mi", miles);
        else
            sprintf_s(label, "GPS: %.0f ft", m_displayDistance * 3.28084f);
    }
    const float scale = RadarGeometry::GetRadarScale();
    const float textWidth = 150.0f * scale;
    const float textHeight = 20.0f * scale;
    const float x = radarX + (radarWidth - textWidth) * 0.5f;
    const DWORD color = tocolor(255, 255, 255, 255);
    draw->dxDrawText(label, x, radarY - textHeight - 4.0f * scale,
        textWidth, textHeight, 0.0f, color);
    draw->dxDrawText(label, x, radarY + radarHeight + 4.0f * scale,
        textWidth, textHeight, 0.0f, color);
}
