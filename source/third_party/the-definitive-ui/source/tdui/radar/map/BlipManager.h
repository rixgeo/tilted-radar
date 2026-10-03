/*****************************************************************************
 *
 *  PROJECT:     The-Definitive-UI
 *  FILE:        source/Radar/mapmanager/BlipManager.h
 *  PURPOSE:     Radar blips - stock sprite textures and overlay drawing
 *
 ****************************************************************************/

#pragma once

#include <d3d9.h>
#include "CRadar.h"
#include "RenderWare.h"
#include "BlipTypes.h"
#include "StockRadarDraw.h"

class DxDrawPrimitives;

class BlipManager
{
public:
    static const int MAX_BLIP_ID = 63;

    explicit BlipManager(LPDIRECT3DDEVICE9 pDevice);
    ~BlipManager();

    bool Initialize();
    void Shutdown();

    LPDIRECT3DTEXTURE9 GetStockSpriteTexture(int spriteId);
    LPDIRECT3DTEXTURE9 GetOwnedSpriteTexture(int spriteId);

    void DrawStockOverlay(const StockRadarPlane& plane, bool gangZones);

    // D3D blips into a square RT; composited through the circle shader on HUD.
    void DrawBlipsToRenderTarget(const StockRadarPlane& plane, DxDrawPrimitives* draw,
                                 float hudRadarSizeX, float defaultSpriteSize,
                                 bool previewPedOnly, bool useAircraftIcon);
    void DrawMissionMarkersOnHud(const StockRadarPlane& plane, DxDrawPrimitives* draw,
                                 float screenWidth, float screenHeight);

    static bool                 IsLegendSprite(unsigned char spriteId);
    static bool                 IsMissionCheckpointSprite(unsigned char spriteId);
    static void                 SetMissionMarkerOptions(float size, bool outline);
    static eHeightIndicatorType GetHeightIndicatorType(float blipZ, float playerZ, float threshold = 2.0f);
    static DWORD                TraceColorToD3D(unsigned int blipColour, bool bright, bool friendly);
    static float                GetIconCanvasScale();
    static float                GetStockSpriteDiameterPx(float rtScale = 1.0f);
    static float                GetTraceMarkerDiameterPx(unsigned char blipSize, float rtScale = 1.0f);

private:
    LPDIRECT3DTEXTURE9 ConvertRwTexture(RwTexture* rwTex);

    LPDIRECT3DDEVICE9  m_pDevice;
    LPDIRECT3DTEXTURE9 m_stockTextures[MAX_BLIP_ID + 1];
    RwTexture*         m_stockTextureSources[MAX_BLIP_ID + 1];
    RwRaster*          m_stockTextureSourceRasters[MAX_BLIP_ID + 1];
    bool               m_bInitialized;
};
