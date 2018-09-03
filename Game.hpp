#pragma once

#include "GL.hpp"

#include <SDL.h>
#include <SDL_audio.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

// The 'Game' struct holds all of the game-relevant state,
// and is called by the main loop.

struct Game {
    //Game creates OpenGL resources (i.e. vertex buffer objects) in its
    //constructor and frees them in its destructor.
    Game();
    ~Game();

    //handle_event is called when new mouse or keyboard events are received:
    // (note that this might be many times per frame or never)
    //The function should return 'true' if it handled the event.
    bool handle_event(SDL_Event const &evt, glm::uvec2 window_size);

    //update is called at the start of a new frame, after events are handled:
    void update(float elapsed, uint32_t num_frames);

    //draw is called after update:
    void draw(glm::uvec2 drawable_size);

    //------- opengl resources -------

    //shader program that draws lit objects with vertex colors:
    struct {
        GLuint program = -1U; //program object

        //uniform locations:
        GLuint object_to_clip_mat4 = -1U;
        GLuint object_to_light_mat4x3 = -1U;
        GLuint normal_to_light_mat3 = -1U;
        GLuint sun_direction_vec3 = -1U;
        GLuint sun_color_vec3 = -1U;
        GLuint sky_direction_vec3 = -1U;
        GLuint sky_color_vec3 = -1U;

        //attribute locations:
        GLuint Position_vec4 = -1U;
        GLuint Normal_vec3 = -1U;
        GLuint Color_vec4 = -1U;
    } simple_shading;

    //mesh data, stored in a vertex buffer:
    GLuint meshes_vbo = -1U; //vertex buffer holding mesh data

    //The location of each mesh in the meshes vertex buffer:
    struct Mesh {
        GLint first = 0;
        GLsizei count = 0;
    };

    Mesh background_mesh;
    Mesh sat_mesh;
    Mesh asteroid_mesh;
    Mesh junk_mesh;
    Mesh health_bar_win_mesh;
    Mesh health_bar_foreground_mesh;

    GLuint meshes_for_simple_shading_vao = -1U; //vertex array object that describes how to connect the meshes_vbo to the simple_shading_program

    //------- game state -------

    struct Transform {
        glm::quat rotation;
        glm::quat ang_vel;
        glm::vec3 position;
        glm::vec3 lin_vel;
    };

    float fuel = 0.6f;
    float fuel_burn_increment = 0.0005f;
    float fuel_asteroid_increment = 0.03f;

    uint32_t asteroid_spawn_interval = 800;
    uint32_t junk_spawn_interval = 400;

    float asteroid_capture_distance = 0.07f;
    float collision_min_distance = 0.1f;

    glm::vec2 frame_max = glm::vec2(0.85f, 0.5f);
    glm::vec2 frame_min = glm::vec2(-0.85f, -0.5f);

    struct {
        bool yaw_left = false;
        bool yaw_right = false;
        bool trans_left = false;
        bool trans_right = false;
        bool trans_fwd = false;
        bool trans_back = false;
        bool grab = false;
    } controls;

    struct FlyingObject {
        Transform transform;
        bool active;
    };

    FlyingObject sat {
        {   glm::angleAxis(0.0f, glm::vec3(1.0f, 0.0f, 0.0f)), // start pointing upwards
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), // not rotating
            glm::vec3(0.0f), // at the origin
            glm::vec3(0.0f)}, // stationary
        true};

    std::vector<FlyingObject> asteroids;
    std::vector<FlyingObject> junks;
};
