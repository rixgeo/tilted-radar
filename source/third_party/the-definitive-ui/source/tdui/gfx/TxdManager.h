/*****************************************************************************
 *
 *  PROJECT:     The-Definitive-UI
 *  FILE:        source/TxdManager/TxdManager.h
 *  PURPOSE:     Load textures from .txd (same approach as Radar Trilogy reference)
 *
 *****************************************************************************/

#pragma once

#include <d3d9.h>
#include <string>
#include <unordered_map>

struct RwTexDictionary;
struct RwTexture;

class TxdManager
{
public:
    TxdManager();
    ~TxdManager();

    bool Initialize(LPDIRECT3DDEVICE9 pDevice);
    void Shutdown();

    bool IsInitialized() const { return m_bInitialized; }

    // Load / unload dictionary (path relative to plugin or absolute)
    bool LoadTxd(const char* szPath);
    void UnloadTxd();
    bool IsTxdLoaded() const { return m_pTxd != nullptr; }

    // Find by name inside loaded TXD and convert to D3D9 (cached)
    LPDIRECT3DTEXTURE9 GetTexture(const char* szName);
    LPDIRECT3DTEXTURE9 LoadTexture(const char* szName); // alias of GetTexture

    void ReleaseCachedTextures();

    // Convert single RwTexture -> IDirect3DTexture9 (caller owns ref)
    static LPDIRECT3DTEXTURE9 RwTextureToD3D9(LPDIRECT3DDEVICE9 pDevice, RwTexture* pRwTex);
    // Copies a native RenderWare D3D9 texture into a managed texture (caller owns ref).
    static LPDIRECT3DTEXTURE9 CopyRwTextureFromD3D(LPDIRECT3DDEVICE9 pDevice, RwTexture* pRwTex);
    // Copies a named texture from an already loaded game TXD.
    static LPDIRECT3DTEXTURE9 CopyNamedTextureFromGameTxd(
        LPDIRECT3DDEVICE9 pDevice, const char* txdName, const char* textureName);
    // Copies one stock radar dictionary from the game TXD pool. Caller owns the returned ref.
    static bool LoadStockRadarTile(LPDIRECT3DDEVICE9 pDevice, int tileIndex,
                                   LPDIRECT3DTEXTURE9* outTile);

private:
    LPDIRECT3DDEVICE9  m_pDevice;
    RwTexDictionary*   m_pTxd;
    bool               m_bInitialized;

    std::unordered_map<std::string, LPDIRECT3DTEXTURE9> m_cache;
};
