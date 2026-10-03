/*****************************************************************************
 *
 *  PROJECT:     The-Definitive-UI
 *  FILE:        source/Radar/mapmanager/BlipManager.cpp
 *  PURPOSE:     Radar blips - stock sprite textures and overlay drawing
 *
 *****************************************************************************/

#include "BlipManager.h"
#include "MapChunkManager.h"
#include "TxdManager.h"
#include "Radar.h"
#include "Config.h"
#include "CRadar.h"
#include "CSprite2d.h"
#include "RenderWare.h"
#include "RadarGeometry.h"
#include "MathUtils.h"
#include "Config.h"
#include "GameState.h"
#include "draw/DxDrawPrimitives.h"
#include "CPlayerPed.h"
#include "CTheScripts.h"
#include "CPools.h"
#include "CObject.h"
#include "CEntryExit.h"
#include "CVehicle.h"
#include "common.h"
#include "CMenuManager.h"
#include <d3dx9.h>
#include <cstdio>
#include <cmath>

namespace
{
    float s_missionMarkerSize = 4.4f;
    bool s_missionMarkerOutline = true;

    LPDIRECT3DTEXTURE9 RwTextureToD3D9(LPDIRECT3DDEVICE9 pDevice, RwTexture* rwTex)
    {
        if (!pDevice || !rwTex)
            return nullptr;

        RwRaster* raster = RwTextureGetRaster(rwTex);
        if (!raster)
            return nullptr;

        const int w = RwRasterGetWidth(raster);
        const int h = RwRasterGetHeight(raster);
        if (w <= 0 || h <= 0)
            return nullptr;

        RwImage* img = RwImageCreate(w, h, 32);
        if (!img)
            return nullptr;
        if (!RwImageAllocatePixels(img))
        {
            RwImageDestroy(img);
            return nullptr;
        }
        if (!RwImageSetFromRaster(img, raster))
        {
            RwImageFreePixels(img);
            RwImageDestroy(img);
            return nullptr;
        }

        LPDIRECT3DTEXTURE9 d3dTex = nullptr;
        if (FAILED(pDevice->CreateTexture((UINT)w, (UINT)h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &d3dTex, nullptr))
            || !d3dTex)
        {
            RwImageFreePixels(img);
            RwImageDestroy(img);
            return nullptr;
        }

        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(d3dTex->LockRect(0, &locked, nullptr, 0)))
        {
            RwUInt8* src = RwImageGetPixels(img);
            const int srcStride = RwImageGetStride(img);
            const int dstStride = locked.Pitch;
            for (int y = 0; y < h; ++y)
            {
                RwUInt8* rowSrc = src + y * srcStride;
                RwUInt8* rowDst = static_cast<RwUInt8*>(locked.pBits) + y * dstStride;
                for (int x = 0; x < w; ++x)
                {
                    const int off = x * 4;
                    rowDst[off + 0] = rowSrc[off + 2];
                    rowDst[off + 1] = rowSrc[off + 1];
                    rowDst[off + 2] = rowSrc[off + 0];
                    rowDst[off + 3] = rowSrc[off + 3];
                }
            }
            d3dTex->UnlockRect(0);
        }

        RwImageFreePixels(img);
        RwImageDestroy(img);
        return d3dTex;
    }

}

BlipManager::BlipManager(LPDIRECT3DDEVICE9 pDevice)
    : m_pDevice(pDevice)
    , m_bInitialized(false)
{
    ZeroMemory(m_stockTextures, sizeof(m_stockTextures));
    ZeroMemory(m_stockTextureSources, sizeof(m_stockTextureSources));
    ZeroMemory(m_stockTextureSourceRasters, sizeof(m_stockTextureSourceRasters));
}

BlipManager::~BlipManager()
{
    Shutdown();
}

bool BlipManager::Initialize()
{
    if (m_bInitialized)
        return true;

    StockRadarDraw::EnsureHooksInstalled();

    m_bInitialized = true;
    return true;
}

void BlipManager::Shutdown()
{
    for (int i = 0; i <= MAX_BLIP_ID; ++i)
    {
        if (m_stockTextures[i])
        {
            m_stockTextures[i]->Release();
            m_stockTextures[i] = nullptr;
        }
        m_stockTextureSources[i] = nullptr;
        m_stockTextureSourceRasters[i] = nullptr;
    }

    m_bInitialized = false;
}

LPDIRECT3DTEXTURE9 BlipManager::ConvertRwTexture(RwTexture* rwTex)
{
    return RwTextureToD3D9(m_pDevice, rwTex);
}

LPDIRECT3DTEXTURE9 BlipManager::GetStockSpriteTexture(int spriteId)
{
    (void)spriteId;
    return nullptr;
}

LPDIRECT3DTEXTURE9 BlipManager::GetOwnedSpriteTexture(int spriteId)
{
    if (spriteId == RADAR_SPRITE_LIGHT || spriteId == RADAR_SPRITE_RUNWAY)
        return nullptr;
    if (spriteId < 0 || spriteId > MAX_BLIP_ID || !m_pDevice)
        return nullptr;

    RwTexture* rwTex = nullptr;
    if (CRadar::RadarBlipSprites)
        rwTex = CRadar::RadarBlipSprites[spriteId].m_pTexture;
    RwRaster* raster = rwTex ? RwTextureGetRaster(rwTex) : nullptr;

    if (m_stockTextureSources[spriteId] != rwTex
        || m_stockTextureSourceRasters[spriteId] != raster)
    {
        if (m_stockTextures[spriteId])
        {
            m_stockTextures[spriteId]->Release();
            m_stockTextures[spriteId] = nullptr;
        }
        m_stockTextureSources[spriteId] = rwTex;
        m_stockTextureSourceRasters[spriteId] = raster;
    }

    if (!rwTex)
        return nullptr;
    if (m_stockTextures[spriteId])
        return m_stockTextures[spriteId];

    // Copy the current sprite texture because mods and menu transitions can replace it.
    m_stockTextures[spriteId] = TxdManager::CopyRwTextureFromD3D(m_pDevice, rwTex);
    if (!m_stockTextures[spriteId])
        m_stockTextures[spriteId] = ConvertRwTexture(rwTex);
    return m_stockTextures[spriteId];
}

void BlipManager::DrawStockOverlay(const StockRadarPlane& plane, bool gangZones)
{
    StockRadarDraw::SetPlane(plane);
    StockRadarDraw::Begin();
    StockRadarDraw::Draw(gangZones, false);
    StockRadarDraw::End();
}

namespace
{
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
        if (trace.m_nBlipType == BLIP_CAR && trace.m_nEntityHandle)
        {
            if (CVehicle* veh = CPools::GetVehicle(static_cast<int>(trace.m_nEntityHandle)))
            {
                out = veh->GetPosition();
                return true;
            }
        }
        if (trace.m_nBlipType == BLIP_OBJECT && trace.m_nEntityHandle)
        {
            if (CObject* obj = CPools::GetObject(static_cast<int>(trace.m_nEntityHandle)))
            {
                out = obj->GetPosition();
                return true;
            }
        }

        out = trace.m_vecPos;
        if (trace.m_pEntryExit)
            trace.m_pEntryExit->GetPositionRelativeToOutsideWorld(out);
        return true;
    }

    bool TraceShowsOnRadar(const tRadarTrace& trace)
    {
        if (!trace.m_bInUse)
            return false;
        if (trace.m_nBlipDisplay != BLIP_DISPLAY_BOTH
            && trace.m_nBlipDisplay != BLIP_DISPLAY_BLIP_ONLY)
            return false;
        if (trace.m_nBlipType == BLIP_CONTACTPOINT && CTheScripts::IsPlayerOnAMission())
            return false;
        return true;
    }

    enum class RtProject
    {
        Miss,
        Inside,
        Orbit
    };

    // Keep projected icons inside the usable radar orbit, including icons behind the camera.
    RtProject ProjectWorldToBlipRt(float worldX, float worldY, const StockRadarPlane& plane,
                                   float& outX, float& outY, bool allowOrbitClamp,
                                   float markerRadius)
    {
        if (!plane.use3D || plane.rtWidth < 1.0f || plane.rtHeight < 1.0f)
            return RtProject::Miss;

        const float cx = plane.rtWidth * 0.5f;
        const float cy = plane.rtHeight * 0.5f;
        const bool useSquare = !plane.shapeCircle;
        const float halfX = (plane.halfX > 1.0f) ? plane.halfX : cx;
        const float halfY = (plane.halfY > 1.0f) ? plane.halfY : cy;

        D3DXVECTOR3 radarPos;
        RadarGeometry::WorldToRadarPos(worldX, worldY, radarPos);

        float px = 0.0f;
        float py = 0.0f;
        if (RadarGeometry::WorldToCircleScreen(
                radarPos, plane.cameraPos, plane.cameraRot,
                plane.fov, plane.nearPlane, plane.farPlane,
                plane.rtWidth, plane.rtHeight, plane.rtWidth, plane.rtHeight,
                cx, cy, px, py, plane.projectionAspect))
        {
            if (!allowOrbitClamp)
            {
                if (!RadarGeometry::IsInsideOrbit(px, py, cx, cy, halfX, halfY, useSquare))
                    return RtProject::Miss;
                outX = px;
                outY = py;
                return RtProject::Inside;
            }

            const float safeHalfX = (std::max)(1.0f, halfX - markerRadius - 2.0f);
            const float safeHalfY = (std::max)(1.0f, halfY - markerRadius - 2.0f);
            RadarGeometry::ClampToOrbit(
                px, py, cx, cy, safeHalfX, safeHalfY, outX, outY, useSquare);
            return RadarGeometry::IsInsideOrbit(px, py, cx, cy, halfX, halfY, useSquare)
                ? RtProject::Inside : RtProject::Orbit;
        }

        const float safeHalfX = (std::max)(1.0f, halfX - markerRadius - 2.0f);
        const float safeHalfY = (std::max)(1.0f, halfY - markerRadius - 2.0f);

        D3DXMATRIX view{}, projection{};
        MathUtils::BuildRadarViewProj(
            plane.cameraPos, plane.cameraRot, plane.fov,
            plane.nearPlane, plane.farPlane, plane.projectionAspect,
            !RadarConfig::GetRadar3D(), view, projection);
        (void)projection;
        D3DXVECTOR3 cameraSpacePos{};
        D3DXVec3TransformCoord(&cameraSpacePos, &radarPos, &view);
        const float tanHalfFov = tanf(plane.fov * 0.5f);
        const float projectionAspect = (plane.projectionAspect > 0.0f)
            ? plane.projectionAspect : 1.0f;
        float dirX = cameraSpacePos.x / tanHalfFov;
        float dirY = -cameraSpacePos.y / (tanHalfFov / projectionAspect);

        if (fabsf(dirX) < 0.001f && fabsf(dirY) < 0.001f)
        {
            const D3DXVECTOR3 playerPos(plane.playerRadarX, plane.playerRadarY, 0.0f);
            float angle = 0.0f;
            if (!MathUtils::DirectionToOrbitAngle(playerPos, radarPos, plane.yaw, angle))
                return RtProject::Miss;
            dirX = cosf(angle);
            dirY = sinf(angle);
        }

        RadarGeometry::PointOnOrbitEdge(
            cx, cy, safeHalfX, safeHalfY, dirX, dirY, useSquare, outX, outY);
        return RtProject::Orbit;
    }

    float BlipRtEdgeFade(float x, float y, const StockRadarPlane& plane)
    {
        if (!RadarConfig::GetBlipEdgeFade())
            return 1.0f;
        const float fadeW = MathUtils::ScaleRadarLength(18.0f)
            * (plane.rtWidth / (plane.sizeX > 1.0f ? plane.sizeX : plane.rtWidth));
        return RadarGeometry::ComputeOrbitEdgeFade(
            x, y, plane.rtWidth * 0.5f, plane.rtHeight * 0.5f,
            plane.halfX, plane.halfY, !plane.shapeCircle, fadeW);
    }
}

float BlipManager::GetIconCanvasScale()
{
    constexpr float kBase = 1.0f / 3.5f;
    return kBase * (static_cast<float>(RadarConfig::GetBlipIconScalePercent()) / 100.0f);
}

float BlipManager::GetStockSpriteDiameterPx(float rtScale)
{
    const float h = static_cast<float>(RsGlobal.maximumHeight);
    const float half = std::floor(h / 360.0f * 8.0f);
    return (std::max)(12.0f, half * 2.0f) * GetIconCanvasScale() * rtScale;
}

float BlipManager::GetTraceMarkerDiameterPx(unsigned char blipSize, float rtScale)
{
    const float h = static_cast<float>(RsGlobal.maximumHeight);
    const float stretch = h / 360.0f;
    return (std::max)(3.0f, static_cast<float>(blipSize) * 2.0f * stretch * rtScale * GetIconCanvasScale());
}

void BlipManager::DrawBlipsToRenderTarget(const StockRadarPlane& plane, DxDrawPrimitives* draw,
                                          float hudRadarSizeX, float /*defaultSpriteSize*/,
                                          bool previewPedOnly, bool useAircraftIcon)
{
    if (!draw || !plane.use3D || plane.rtWidth < 1.0f || plane.rtHeight < 1.0f)
        return;

    const float rtW = plane.rtWidth;
    const float rtH = plane.rtHeight;
    const float rtScale = rtW / (hudRadarSizeX > 1.0f ? hudRadarSizeX : rtW);
    const float spriteSize = BlipManager::GetStockSpriteDiameterPx(rtScale);
    const bool isPauseMap = FrontEndMenuManager.m_bDrawRadarOrMap;
    const float hudRadarRange = (!previewPedOnly && !isPauseMap)
        ? StockRadarDraw::GetHudRadarRange() : 0.0f;
    const CVector playerWorldPosition = GameState::SafePlayerCentreForMap();

    auto drawSprite = [&](int spriteId, float px, float py, float size, float angle, DWORD color) {
        if ((color >> 24) == 0)
            return;
        LPDIRECT3DTEXTURE9 tex = GetOwnedSpriteTexture(spriteId);
        const float half = size * 0.5f;
        if (!tex)
        {
            if (!CRadar::RadarBlipSprites || spriteId < 0 || spriteId > MAX_BLIP_ID)
                return;
            const BYTE alpha = static_cast<BYTE>((color >> 24) & 0xFF);
            if (fabsf(angle) > 0.001f)
            {
                CRadar::DrawRotatingRadarSprite(
                    &CRadar::RadarBlipSprites[spriteId], px, py, angle,
                    half, half, CRGBA(255, 255, 255, alpha));
            }
            else
            {
                CRadar::RadarBlipSprites[spriteId].Draw(
                    CRect(px - half, py - half, px + half, py + half),
                    CRGBA(255, 255, 255, alpha));
            }
            return;
        }
        draw->dxDrawImage2DRotatedSurface(px - half, py - half, size, size, tex, angle, color, rtW, rtH);
    };

    auto playerHeadingRad = [](CPed* ped) -> float {
        if (!ped)
            return 0.0f;
        if (!GameState::IsPedInVehicleTransition(ped))
        {
            __try { return FindPlayerHeading(0); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return ped->GetHeading(); }
        }
        return ped->GetHeading();
    };

    // Negated heading + yaw give correct L/R; drop ±π (that was a 180° sprite flip).
    auto centreSpriteAngle = [&](CPed* ped, bool preview) -> float {
        if (preview)
            return -plane.yaw;
        return (-playerHeadingRad(ped)) - plane.yaw;
    };

    auto drawAtWorld = [&](float worldX, float worldY, bool allowOrbitClamp,
                           float markerRadius, auto&& fn) {
        float px = 0.0f;
        float py = 0.0f;
        const RtProject projected = ProjectWorldToBlipRt(
            worldX, worldY, plane, px, py, allowOrbitClamp, markerRadius);
        if (projected == RtProject::Miss)
            return;
        const float fade = allowOrbitClamp ? 1.0f : BlipRtEdgeFade(px, py, plane);
        if (fade <= 0.0f)
            return;
        fn(px, py, fade);
    };

    if (previewPedOnly)
    {
        // Settings preview focuses camera on MAP_CENTER — same world as map tiles.
        const float previewWorldX = MapChunkManager::MAP_CENTER_X - RadarGeometry::RADAR_OFFSET_X;
        const float previewWorldY = MapChunkManager::MAP_CENTER_Y - RadarGeometry::RADAR_OFFSET_Y;
        drawAtWorld(previewWorldX, previewWorldY, false, spriteSize * 0.5f,
                    [&](float px, float py, float fade) {
            const DWORD color = D3DCOLOR_ARGB((BYTE)(255.0f * fade), 255, 255, 255);
            drawSprite(RADAR_SPRITE_CENTRE, px, py, spriteSize, centreSpriteAngle(nullptr, true), color);
        });
        return;
    }

    CPed* player = FindPlayerPed();
    const float playerZ = player ? player->GetPosition().z : 0.0f;
    if (player && !useAircraftIcon)
    {
        const float angle = centreSpriteAngle(player, false);
        drawAtWorld(plane.playerRadarX - RadarGeometry::RADAR_OFFSET_X,
                    plane.playerRadarY - RadarGeometry::RADAR_OFFSET_Y, false, spriteSize * 0.5f,
                    [&](float px, float py, float fade) {
            const DWORD color = D3DCOLOR_ARGB((BYTE)(255.0f * fade), 255, 255, 255);
            drawSprite(RADAR_SPRITE_CENTRE, px, py, spriteSize, angle, color);
        });
    }

    if (CRadar::ms_RadarTrace)
    {
        const int waypointIdx = FrontEndMenuManager.m_nTargetBlipIndex
            ? CRadar::GetActualBlipArrayIndex(FrontEndMenuManager.m_nTargetBlipIndex)
            : -1;

        for (unsigned int i = 0; i < MAX_RADAR_TRACES; ++i)
        {
            const tRadarTrace& trace = CRadar::ms_RadarTrace[i];
            if (!TraceShowsOnRadar(trace))
                continue;

            const unsigned char spriteId = trace.m_nRadarSprite;
            if (spriteId == RADAR_SPRITE_NORTH || spriteId == RADAR_SPRITE_CENTRE
                || spriteId == RADAR_SPRITE_LIGHT || spriteId == RADAR_SPRITE_RUNWAY)
                continue;

            CVector world{};
            if (!GetTraceWorldPos(trace, world))
                continue;
            const bool missionCp = (trace.m_nBlipType == BLIP_COORD || trace.m_nBlipType == BLIP_CONTACTPOINT)
                && IsMissionCheckpointSprite(spriteId);
            if (missionCp || spriteId == RADAR_SPRITE_NONE)
                continue;
            const bool indicator = trace.m_nBlipType == BLIP_CHAR
                || trace.m_nBlipType == BLIP_CAR
                || trace.m_nBlipType == BLIP_SPOTLIGHT
                || missionCp;
            const bool waypoint = (static_cast<int>(i) == waypointIdx) || (spriteId == RADAR_SPRITE_WAYPOINT);
            if (waypoint)
                continue;

            if (!isPauseMap
                && !indicator && spriteId != RADAR_SPRITE_NONE)
            {
                const float dx = world.x - playerWorldPosition.x;
                const float dy = world.y - playerWorldPosition.y;
                if (dx * dx + dy * dy > hudRadarRange * hudRadarRange)
                    continue;
            }

            const bool clampToOrbit = indicator;
            drawAtWorld(world.x, world.y, clampToOrbit, spriteSize * 0.5f,
                        [&](float px, float py, float fade) {
                if (spriteId != RADAR_SPRITE_NONE)
                {
                    const BYTE alpha = (BYTE)((255.0f * fade) + 0.5f);
                    const DWORD color = D3DCOLOR_ARGB(alpha, 255, 255, 255);
                    drawSprite(spriteId, px, py, spriteSize, 0.0f, color);
                    return;
                }

                const DWORD baseColor = TraceColorToD3D(trace.m_nColour, trace.m_bBright != 0, trace.m_bFriendly != 0);
                const BYTE alpha = (BYTE)((float)((baseColor >> 24) & 0xFF) * fade);
                const DWORD color = (baseColor & 0x00FFFFFF) | (alpha << 24);

                const float traceSize = BlipManager::GetTraceMarkerDiameterPx(trace.m_nBlipSize, rtScale);

                const eHeightIndicatorType heightType = GetHeightIndicatorType(world.z, playerZ);
                draw->dxDrawGTAIndicatorBlipSurface(px, py, traceSize, color, heightType, rtW, rtH);
            });
        }
    }

}

void BlipManager::DrawMissionMarkersOnHud(const StockRadarPlane& plane, DxDrawPrimitives* draw,
                                           float screenWidth, float screenHeight)
{
    if (!draw || !plane.use3D || !CRadar::ms_RadarTrace
        || screenWidth < 1.0f || screenHeight < 1.0f)
        return;

    CPed* player = FindPlayerPed();
    const float playerZ = player ? player->GetPosition().z : 0.0f;
    const int waypointIdx = FrontEndMenuManager.m_nTargetBlipIndex
        ? CRadar::GetActualBlipArrayIndex(FrontEndMenuManager.m_nTargetBlipIndex)
        : -1;

    float visibleHalfX = plane.halfX;
    float visibleHalfY = plane.halfY;
    if (s_missionMarkerOutline)
    {
        const float edgeInset =
            (s_missionMarkerSize * screenHeight / 448.0f) * 0.5f + 4.0f;
        visibleHalfX = (std::max)(1.0f, visibleHalfX - edgeInset);
        visibleHalfY = (std::max)(1.0f, visibleHalfY - edgeInset);
    }

    const bool useSquare = !plane.shapeCircle;
    for (unsigned int i = 0; i < MAX_RADAR_TRACES; ++i)
    {
        const tRadarTrace& trace = CRadar::ms_RadarTrace[i];
        const unsigned char spriteId = trace.m_nRadarSprite;
        const bool waypoint = static_cast<int>(i) == waypointIdx;
        const bool missionCheckpoint =
            (trace.m_nBlipType == BLIP_COORD || trace.m_nBlipType == BLIP_CONTACTPOINT)
            && IsMissionCheckpointSprite(spriteId);
        const bool nativeShape = spriteId == RADAR_SPRITE_NONE;
        const bool orbitIndicator = trace.m_nBlipType == BLIP_CHAR
            || trace.m_nBlipType == BLIP_CAR
            || trace.m_nBlipType == BLIP_SPOTLIGHT;
        if (!TraceShowsOnRadar(trace)
            || (!missionCheckpoint && !nativeShape && !waypoint && !orbitIndicator)
            || (!waypoint && !CRadar::HasThisBlipBeenRevealed(static_cast<int>(i))))
            continue;

        CVector world{};
        if (!GetTraceWorldPos(trace, world))
            continue;

        D3DXVECTOR3 radarPos{};
        RadarGeometry::WorldToRadarPos(world.x, world.y, radarPos);
        float px = 0.0f;
        float py = 0.0f;
        bool clampedToOrbit = false;
        if (!RadarGeometry::WorldToCircleScreen(
                radarPos, plane.cameraPos, plane.cameraRot,
                plane.fov, plane.nearPlane, plane.farPlane,
                screenWidth, screenHeight, plane.sizeX, plane.sizeY,
                plane.cx, plane.cy, px, py, plane.projectionAspect))
        {
            const D3DXVECTOR3 playerPos(plane.playerRadarX, plane.playerRadarY, 0.0f);
            float angle = 0.0f;
            if (!MathUtils::DirectionToOrbitAngle(playerPos, radarPos, plane.yaw, angle))
                continue;
            RadarGeometry::PointOnOrbitEdge(
                plane.cx, plane.cy, visibleHalfX, visibleHalfY,
                cosf(angle), sinf(angle), useSquare, px, py);
            clampedToOrbit = true;
        }
        else if (!RadarGeometry::IsInsideOrbit(
                     px, py, plane.cx, plane.cy, visibleHalfX, visibleHalfY, useSquare))
        {
            RadarGeometry::ClampToOrbit(
                px, py, plane.cx, plane.cy, visibleHalfX, visibleHalfY,
                px, py, useSquare);
            clampedToOrbit = true;
        }
        if (orbitIndicator && !nativeShape && !missionCheckpoint && !waypoint && !clampedToOrbit)
            continue;

        float alphaFade = 1.0f;
        if (RadarConfig::GetBlipEdgeFade())
        {
            alphaFade = RadarGeometry::ComputeOrbitEdgeFade(
                px, py, plane.cx, plane.cy, plane.halfX, plane.halfY, useSquare,
                MathUtils::ScaleRadarLength(18.0f));
        }
        if (alphaFade <= 0.0f)
            continue;

        DWORD color = TraceColorToD3D(trace.m_nColour,
            trace.m_bBright != 0, trace.m_bFriendly != 0);
        const BYTE alpha = static_cast<BYTE>(
            static_cast<float>((color >> 24) & 0xFF) * alphaFade);
        color = (color & 0x00FFFFFF) | (static_cast<DWORD>(alpha) << 24);

        draw->dxDrawGTAIndicatorBlipSurface(
            px, py, s_missionMarkerSize * screenHeight / 448.0f, color,
            GetHeightIndicatorType(world.z, playerZ),
            screenWidth, screenHeight, s_missionMarkerOutline);
    }
}

bool BlipManager::IsLegendSprite(unsigned char spriteId)
{
    switch (spriteId)
    {
    case RADAR_SPRITE_BIGSMOKE:
    case RADAR_SPRITE_CATALINAPINK:
    case RADAR_SPRITE_CESARVIAPANDO:
    case RADAR_SPRITE_CJ:
    case RADAR_SPRITE_CRASH1:
    case RADAR_SPRITE_MCSTRAP:
    case RADAR_SPRITE_OGLOC:
    case RADAR_SPRITE_RYDER:
    case RADAR_SPRITE_SWEET:
    case RADAR_SPRITE_THETRUTH:
    case RADAR_SPRITE_TORENORANCH:
    case RADAR_SPRITE_WOOZIE:
    case RADAR_SPRITE_ZERO:
        return true;
    default:
        return false;
    }
}

bool BlipManager::IsMissionCheckpointSprite(unsigned char spriteId)
{
    switch (spriteId)
    {
    case RADAR_SPRITE_NONE:
    case RADAR_SPRITE_QMARK:
        return true;
    default:
        return false;
    }
}

void BlipManager::SetMissionMarkerOptions(float size, bool outline)
{
    s_missionMarkerSize = size;
    s_missionMarkerOutline = outline;
}

eHeightIndicatorType BlipManager::GetHeightIndicatorType(float blipZ, float playerZ, float threshold)
{
    const float diff = blipZ - playerZ;
    if (diff > threshold)
        return HEIGHT_INDICATOR_ABOVE;
    if (diff < -threshold)
        return HEIGHT_INDICATOR_BELOW;
    return HEIGHT_INDICATOR_SAME;
}

DWORD BlipManager::TraceColorToD3D(unsigned int blipColour, bool bright, bool friendly)
{
    CRGBA color = CRadar::GetRadarTraceColour(blipColour, bright ? 1 : 0, friendly ? 1 : 0);
    return D3DCOLOR_ARGB(color.a, color.r, color.g, color.b);
}
