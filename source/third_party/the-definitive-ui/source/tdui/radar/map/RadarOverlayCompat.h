/*****************************************************************************
 *  PROJECT:     The-Definitive-UI
 *  FILE:        source/Radar/mapmanager/RadarOverlayCompat.h
 *  PURPOSE:     Dispatch third-party radar overlay callbacks
 *****************************************************************************/

#pragma once

namespace RadarOverlayCompat
{
    void InvokeHudBlips();
    bool IsInvokingHudBlips();

    // Pause-map overlays remain available to third-party plugins.
    void InvokePauseMapOverlay(bool drawStockGangOverlay);

    bool IsInvokingPauseMapOverlay();
}
