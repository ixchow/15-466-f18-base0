#pragma once

#include "GL.hpp"

#include <SDL.h>
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
    void update(float elapsed);

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
    Mesh satellite_mesh;


    GLuint meshes_for_simple_shading_vao = -1U; //vertex array object that describes how to connect the meshes_vbo to the simple_shading_program

    //------- game state -------

    glm::uvec2 board_size = glm::uvec2(5,4);
    std::vector< Mesh const * > board_meshes;
    std::vector< glm::quat > board_rotations;

    glm::uvec2 cursor = glm::vec2(0,0);

    struct {
        bool yaw_left = false;
        bool yaw_right = false;
        bool trans_left = false;
        bool trans_right = false;
        bool trans_fwd = false;
        bool trans_back = false;
    } controls;

};
