/*****************************************************************************
 *
 *  PROJECT:     The-Definitive-UI
 *  FILE:        source/Radar/render/RadarViewContext.h
 *  PURPOSE:     Per-frame radar view state shared between renderer stages
 *
 *****************************************************************************/

#pragma once

struct RadarViewContext
{
    float circleX, circleY;
    float sizeX, sizeY;
    float centerX, centerY;
    float halfX, halfY;

    float rtWidth, rtHeight;
    int   screenWidth, screenHeight;
    float screenAspect;

    bool shapeCircle;
    bool borderShapeCircle;

    float nearPlane;
    float farPlane;

    bool  isInAircraft;
    bool  isInPlane;
    float rollAngle;
    float pitchAngle;

    void* player;  // CPed* — avoid game include in header

    // Blip size helper: scale base size uniformly with the viewport height
    float CalculateBlipSize(float baseBlipSize = 24.0f) const;
};
