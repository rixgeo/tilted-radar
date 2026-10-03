/*****************************************************************************
 *
 *  PROJECT:     The-Definitive-UI
 *  FILE:        source/Radar/mapmanager/StockRadarDraw.cpp
 *  PURPOSE:     Project stock CRadar blips/gang overlay onto a custom plane
 *
 *****************************************************************************/

#include "StockRadarDraw.h"
#include "BlipManager.h"
#include "RadarOverlayCompat.h"
#include "RadarGeometry.h"
#include "MathUtils.h"
#include "Utils.h"
#include "GameState.h"
#include "Config.h"
#include "plugin.h"
#include "RenderWare.h"
#include "common.h"
#include "CRadar.h"
#include "CWorld.h"
#include "CTimer.h"
#include "CMenuManager.h"
#include "CVehicle.h"
#include "CPlayerPed.h"
#include <algorithm>
#include "CPlayerInfo.h"
#include "CTheScripts.h"
#include "CPools.h"
#include "CObject.h"
#include "CEntryExit.h"
#include "CModelInfo.h"
#include "eVehicleType.h"
#include "eModelID.h"
#include "RenderWare.h"
#include "safetyhook.hpp"
#include <d3d9.h>
#include <cstring>
#include <cmath>

namespace
{
    constexpr float kSaScreenW   = 640.0f;
    constexpr float kSaScreenH   = 448.0f;
    constexpr float kWorldBound  = 3000.0f;
    constexpr float kRadarMinRange = 180.0f;
    constexpr float kRadarMaxRange = 350.0f;
    constexpr float kRadarMinSpeed = 0.3f;
    constexpr float kRadarMaxSpeed = 0.9f;
    constexpr uintptr_t kAddrTransform   = 0x583480;
    constexpr uintptr_t kAddrLimitPoint  = 0x5832F0;
    constexpr uintptr_t kAddrLimitToMap  = 0x583350;
    constexpr uintptr_t kAddrYouAreHere  = 0x584960;
    constexpr uintptr_t kAddrDrawSprite  = 0x585FF0;
    constexpr uintptr_t kAddrAirstrip    = 0x587D20;
    constexpr uintptr_t kAddrShowTraceWithHeight = 0x584070;

    bool  s_active = false;
    bool  s_hooksInstalled = false;
    bool  s_radarStateSaved = false;
    StockRadarPlane s_plane{};

    CVector PlayerCentreForMap();

    struct RadarGlobals
    {
        CVector2D origin{};
        float     range = 0.0f;
        float     sin   = 0.0f;
        float     cos   = 0.0f;
        float     orient = 0.0f;
        bool      drawingMap = false;
    };
    RadarGlobals s_savedRadar{};

    // Manual 5-byte gateways break LimitToMap (6-byte prologue) — stock pause map
    // with MenuRender=0 calls the trampoline and jumps into garbage (AV ~0x26b8xxxx).
    safetyhook::InlineHook s_hookTransform;
    safetyhook::InlineHook s_hookLimitPoint;
    safetyhook::InlineHook s_hookLimitToMap;
    safetyhook::InlineHook s_hookYouAreHere;
    safetyhook::InlineHook s_hookDrawSprite;
    safetyhook::InlineHook s_hookAirstrip;
    safetyhook::InlineHook s_hookShowTraceWithHeight;
    bool PlaceOnOrbitFromRadarPos(const D3DXVECTOR3& radarPos, float& px, float& py)
    {
        const D3DXVECTOR3 playerPos(s_plane.playerRadarX, s_plane.playerRadarY, 0.0f);
        float angle = 0.0f;
        if (!MathUtils::DirectionToOrbitAngle(playerPos, radarPos, s_plane.yaw, angle))
            return false;
        RadarGeometry::PointOnOrbitEdge(s_plane.cx, s_plane.cy, s_plane.halfX, s_plane.halfY,
                                        cosf(angle), sinf(angle), !s_plane.shapeCircle, px, py);
        return true;
    }

    enum class HudProject
    {
        Miss,
        Inside,
        Orbit
    };

    HudProject ProjectWorldOntoHud(float worldX, float worldY, float& px, float& py, bool allowOrbit)
    {
        if (!s_plane.use3D || s_plane.rtWidth < 1.0f || s_plane.rtHeight < 1.0f)
            return HudProject::Miss;

        D3DXVECTOR3 radarPos;
        RadarGeometry::WorldToRadarPos(worldX, worldY, radarPos);

        const bool useSquare = !s_plane.shapeCircle;
        float circleX = 0.0f;
        float circleY = 0.0f;
        if (RadarGeometry::WorldToCircleScreen(
                radarPos, s_plane.cameraPos, s_plane.cameraRot,
                s_plane.fov, s_plane.nearPlane, s_plane.farPlane,
                s_plane.rtWidth, s_plane.rtHeight, s_plane.sizeX, s_plane.sizeY,
                s_plane.cx, s_plane.cy, circleX, circleY, s_plane.projectionAspect))
        {
            if (RadarGeometry::IsInsideOrbit(circleX, circleY, s_plane.cx, s_plane.cy,
                                              s_plane.halfX, s_plane.halfY, useSquare))
            {
                px = circleX;
                py = circleY;
                return HudProject::Inside;
            }
            if (!allowOrbit)
                return HudProject::Miss;
            RadarGeometry::ClampToOrbit(circleX, circleY, s_plane.cx, s_plane.cy,
                                        s_plane.halfX, s_plane.halfY, px, py, useSquare);
            return HudProject::Orbit;
        }

        if (!allowOrbit || !PlaceOnOrbitFromRadarPos(radarPos, px, py))
            return HudProject::Miss;
        return HudProject::Orbit;
    }

    bool ProjectWorldOntoHudCanvas(float worldX, float worldY, float& px, float& py)
    {
        if (!s_plane.use3D || s_plane.rtWidth < 1.0f || s_plane.rtHeight < 1.0f)
            return false;

        D3DXVECTOR3 radarPos;
        RadarGeometry::WorldToRadarPos(worldX, worldY, radarPos);

        return RadarGeometry::WorldToCircleScreen(
            radarPos, s_plane.cameraPos, s_plane.cameraRot,
            s_plane.fov, s_plane.nearPlane, s_plane.farPlane,
            s_plane.rtWidth, s_plane.rtHeight, s_plane.sizeX, s_plane.sizeY,
            s_plane.cx, s_plane.cy, px, py, s_plane.projectionAspect);
    }

    bool UsesCanvasBlipProjection()
    {
        return s_plane.use3D && GameState::ShouldDrawRadarMap();
    }

    // Donor RenderBlips2D: POIs draw only if inside the 3D disc. Orbit clamp is a
    // second pass for waypoint / legends / CHAR-CAR-checkpoint indicators.
    // Flat 2D (skynet174/2D-RADAR): clamp to rim instead of dropping icons.
    bool ProjectActivePlane3D(CVector2D const& in, float& px, float& py)
    {
        CVector2D world{};
        CRadar::TransformRadarPointToRealWorldSpace(world, in);
        if (!RadarConfig::GetRadar3D())
            return ProjectWorldOntoHud(world.x, world.y, px, py, true) != HudProject::Miss;
        if (RadarOverlayCompat::IsInvokingHudBlips())
        {
            const CVector playerCentre = PlayerCentreForMap();
            const float dx = world.x - playerCentre.x;
            const float dy = world.y - playerCentre.y;
            const float range = StockRadarDraw::GetHudRadarRange();
            if (dx * dx + dy * dy > range * range)
                return false;

            return ProjectWorldOntoHud(world.x, world.y, px, py, true) != HudProject::Miss;
        }
        if (UsesCanvasBlipProjection())
            return ProjectWorldOntoHudCanvas(world.x, world.y, px, py);
        return ProjectWorldOntoHud(world.x, world.y, px, py, false) == HudProject::Inside;
    }

    CVector PlayerCentreForMap();

    void ProjectFlatOntoHudPlane(CVector2D const& in, float& px, float& py)
    {
        // Legacy overlays expect a flat radar rect mapping,
        // not the tilted 3D disc projection used for our own blips.
        const float halfX = (s_plane.sizeX > 0.0f) ? (s_plane.sizeX * 0.5f) : s_plane.half;
        const float halfY = (s_plane.sizeY > 0.0f) ? (s_plane.sizeY * 0.5f) : s_plane.half;
        px = s_plane.cx + halfX * in.x;
        py = s_plane.cy - halfY * in.y;
    }

    void ProjectActivePlanePoint(CVector2D const& in, float& px, float& py)
    {
        if (s_plane.use3D)
        {
            if (!ProjectActivePlane3D(in, px, py))
            {
                px = -10000.0f;
                py = -10000.0f;
            }
            return;
        }

        ProjectFlatOntoHudPlane(in, px, py);
    }

    bool UsesBase640Coords()
    {
        return FrontEndMenuManager.m_bDrawRadarOrMap;
    }

    void ToOutputCoordSpace(float px, float py, CVector2D& out)
    {
        if (UsesBase640Coords())
        {
            out.x = px * kSaScreenW / SCREEN_WIDTH;
            out.y = py * kSaScreenH / SCREEN_HEIGHT;
        }
        else
        {
            out.x = px;
            out.y = py;
        }
    }

    void __cdecl TransformRadarPointToScreenSpace_Hook(CVector2D& out, CVector2D const& in)
    {
        __asm { push edx }

        if (s_active)
        {
            float px = 0.0f;
            float py = 0.0f;

            ProjectActivePlanePoint(in, px, py);
            ToOutputCoordSpace(px, py, out);
        }
        else if (s_hookTransform)
        {
            // Explicit ref types — bare ccall(out,in) deduces by-value and breaks the trampoline.
            s_hookTransform.ccall<void, CVector2D&, CVector2D const&>(out, in);
        }

        __asm { pop edx }
    }

    void __cdecl LimitToMap_Hook(float* pX, float* pY)
    {
        if (!pX || !pY)
            return;

        if (s_active)
        {
            // Canvas blips: keep drawing off-disc points; edge fade handles visibility.
            if (UsesCanvasBlipProjection()
                && !RadarOverlayCompat::IsInvokingPauseMapOverlay())
            {
                return;
            }

            // Pause-map overlays expect stock LimitToMap clamping.
            if (RadarOverlayCompat::IsInvokingPauseMapOverlay())
            {
                if (*pX < s_plane.clipL) *pX = s_plane.clipL;
                if (*pX > s_plane.clipR) *pX = s_plane.clipR;
                if (*pY < s_plane.clipT) *pY = s_plane.clipT;
                if (*pY > s_plane.clipB) *pY = s_plane.clipB;
                return;
            }

            constexpr float pad = 28.0f;
            if (*pX < s_plane.clipL - pad || *pX > s_plane.clipR + pad
                || *pY < s_plane.clipT - pad || *pY > s_plane.clipB + pad)
            {
                *pX = -10000.0f;
                *pY = -10000.0f;
            }
            return;
        }

        if (s_hookLimitToMap)
            s_hookLimitToMap.ccall<void, float*, float*>(pX, pY);
    }

    float __cdecl LimitRadarPoint_Hook(CVector2D& point)
    {
        const float mag = sqrtf(point.x * point.x + point.y * point.y);

        // Map coordinates and our 3D HUD projection use the original point.
        if (FrontEndMenuManager.m_bDrawRadarOrMap)
            return mag;
        if (s_active && s_plane.use3D)
            return mag;

        if (mag > 1.0f)
        {
            point.x /= mag;
            point.y /= mag;
        }
        return mag;
    }

    CVector PlayerCentreForMap()
    {
        return GameState::SafePlayerCentreForMap();
    }

    void __cdecl DrawYouAreHereSprite_Hook(float x, float y)
    {
        if (!s_active)
        {
            if (s_hookYouAreHere)
                s_hookYouAreHere.ccall<void, float, float>(x, y);
            return;
        }

        CPed* player = FindPlayerPed();
        bool playerInAircraft = false;
        if (player && player->bInVehicle && player->m_pVehicle)
        {
            const int modelIndex = player->m_pVehicle->m_nModelIndex;
            playerInAircraft = CModelInfo::IsPlaneModel(modelIndex) || CModelInfo::IsHeliModel(modelIndex);
        }

        static unsigned int& mapYouAreHereTimer = *reinterpret_cast<unsigned int*>(0xBAA358);
        static bool& mapYouAreHereDisplay = *reinterpret_cast<bool*>(0x8D0930);

        if (CTimer::m_snTimeInMillisecondsPauseMode - mapYouAreHereTimer > 700u)
        {
            mapYouAreHereTimer = CTimer::m_snTimeInMillisecondsPauseMode;
            mapYouAreHereDisplay = !mapYouAreHereDisplay;
        }

        CVector2D radarPos{};
        const CVector centre = PlayerCentreForMap();
        CRadar::TransformRealWorldPointToRadarSpace(radarPos, CVector2D(centre.x, centre.y));
        CRadar::LimitRadarPoint(radarPos);

        float px = 0.0f;
        float py = 0.0f;
        ProjectActivePlanePoint(radarPos, px, py);

        float drawX = px;
        float drawY = py;
        if (UsesBase640Coords())
        {
            drawX = px * kSaScreenW / SCREEN_WIDTH;
            drawY = py * kSaScreenH / SCREEN_HEIGHT;
        }

        constexpr float pad = 28.0f;
        const bool onMap = px >= s_plane.clipL - pad && px <= s_plane.clipR + pad
            && py >= s_plane.clipT - pad && py <= s_plane.clipB + pad;

        if (onMap && mapYouAreHereDisplay && !playerInAircraft && CRadar::RadarBlipSprites)
        {
            constexpr float kPi = 3.14159265f;
            float angle = kPi;
            CPed* ped = player;
            if (ped && !GameState::IsPedInVehicleTransition(ped))
            {
                __try
                {
                    angle = FindPlayerHeading(0) + kPi;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    angle = ped->GetHeading() + kPi;
                }
            }
            else if (ped)
                angle = ped->GetHeading() + kPi;

            const unsigned half = static_cast<unsigned int>(
                static_cast<float>(RsGlobal.maximumHeight) / kSaScreenH * 8.0f);
            CRadar::DrawRotatingRadarSprite(
                &CRadar::RadarBlipSprites[RADAR_SPRITE_CENTRE],
                drawX,
                drawY,
                angle,
                half,
                half,
                CRGBA(255, 255, 255, 255));
        }

        CRadar::AddBlipToLegendList(0, RADAR_SPRITE_MAP_HERE);
    }

    void __cdecl DrawRadarSprite_Hook(unsigned short spriteId, float x, float y, unsigned char alpha)
    {
        if (s_active && !UsesBase640Coords()
            && (spriteId == RADAR_SPRITE_NORTH
                || spriteId == RADAR_SPRITE_LIGHT
                || spriteId == RADAR_SPRITE_RUNWAY))
            return;

        float drawX = x;
        float drawY = y;
        unsigned char drawAlpha = alpha;

        if (s_active && !RadarOverlayCompat::IsInvokingHudBlips()
            && UsesCanvasBlipProjection() && RadarConfig::GetBlipEdgeFade())
        {
            const float fade = RadarGeometry::ComputeOrbitEdgeFade(
                x, y, s_plane.cx, s_plane.cy, s_plane.halfX, s_plane.halfY,
                !s_plane.shapeCircle,
                MathUtils::ScaleRadarLength(18.0f));
            if (fade <= 0.0f)
                return;
            drawAlpha = static_cast<unsigned char>((float)drawAlpha * fade);
        }

        if (s_hookDrawSprite)
            s_hookDrawSprite.ccall<void, unsigned short, float, float, unsigned char>(
                spriteId, drawX, drawY, drawAlpha);
    }

    void __cdecl ShowRadarTraceWithHeight_Hook(float x, float y, unsigned int size,
                                                unsigned char red, unsigned char green,
                                                unsigned char blue, unsigned char alpha,
                                                unsigned char type)
    {
        if (s_hookShowTraceWithHeight)
        {
            s_hookShowTraceWithHeight.ccall<void, float, float, unsigned int,
                unsigned char, unsigned char, unsigned char, unsigned char,
                unsigned char>(x, y, size, red, green, blue, alpha, type);
        }
    }

    void SaveRadarGlobals()
    {
        s_savedRadar.origin     = CRadar::vec2DRadarOrigin;
        s_savedRadar.range      = CRadar::m_radarRange;
        s_savedRadar.sin        = CRadar::cachedSin;
        s_savedRadar.cos        = CRadar::cachedCos;
        s_savedRadar.orient     = CRadar::m_fRadarOrientation;
        s_savedRadar.drawingMap = FrontEndMenuManager.m_bDrawRadarOrMap;
        s_radarStateSaved       = true;
    }

    void RestoreRadarGlobals()
    {
        if (!s_radarStateSaved)
            return;
        CRadar::vec2DRadarOrigin                 = s_savedRadar.origin;
        CRadar::m_radarRange                     = s_savedRadar.range;
        CRadar::cachedSin                        = s_savedRadar.sin;
        CRadar::cachedCos                        = s_savedRadar.cos;
        CRadar::m_fRadarOrientation              = s_savedRadar.orient;
        FrontEndMenuManager.m_bDrawRadarOrMap    = s_savedRadar.drawingMap;
        s_radarStateSaved = false;
    }

    void __cdecl SetupAirstripBlips_Hook()
    {
        if (CRadar::airstrip_blip)
        {
            CRadar::ClearBlip(CRadar::airstrip_blip);
            CRadar::airstrip_blip = 0;
        }
    }

    void ApplyMapProjection()
    {
        CRadar::vec2DRadarOrigin    = CVector2D(0.0f, 0.0f);
        CRadar::m_radarRange        = kWorldBound - 10.0f;
        CRadar::cachedSin           = 0.0f;
        CRadar::cachedCos           = 1.0f;
        CRadar::m_fRadarOrientation = 0.0f;
    }

    float CalculateHudRadarRange()
    {
        CPlayerPed* player = FindPlayerPed();
        if (!player)
            return kRadarMinRange;

        CVehicle* vehicle = FindPlayerVehicle();
        CPlayerInfo* info = player->GetPlayerInfoForThisPlayerPed();
        const bool remote = info && (info->m_pRemoteVehicle || info->m_bAfterRemoteVehicleExplosion);

        if (!vehicle || remote)
            return kRadarMinRange;

        if (vehicle->m_nVehicleSubClass == VEHICLE_PLANE && vehicle->m_nModelIndex != MODEL_VORTEX)
        {
            const float speedZ = vehicle->GetPosition().z / 200.0f;
            if (speedZ < kRadarMinSpeed)
                return kRadarMaxRange - 10.0f;
            if (speedZ < kRadarMaxSpeed)
                return (speedZ - kRadarMinSpeed) * (1.0f / 60.0f) + (kRadarMaxRange - 10.0f);
            return kRadarMaxRange;
        }

        const float speed = FindPlayerSpeed().Magnitude();
        if (speed < kRadarMinSpeed)
            return kRadarMinRange;
        if (speed >= kRadarMaxSpeed)
            return kRadarMaxRange;
        return (speed - kRadarMinSpeed) * (850.0f / (kRadarMinSpeed * 10.0f)) + kRadarMinRange;
    }

    // Stock CRadar::DrawMap can leave m_radarRange near the full map width.
    void UpdateHudRadarRange()
    {
        CRadar::m_radarRange = CalculateHudRadarRange();
    }
}

void StockRadarDraw::EnsureHooksInstalled()
{
    if (s_hooksInstalled)
        return;

    s_hookTransform = safetyhook::create_inline(
        reinterpret_cast<void*>(kAddrTransform), TransformRadarPointToScreenSpace_Hook);
    s_hookLimitPoint = safetyhook::create_inline(
        reinterpret_cast<void*>(kAddrLimitPoint), LimitRadarPoint_Hook);
    s_hookLimitToMap = safetyhook::create_inline(
        reinterpret_cast<void*>(kAddrLimitToMap), LimitToMap_Hook);
    s_hookYouAreHere = safetyhook::create_inline(
        reinterpret_cast<void*>(kAddrYouAreHere), DrawYouAreHereSprite_Hook);
    s_hookDrawSprite = safetyhook::create_inline(
        reinterpret_cast<void*>(kAddrDrawSprite), DrawRadarSprite_Hook);
    s_hookAirstrip = safetyhook::create_inline(
        reinterpret_cast<void*>(kAddrAirstrip), SetupAirstripBlips_Hook);
    s_hookShowTraceWithHeight = safetyhook::create_inline(
        reinterpret_cast<void*>(kAddrShowTraceWithHeight), ShowRadarTraceWithHeight_Hook);

    if (!s_hookTransform || !s_hookLimitPoint || !s_hookLimitToMap
        || !s_hookYouAreHere || !s_hookDrawSprite || !s_hookAirstrip
        || !s_hookShowTraceWithHeight)
    {
        s_hookTransform.reset();
        s_hookLimitPoint.reset();
        s_hookLimitToMap.reset();
        s_hookYouAreHere.reset();
        s_hookDrawSprite.reset();
        s_hookAirstrip.reset();
        s_hookShowTraceWithHeight.reset();
        return;
    }

    s_hooksInstalled = true;
}

void StockRadarDraw::SetPlane(const StockRadarPlane& plane)
{
    s_plane = plane;
}

float StockRadarDraw::GetHudRadarRange()
{
    return CalculateHudRadarRange();
}

void StockRadarDraw::DrawHudBlipsCallbacks(const StockRadarPlane& plane)
{
    if (!s_hooksInstalled || !plane.use3D || s_active || FrontEndMenuManager.m_bDrawRadarOrMap)
        return;

    const StockRadarPlane previousPlane = s_plane;
    const bool previousMapMode = FrontEndMenuManager.m_bDrawRadarOrMap;
    SetPlane(plane);
    Begin();
    FrontEndMenuManager.m_bDrawRadarOrMap = false;

    CRadar::CalculateCachedSinCos();
    const CVector centre = PlayerCentreForMap();
    CRadar::vec2DRadarOrigin = CVector2D(centre.x, centre.y);
    UpdateHudRadarRange();
    RadarOverlayCompat::InvokeHudBlips();

    End();
    FrontEndMenuManager.m_bDrawRadarOrMap = previousMapMode;
    SetPlane(previousPlane);
}

void StockRadarDraw::SyncRwSpritePipeline()
{
    _rwD3D9SetPixelShader(nullptr);
    _rwD3D9SetVertexShader(nullptr);
    RwD3D9SetTexture(nullptr, 0);
    RwD3D9SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    RwD3D9SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    RwD3D9SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    RwD3D9SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    RwD3D9SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    RwD3D9SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    RwD3D9SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

void StockRadarDraw::SanitizeDrawState()
{
    // Third-party callbacks may leave D3DRS_SCISSORTESTENABLE on the radar RT rect; if that
    // leaks past EndRadarZone the whole HUD draws into a tiny top-left clip.
    if (auto* device = static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice()))
    {
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_CLIPPING, TRUE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,
            D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
            D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);
        device->SetVertexDeclaration(nullptr);
        device->SetTexture(0, nullptr);
        device->SetTexture(1, nullptr);
    }

    SyncRwSpritePipeline();
    InvalidateRwShaderCache();
}

void StockRadarDraw::InvalidateRwShaderCache()
{
    Ui::PoisonRwShaderCache();
}

void StockRadarDraw::RestoreHudPipeline()
{
    InvalidateRwShaderCache();
}

void StockRadarDraw::Begin()
{
    SaveRadarGlobals();
    SyncRwSpritePipeline();
    s_active = true;
}

void StockRadarDraw::End()
{
    s_active = false;
    s_plane.use3D = false;
    RestoreRadarGlobals();
}

bool StockRadarDraw::IsActive()
{
    return s_active;
}

void StockRadarDraw::Draw(bool gangOverlay, bool gangInMenu)
{
    if (s_plane.use3D)
        FrontEndMenuManager.m_bDrawRadarOrMap = false;

    if (FrontEndMenuManager.m_bDrawRadarOrMap)
    {
        ApplyMapProjection();
    }
    else
    {
        CRadar::CalculateCachedSinCos();
        const CVector centre = PlayerCentreForMap();
        CRadar::vec2DRadarOrigin = CVector2D(centre.x, centre.y);
        UpdateHudRadarRange();
    }

    if (s_plane.use3D)
    {
        // HUD callbacks are dispatched separately while the blip render target is active.
    }
    else if (FrontEndMenuManager.m_bDrawRadarOrMap)
    {
        // Replace the direct pause-map gang call with its live plugin-sdk chain,
        // preserving the original once when the setting allows it.
        RadarOverlayCompat::InvokePauseMapOverlay(gangOverlay);
    }
    else
    {
        // Rare 2D HUD fallback (no RT): retain the stock gang overlay.
        if (gangOverlay)
            CRadar::DrawRadarGangOverlay(gangInMenu);
    }

}
