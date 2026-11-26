#pragma once

// A camera whose answers can be written by hand, shared by every test that projects.

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

#include "viewer/game/camera.h"

namespace test_camera {

constexpr float kFov = 1.222f; // ~70 degrees vertical, the value GW2 reports by default
constexpr float kWidth = 1920.0f;
constexpr float kHeight = 1080.0f;

// At the origin looking down +Z with no roll and no pitch, so view space and world space
// coincide.
inline viewer::Camera axisCamera() {
    viewer::Camera camera;
    camera.position = glm::vec3(0.0f);
    camera.front = glm::vec3(0.0f, 0.0f, 1.0f);
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.viewport = glm::vec2(kWidth, kHeight);
    camera.aspect = kWidth / kHeight;
    camera.fov = kFov;
    camera.view = glm::lookAtLH(camera.position, camera.position + camera.front, camera.up);
    camera.projection = glm::perspectiveLH(camera.fov, camera.aspect, camera.near_plane, 10000.0f);
    camera.view_projection = camera.projection * camera.view;
    camera.valid = true;
    return camera;
}

} // namespace test_camera
