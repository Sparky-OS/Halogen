// Copyright 2026 Jonathan Hale
// SPDX-License-Identifier: MIT

#include "Camera.h"

#include "Constants.h"
#include "MathConversions.h"

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <utils/EntityManager.h>

#include <Corrade/Containers/StringStl.h>
#include <Corrade/Containers/StringView.h>

#include <cmath>

using namespace Corrade::Containers::Literals;

ANARI_HALOGEN_TYPEFOR_DEFINITION(Halogen::Camera *);

namespace Halogen {

Camera::Camera(DeviceState *s, const char *subtype)
    : Object(ANARI_CAMERA, s)
    , mSubtype(subtype ? subtype : "perspective")
{
    filament::Engine * const engine = s->engine;
    mEntity = utils::EntityManager::get().create();
    filament::Camera *cam = engine->createCamera(mEntity);
    mCamera = {engine, cam, [engine, e = mEntity]() {
        engine->destroyCameraComponent(e);
        utils::EntityManager::get().destroy(e);
    }};
}

Camera::~Camera() = default;

void Camera::commitParameters()
{
    using float3 = anari::math::float3;
    const float3 position = getParam<float3>(
        "position", float3(0.0f, 0.0f, 0.0f));
    const float3 direction = getParam<float3>(
        "direction", float3(0.0f, 0.0f, -1.0f));
    const float3 up = getParam<float3>("up", float3(0.0f, 1.0f, 0.0f));

    const filament::math::double3 eye = toDouble3(position);
    const filament::math::double3 center = {
        position[0] + direction[0],
        position[1] + direction[1],
        position[2] + direction[2]};
    const filament::math::double3 upVec = toDouble3(up);
    mCamera->lookAt(eye, center, upVec);

    const float aspect = getParam<float>("aspect", 1.0f);
    const float near = getParam<float>("near", 0.1f);
    const float far = getParam<float>("far", 1000.0f);

    // imageRegion (KHR_CAMERA_PERSPECTIVE and KHR_CAMERA_ORTHOGRAPHIC): the
    // part of the image plane, in [0, 1] x [0, 1] with the origin at the
    // lower left, that the frame shows. Its box can lie partly or wholly
    // outside the unit square: the frustum then grows on that side, which is
    // how a camera gets an off-axis frustum (stereo eyes, tiles, lens shift).
    // Unset, or [0, 0, 1, 1], leaves the projection exactly as it was.
    float region[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    bool useRegion = getParam("imageRegion", ANARI_FLOAT32_BOX2, region)
        && !(region[0] == 0.0f && region[1] == 0.0f && region[2] == 1.0f
            && region[3] == 1.0f);
    if (useRegion
        && !(std::isfinite(region[0]) && std::isfinite(region[1])
            && std::isfinite(region[2]) && std::isfinite(region[3])
            && region[0] < region[2] && region[1] < region[3])) {
        reportMessage(ANARI_SEVERITY_ERROR,
            "camera imageRegion must be a finite box with lower < upper; "
            "the whole image is used");
        useRegion = false;
    }

    if (mSubtype == "orthographic"_s) {
        const float height = getParam<float>("height", 1.0f);
        const float halfH = height * 0.5f;
        const float halfW = halfH * aspect;
        if (useRegion) {
            mCamera->setProjection(filament::Camera::Projection::ORTHO,
                -halfW + region[0] * 2.0f * halfW,
                -halfW + region[2] * 2.0f * halfW,
                -halfH + region[1] * 2.0f * halfH,
                -halfH + region[3] * 2.0f * halfH, near, far);
        } else {
            mCamera->setProjection(
                filament::Camera::Projection::ORTHO,
                -halfW, halfW, -halfH, halfH, near, far);
        }
    } else {
        const float fovy = getParam<float>("fovy", Pi / 3.0f);
        const float fovDegrees = fovy * 180.0f / Pi;
        if (useRegion) {
            const double halfH = near * std::tan(double(fovy) * 0.5);
            const double halfW = halfH * aspect;
            mCamera->setProjection(filament::Camera::Projection::PERSPECTIVE,
                -halfW + region[0] * 2.0 * halfW,
                -halfW + region[2] * 2.0 * halfW,
                -halfH + region[1] * 2.0 * halfH,
                -halfH + region[3] * 2.0 * halfH, near, far);
        } else {
            mCamera->setProjection(fovDegrees, aspect, near, far);
        }
    }

    // Neutral exposure by default so Filament's rendered luminance maps
    // directly to pixel values.  The "exposure" parameter is a Filament
    // extension (not part of standard ANARI).
    const float exposure = getParam<float>("exposure", 1.0f);
    mCamera->setExposure(exposure);

    markCommitted();
}

}
