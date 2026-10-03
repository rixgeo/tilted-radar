/*****************************************************************************
 *
 *  PROJECT:     The-Definitive-UI
 *  FILE:        source/Radar/render/RadarViewContext.cpp
 *  PURPOSE:     Per-frame radar view state shared between renderer stages
 *
 *****************************************************************************/

#include "RadarViewContext.h"

float RadarViewContext::CalculateBlipSize(float baseBlipSize) const
{
    return baseBlipSize * (static_cast<float>(screenHeight) / 1080.0f);
}
