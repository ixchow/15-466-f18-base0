#include "Game.hpp"

#include "gl_errors.hpp" //helper for dumpping OpenGL error messages
#include "read_chunk.hpp" //helper for reading a vector of structures from a file
#include "data_path.hpp" //helper to get paths relative to executable

#include <glm/gtc/type_ptr.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <iostream>
#include <fstream>
#include <map>
#include <cstddef>
#include <random>
#include <chrono>
#include <ctime>

//helper defined later; throws if shader compilation fails:
static GLuint compile_shader(GLenum type, std::string const &source);

Game::Game() {
    { //create an opengl program to perform sun/sky (well, directional+hemispherical) lighting:
        GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
            "#version 330\n"
            "uniform mat4 object_to_clip;\n"
            "uniform mat4x3 object_to_light;\n"
            "uniform mat3 normal_to_light;\n"
            "layout(location=0) in vec4 Position;\n" //note: layout keyword used to make sure that the location-0 attribute is always bound to something
            "in vec3 Normal;\n"
            "in vec4 Color;\n"
            "out vec3 position;\n"
            "out vec3 normal;\n"
            "out vec4 color;\n"
            "void main() {\n"
            "   gl_Position = object_to_clip * Position;\n"
            "   position = object_to_light * Position;\n"
            "   normal = normal_to_light * Normal;\n"
            "   color = Color;\n"
            "}\n"
        );

        GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
            "#version 330\n"
            "uniform vec3 sun_direction;\n"
            "uniform vec3 sun_color;\n"
            "uniform vec3 sky_direction;\n"
            "uniform vec3 sky_color;\n"
            "in vec3 position;\n"
            "in vec3 normal;\n"
            "in vec4 color;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "   vec3 total_light = vec3(0.0, 0.0, 0.0);\n"
            "   vec3 n = normalize(normal);\n"
            "   { //sky (hemisphere) light:\n"
            "       vec3 l = sky_direction;\n"
            "       float nl = 0.5 + 0.5 * dot(n,l);\n"
            "       total_light += nl * sky_color;\n"
            "   }\n"
            "   { //sun (directional) light:\n"
            "       vec3 l = sun_direction;\n"
            "       float nl = max(0.0, dot(n,l));\n"
            "       total_light += nl * sun_color;\n"
            "   }\n"
            "   fragColor = vec4(color.rgb * total_light, color.a);\n"
            "}\n"
        );

        simple_shading.program = glCreateProgram();
        glAttachShader(simple_shading.program, vertex_shader);
        glAttachShader(simple_shading.program, fragment_shader);
        //shaders are reference counted so this makes sure they are freed after program is deleted:
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);

        //link the shader program and throw errors if linking fails:
        glLinkProgram(simple_shading.program);
        GLint link_status = GL_FALSE;
        glGetProgramiv(simple_shading.program, GL_LINK_STATUS, &link_status);
        if (link_status != GL_TRUE) {
            std::cerr << "Failed to link shader program." << std::endl;
            GLint info_log_length = 0;
            glGetProgramiv(simple_shading.program, GL_INFO_LOG_LENGTH, &info_log_length);
            std::vector< GLchar > info_log(info_log_length, 0);
            GLsizei length = 0;
            glGetProgramInfoLog(simple_shading.program, GLsizei(info_log.size()), &length, &info_log[0]);
            std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
            throw std::runtime_error("failed to link program");
        }
    }

    { //read back uniform and attribute locations from the shader program:
        simple_shading.object_to_clip_mat4 = glGetUniformLocation(simple_shading.program, "object_to_clip");
        simple_shading.object_to_light_mat4x3 = glGetUniformLocation(simple_shading.program, "object_to_light");
        simple_shading.normal_to_light_mat3 = glGetUniformLocation(simple_shading.program, "normal_to_light");

        simple_shading.sun_direction_vec3 = glGetUniformLocation(simple_shading.program, "sun_direction");
        simple_shading.sun_color_vec3 = glGetUniformLocation(simple_shading.program, "sun_color");
        simple_shading.sky_direction_vec3 = glGetUniformLocation(simple_shading.program, "sky_direction");
        simple_shading.sky_color_vec3 = glGetUniformLocation(simple_shading.program, "sky_color");

        simple_shading.Position_vec4 = glGetAttribLocation(simple_shading.program, "Position");
        simple_shading.Normal_vec3 = glGetAttribLocation(simple_shading.program, "Normal");
        simple_shading.Color_vec4 = glGetAttribLocation(simple_shading.program, "Color");
    }

    struct Vertex {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::u8vec4 Color;
    };
    static_assert(sizeof(Vertex) == 28, "Vertex should be packed.");

    { //load mesh data from a binary blob:
        std::ifstream blob(data_path("asteroid_game_meshes.blob"), std::ios::binary);
        //The blob will be made up of three chunks:
        // the first chunk will be vertex data (interleaved position/normal/color)
        // the second chunk will be characters
        // the third chunk will be an index, mapping a name (range of characters) to a mesh (range of vertex data)

        //read vertex data:
        std::vector< Vertex > vertices;
        read_chunk(blob, "dat0", &vertices);

        //read character data (for names):
        std::vector< char > names;
        read_chunk(blob, "str0", &names);

        //read index:
        struct IndexEntry {
            uint32_t name_begin;
            uint32_t name_end;
            uint32_t vertex_begin;
            uint32_t vertex_end;
        };
        static_assert(sizeof(IndexEntry) == 16, "IndexEntry should be packed.");

        std::vector< IndexEntry > index_entries;
        read_chunk(blob, "idx0", &index_entries);

        if (blob.peek() != EOF) {
            std::cerr << "WARNING: trailing data in meshes file." << std::endl;
        }

        //upload vertex data to the graphics card:
        glGenBuffers(1, &meshes_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, meshes_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * vertices.size(), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        //create map to store index entries:
        std::map< std::string, Mesh > index;
        for (IndexEntry const &e : index_entries) {
            if (e.name_begin > e.name_end || e.name_end > names.size()) {
                throw std::runtime_error("invalid name indices in index.");
            }
            if (e.vertex_begin > e.vertex_end || e.vertex_end > vertices.size()) {
                throw std::runtime_error("invalid vertex indices in index.");
            }
            Mesh mesh;
            mesh.first = e.vertex_begin;
            mesh.count = e.vertex_end - e.vertex_begin;
            auto ret = index.insert(std::make_pair(
                std::string(names.begin() + e.name_begin, names.begin() + e.name_end),
                mesh));
            if (!ret.second) {
                throw std::runtime_error("duplicate name in index.");
            }
        }

        //look up into index map to extract meshes:
        auto lookup = [&index](std::string const &name) -> Mesh {
            auto f = index.find(name);
            if (f == index.end()) {
                throw std::runtime_error("Mesh named '" + name + "' does not appear in index.");
            }
            return f->second;
        };

        background_mesh = lookup("Background");
        sat_mesh = lookup("Satellite");
        asteroid_mesh = lookup("Asteroid");
        junk_mesh = lookup("Junk");
        health_bar_win_mesh = lookup("HealthBarWin");
        health_bar_foreground_mesh = lookup("HealthBarForeground");

    }

    { //create vertex array object to hold the map from the mesh vertex buffer to shader program attributes:
        glGenVertexArrays(1, &meshes_for_simple_shading_vao);
        glBindVertexArray(meshes_for_simple_shading_vao);
        glBindBuffer(GL_ARRAY_BUFFER, meshes_vbo);
        //note that I'm specifying a 3-vector for a 4-vector attribute here, and this is okay to do:
        glVertexAttribPointer(simple_shading.Position_vec4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Position));
        glEnableVertexAttribArray(simple_shading.Position_vec4);
        if (simple_shading.Normal_vec3 != -1U) {
            glVertexAttribPointer(simple_shading.Normal_vec3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Normal));
            glEnableVertexAttribArray(simple_shading.Normal_vec3);
        }
        if (simple_shading.Color_vec4 != -1U) {
            glVertexAttribPointer(simple_shading.Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Color));
            glEnableVertexAttribArray(simple_shading.Color_vec4);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // initialize audio
    // based on https://www.youtube.com/watch?v=U3IsueoqG58
    {
        SDL_Init(SDL_INIT_AUDIO);

        SDL_AudioSpec want, have;

        SDL_memset(&want, 0, sizeof(want));
        want.freq = 44100;
        want.format = AUDIO_S16;
        want.channels = 2;
        want.samples = 4096;
        auto audio = SDL_OpenAudioDevice(nullptr, false, &want, &have, 0);

        Uint32 wav_length;
        Uint8 *wav_buffer;
        SDL_LoadWAV("sound.wav", &have, &wav_buffer, &wav_length);
        SDL_QueueAudio(audio, wav_buffer, wav_length);
        SDL_PauseAudioDevice(audio, false);
    }

    GL_ERRORS();

    //----------------

    // https://stackoverflow.com/questions/9246536/warning-c4244-argument-conversion-from-time-t-to-unsigned-int-possible
    srand( (unsigned int) time(NULL)); // random seed
}

Game::~Game() {
    glDeleteVertexArrays(1, &meshes_for_simple_shading_vao);
    meshes_for_simple_shading_vao = -1U;

    glDeleteBuffers(1, &meshes_vbo);
    meshes_vbo = -1U;

    glDeleteProgram(simple_shading.program);
    simple_shading.program = -1U;

    GL_ERRORS();
}

bool Game::handle_event(SDL_Event const &evt, glm::uvec2 window_size) {
    //ignore any keys that are the result of automatic key repeat:
    if (evt.type == SDL_KEYDOWN && evt.key.repeat) {
        return false;
    }
    //handle tracking the state of keys for yaw and translation control:
    if (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP) {
        if (evt.key.keysym.scancode == SDL_SCANCODE_Z) {
            controls.yaw_left = (evt.type == SDL_KEYDOWN);
            return true;
        } else if (evt.key.keysym.scancode == SDL_SCANCODE_X) {
            controls.yaw_right = (evt.type == SDL_KEYDOWN);
            return true;
        } else if (evt.key.keysym.scancode == SDL_SCANCODE_LEFT) {
            controls.trans_left = (evt.type == SDL_KEYDOWN);
            return true;
        } else if (evt.key.keysym.scancode == SDL_SCANCODE_RIGHT) {
            controls.trans_right = (evt.type == SDL_KEYDOWN);
            return true;
        } else if (evt.key.keysym.scancode == SDL_SCANCODE_UP) {
            controls.trans_fwd = (evt.type == SDL_KEYDOWN);
            return true;
        } else if (evt.key.keysym.scancode == SDL_SCANCODE_DOWN) {
            controls.trans_back = (evt.type == SDL_KEYDOWN);
            return true;
        } else if (evt.key.keysym.scancode == SDL_SCANCODE_SPACE) {
            controls.grab = (evt.type == SDL_KEYDOWN);
            return true;
        }            
    }   
    return false;
}

void Game::update(float elapsed, uint32_t num_frames) {

    auto compute_distance = [&](Transform const &obj1, Transform const &obj2) {
        return glm::distance(obj1.position, obj2.position);
    };

    {
        float amt_lin = elapsed * 0.2f; // translation unit
        float amt_rot = elapsed * 0.03f; // rotation unit
        glm::vec4 dv = glm::vec4(0.0f); // linear velocity increment
        float dw = 0.0f; // angular velocity increment
        int thruster_count = 0;
        // print out per https://stackoverflow.com/questions/11515469/ ...
        //      how-do-i-print-vector-values-of-type-glmvec3-that-have-been-passed-by-referenc

        if (controls.yaw_left) {
            dw += amt_rot;
            thruster_count++;
        }
        if (controls.yaw_right) {
            dw += -amt_rot;
            thruster_count++;
        }
        if (controls.trans_left) { // all 4 translations are in satellite body frame
            dv += glm::vec4(-amt_lin, 0.0f, 0.0f, 0.0f);
            thruster_count++;
        }
        if (controls.trans_right) {
            dv += glm::vec4(amt_lin, 0.0f, 0.0f, 0.0f);
            thruster_count++;
        }
        if (controls.trans_fwd) {
            dv += glm::vec4(0.0f, amt_lin, 0.0f, 0.0f);
            thruster_count++;
        }
        if (controls.trans_back) {
            dv += glm::vec4(0.0f, -amt_lin, 0.0f, 0.0f);
            thruster_count++;
        }    
        glm::quat &r = sat.transform.rotation;
        glm::quat &w = sat.transform.ang_vel;
        glm::vec3 &s = sat.transform.position;
        glm::vec3 &v = sat.transform.lin_vel;
        dv = glm::mat4_cast(r) * dv; // convert from body to world frame
        w *= glm::quat(glm::vec3(0.0f, 0.0f, dw)); // increment angular velocity
        w = glm::normalize(w);
        r *= w; // increment rotation as well
        r = glm::normalize(r);
        v += glm::vec3(dv); 
        s += v * elapsed; 
        fuel -= thruster_count * fuel_burn_increment;
    }

    for (auto &asteroid: asteroids) {
        glm::quat &r = asteroid.transform.rotation;
        glm::vec3 &v = asteroid.transform.lin_vel;
        glm::vec3 &s = asteroid.transform.position;
        r *= glm::angleAxis(elapsed, glm::vec3(1.0f, 1.0f, 1.0f)); // tumbling motion
        r = glm::normalize(r);
        s += glm::vec3(elapsed * v.x, elapsed * v.y, 0.0f); 
    }

    for (auto &junk: junks) {
        glm::quat &r = junk.transform.rotation;
        glm::vec3 &v = junk.transform.lin_vel;
        glm::vec3 &s = junk.transform.position;
        r *= glm::angleAxis(elapsed, glm::vec3(-1.0f, 1.0f, -1.0f)); // tumbling motion
        r = glm::normalize(r);
        s += glm::vec3(elapsed * v.x, elapsed * v.y, 0.0f); 
    }

    for (auto& asteroid: asteroids){
        if ((compute_distance(sat.transform, asteroid.transform))<=asteroid_capture_distance && controls.grab){
            asteroid.active = false;
            fuel += fuel_asteroid_increment;
        }
    }

    for (auto& junk: junks){
        if ((compute_distance(sat.transform, junk.transform))<=collision_min_distance){
            sat.active = false;
        }
    }        


    if (fuel < 0.0f){
        sat.active = false;
    }

    auto spawn_object = [&](std::vector<FlyingObject> &objs, int edge){
        objs.emplace_back();

        float x_start;
        float x_end;
        float y_start;
        float y_end;

        auto random_in_range = [&](float max, float min){
            return min + static_cast <float> (rand()) / ( static_cast <float> (RAND_MAX/(max-min)));
        };

        switch (edge){
            case 0: // top edge
                x_start = random_in_range(frame_max.x, frame_min.x);
                x_end = random_in_range(frame_max.x, frame_min.x);
                y_start = frame_max.y;
                y_end = frame_min.y;
                break;
            case 1: // right edge
                x_start = frame_max.x;
                x_end = frame_min.x;      
                y_start = random_in_range(frame_max.y, frame_min.y);
                y_end = random_in_range(frame_max.y, frame_min.y);
                break;
            case 2: // bottom edge
                x_start = random_in_range(frame_max.x, frame_min.x);
                x_end = random_in_range(frame_max.x, frame_min.x);
                y_start = frame_min.y;
                y_end = frame_max.y;
                break;
            case 3: // left edge
                x_start = frame_min.x;
                x_end = frame_max.x;      
                y_start = random_in_range(frame_max.y, frame_min.y);
                y_end = random_in_range(frame_max.y, frame_min.y);
                break;
            default:
                break;
        }

        float th = atan2(y_end - y_start, x_end - x_start);
        objs.back().transform = {  glm::angleAxis(0.0f, glm::vec3(1.0f, 0.0f, 0.0f)), 
                                        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 
                                        glm::vec3(x_start, y_start, 0.0f), 
                                        glm::vec3(cos(th)*0.1f, sin(th)*0.1f, 0.0f)};
        objs.back().active = true;
    };

    if (num_frames % asteroid_spawn_interval == 0){
        int edge = rand()/(RAND_MAX/4);
        spawn_object(asteroids, edge);
    }

    if (num_frames % junk_spawn_interval == 0){
        int edge = rand()/(RAND_MAX/4);
        spawn_object(junks, edge);
    }
}

void Game::draw(glm::uvec2 drawable_size) {
    //Set up a transformation matrix to fit the board in the window:
    glm::mat4 world_to_clip;
    {
        float aspect = float(drawable_size.x) / float(drawable_size.y);

        //want scale such that board * scale fits in [-aspect,aspect]x[-1.0,1.0] screen box:
        float scale = glm::min(
            2.0f * aspect / 1.0f,
            2.0f / 1.0f
        );

        //center of board will be placed at center of screen:
        glm::vec2 center = glm::vec2(0.0f, 0.0f);

        //NOTE: glm matrices are specified in column-major order
        world_to_clip = glm::mat4(
            scale / aspect, 0.0f, 0.0f, 0.0f,
            0.0f, scale, 0.0f, 0.0f,
            0.0f, 0.0f,-1.0f, 0.0f,
            -(scale / aspect) * center.x, -scale * center.y, 0.0f, 1.0f
        );
    }

    //set up graphics pipeline to use data from the meshes and the simple shading program:
    glBindVertexArray(meshes_for_simple_shading_vao);
    glUseProgram(simple_shading.program);

    glUniform3fv(simple_shading.sun_color_vec3, 1, glm::value_ptr(glm::vec3(0.81f, 0.81f, 0.76f)));
    glUniform3fv(simple_shading.sun_direction_vec3, 1, glm::value_ptr(glm::normalize(glm::vec3(-0.2f, 0.2f, 1.0f))));
    glUniform3fv(simple_shading.sky_color_vec3, 1, glm::value_ptr(glm::vec3(0.2f, 0.2f, 0.3f)));
    glUniform3fv(simple_shading.sky_direction_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 1.0f, 0.0f)));

    //helper function to draw a given mesh with a given transformation:
    auto draw_mesh = [&](Mesh const &mesh, glm::mat4 const &object_to_world) {
        //set up the matrix uniforms:
        if (simple_shading.object_to_clip_mat4 != -1U) {
            glm::mat4 object_to_clip = world_to_clip * object_to_world;
            glUniformMatrix4fv(simple_shading.object_to_clip_mat4, 1, GL_FALSE, glm::value_ptr(object_to_clip));
        }
        if (simple_shading.object_to_light_mat4x3 != -1U) {
            glUniformMatrix4x3fv(simple_shading.object_to_light_mat4x3, 1, GL_FALSE, glm::value_ptr(object_to_world));
        }
        if (simple_shading.normal_to_light_mat3 != -1U) {
            //NOTE: if there isn't any non-uniform scaling in the object_to_world matrix, then the inverse transpose is the matrix itself, and computing it wastes some CPU time:
            glm::mat3 normal_to_world = glm::inverse(glm::transpose(glm::mat3(object_to_world)));
            glUniformMatrix3fv(simple_shading.normal_to_light_mat3, 1, GL_FALSE, glm::value_ptr(normal_to_world));
        }

        //draw the mesh:
        glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
    };

    if (sat.active){
        draw_mesh(sat_mesh,
            glm::mat4(
                0.15f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.15f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                sat.transform.position.x, sat.transform.position.y, 0.0f, 1.0f
            )
            * glm::mat4_cast(sat.transform.rotation)
        );
        if (fuel>1.0f){
            draw_mesh(health_bar_win_mesh,
                glm::mat4(
                    0.03f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.3f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    -0.7f, 0.0f, -0.1f, 1.0f
                )
            );
        } else {
            draw_mesh(health_bar_foreground_mesh,
                glm::mat4(
                    0.03f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.3f*fuel, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    -0.7f, 0.0f, -0.1f, 1.0f
                )
            );            
        }

    }

    for (auto &asteroid: asteroids){
        if (asteroid.active){
            draw_mesh(asteroid_mesh,
                glm::mat4(
                    0.035f, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.035f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.1f, 0.0f,
                    asteroid.transform.position.x, asteroid.transform.position.y, 0.0f, 1.0f
                )
                * glm::mat4_cast(asteroid.transform.rotation)
            );  
        }
    }

    for (auto &junk: junks){
        draw_mesh(junk_mesh,
            glm::mat4(
                0.025f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.025f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.1f, 0.0f,
                junk.transform.position.x, junk.transform.position.y, 0.0f, 1.0f
            )
            * glm::mat4_cast(junk.transform.rotation)
        );        
    }

    glUseProgram(0);

    GL_ERRORS();
}

//create and return an OpenGL vertex shader from source:
static GLuint compile_shader(GLenum type, std::string const &source) {
    GLuint shader = glCreateShader(type);
    GLchar const *str = source.c_str();
    GLint length = GLint(source.size());
    glShaderSource(shader, 1, &str, &length);
    glCompileShader(shader);
    GLint compile_status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status != GL_TRUE) {
        std::cerr << "Failed to compile shader." << std::endl;
        GLint info_log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
        std::vector< GLchar > info_log(info_log_length, 0);
        GLsizei length = 0;
        glGetShaderInfoLog(shader, GLsizei(info_log.size()), &length, &info_log[0]);
        std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
        glDeleteShader(shader);
        throw std::runtime_error("Failed to compile shader.");
    }
    return shader;
}
