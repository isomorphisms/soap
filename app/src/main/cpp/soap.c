#include <android/asset_manager.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "SoapFilms"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static const char *VERTEX_SHADER =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_position;\n"
    "out vec2 v_ndc;\n"
    "void main() {\n"
    "    v_ndc = a_position;\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

enum gesture_kind {
    GESTURE_NONE,
    GESTURE_ORBIT,
    GESTURE_PINCH_PAN,
    GESTURE_BLOCKED
};

struct engine {
    struct android_app *app;

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int32_t width;
    int32_t height;

    GLuint program;
    GLuint vao;
    GLuint vbo;
    GLint aspect_location;
    GLint yaw_location;
    GLint pitch_location;
    GLint zoom_location;
    GLint pan_location;

    char surface_asset[128];

    float yaw;
    float pitch;
    float zoom;
    float pan[2];

    enum gesture_kind gesture;
    float last_x;
    float last_y;
    float pinch_last_distance;
    float pinch_last_mid_x;
    float pinch_last_mid_y;

    bool dirty;
    bool logged_first_frame;
};

static char *load_asset_text(AAssetManager *manager, const char *name) {
    AAsset *asset = AAssetManager_open(manager, name, AASSET_MODE_BUFFER);
    if (asset == NULL) {
        LOGE("could not open asset %s", name);
        return NULL;
    }

    off64_t length = AAsset_getLength64(asset);
    char *text = malloc((size_t)length + 1u);
    if (text == NULL) {
        AAsset_close(asset);
        return NULL;
    }

    off64_t offset = 0;
    while (offset < length) {
        int amount = AAsset_read(asset, text + offset, (size_t)(length - offset));
        if (amount <= 0) {
            LOGE("could not read asset %s", name);
            free(text);
            AAsset_close(asset);
            return NULL;
        }
        offset += amount;
    }

    text[length] = '\0';
    AAsset_close(asset);
    return text;
}

static int count_nonempty_lines(const char *text) {
    int count = 0;
    const char *cursor = text;

    while (*cursor != '\0') {
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r') {
            cursor++;
        }
        if (cursor > start) {
            count++;
        }
        while (*cursor == '\n' || *cursor == '\r') {
            cursor++;
        }
    }
    return count;
}

static bool copy_line(const char *text, int wanted, char *output, size_t output_size) {
    int index = 0;
    const char *cursor = text;

    while (*cursor != '\0') {
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r') {
            cursor++;
        }
        size_t length = (size_t)(cursor - start);
        if (length > 0) {
            if (index == wanted) {
                if (length + 1u > output_size) {
                    return false;
                }
                memcpy(output, start, length);
                output[length] = '\0';
                return true;
            }
            index++;
        }
        while (*cursor == '\n' || *cursor == '\r') {
            cursor++;
        }
    }
    return false;
}

static bool choose_surface(struct engine *engine) {
    if (engine->surface_asset[0] != '\0') {
        return true;
    }

    char *index = load_asset_text(engine->app->activity->assetManager, "surfaces/index.txt");
    if (index == NULL) {
        return false;
    }

    int count = count_nonempty_lines(index);
    if (count <= 0) {
        LOGE("surface index is empty");
        free(index);
        return false;
    }

    struct timespec now = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t seed = (uint64_t)now.tv_sec * UINT64_C(1000000007) ^ (uint64_t)now.tv_nsec;
    int selected = (int)(seed % (uint64_t)count);

    char file_name[96] = {0};
    bool copied = copy_line(index, selected, file_name, sizeof(file_name));
    free(index);
    if (!copied) {
        LOGE("could not read selected surface %d", selected);
        return false;
    }

    int written = snprintf(
        engine->surface_asset,
        sizeof(engine->surface_asset),
        "surfaces/%s",
        file_name
    );
    if (written < 0 || (size_t)written >= sizeof(engine->surface_asset)) {
        engine->surface_asset[0] = '\0';
        LOGE("surface asset name is too long");
        return false;
    }

    LOGI("selected surface %s (%d of %d)", engine->surface_asset, selected + 1, count);
    return true;
}

static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    char *log = length > 0 ? malloc((size_t)length) : NULL;
    if (log != NULL) {
        glGetShaderInfoLog(shader, length, NULL, log);
        LOGE("shader compilation failed: %s", log);
        free(log);
    } else {
        LOGE("shader compilation failed");
    }

    glDeleteShader(shader);
    return 0;
}

static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        return program;
    }

    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    char *log = length > 0 ? malloc((size_t)length) : NULL;
    if (log != NULL) {
        glGetProgramInfoLog(program, length, NULL, log);
        LOGE("program link failed: %s", log);
        free(log);
    } else {
        LOGE("program link failed");
    }

    glDeleteProgram(program);
    return 0;
}

static bool create_renderer(struct engine *engine) {
    static const GLfloat fullscreen_triangle[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f
    };

    if (!choose_surface(engine)) {
        return false;
    }

    char *fragment_source =
        load_asset_text(engine->app->activity->assetManager, engine->surface_asset);
    if (fragment_source == NULL) {
        return false;
    }

    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    free(fragment_source);

    if (vertex_shader == 0 || fragment_shader == 0) {
        if (vertex_shader != 0) {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0) {
            glDeleteShader(fragment_shader);
        }
        return false;
    }

    engine->program = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    if (engine->program == 0) {
        return false;
    }

    engine->aspect_location = glGetUniformLocation(engine->program, "u_aspect");
    engine->yaw_location = glGetUniformLocation(engine->program, "u_yaw");
    engine->pitch_location = glGetUniformLocation(engine->program, "u_pitch");
    engine->zoom_location = glGetUniformLocation(engine->program, "u_zoom");
    engine->pan_location = glGetUniformLocation(engine->program, "u_pan");

    if (engine->aspect_location < 0 || engine->yaw_location < 0 ||
        engine->pitch_location < 0 || engine->zoom_location < 0 ||
        engine->pan_location < 0) {
        LOGE("generated surface shader lost the camera uniform contract");
        return false;
    }

    glGenVertexArrays(1, &engine->vao);
    glBindVertexArray(engine->vao);

    glGenBuffers(1, &engine->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, engine->vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)sizeof(fullscreen_triangle),
        fullscreen_triangle,
        GL_STATIC_DRAW
    );
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        2 * (GLsizei)sizeof(GLfloat),
        (const void *)0
    );
    glEnableVertexAttribArray(0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    LOGI(
        "renderer ready: GL_VERSION=%s GL_RENDERER=%s surface=%s",
        glGetString(GL_VERSION),
        glGetString(GL_RENDERER),
        engine->surface_asset
    );
    return true;
}

static void destroy_display(struct engine *engine);

static bool initialize_display(struct engine *engine) {
    if (engine->app->window == NULL) {
        return false;
    }

    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        LOGE("eglInitialize failed: 0x%x", eglGetError());
        return false;
    }

    EGLConfig config = NULL;
    EGLint config_count = 0;
    if (!eglChooseConfig(
            display,
            config_attributes,
            &config,
            1,
            &config_count
        ) || config_count != 1) {
        LOGE("could not choose GLES3 EGL config: 0x%x", eglGetError());
        eglTerminate(display);
        return false;
    }

    EGLint format = 0;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, format);

    EGLSurface surface =
        eglCreateWindowSurface(display, config, engine->app->window, NULL);
    EGLContext context =
        eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);

    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT) {
        LOGE("could not create EGL surface/context: 0x%x", eglGetError());
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        eglTerminate(display);
        return false;
    }

    if (!eglMakeCurrent(display, surface, surface, context)) {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        return false;
    }

    engine->display = display;
    engine->surface = surface;
    engine->context = context;
    eglQuerySurface(display, surface, EGL_WIDTH, &engine->width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &engine->height);

    if (!create_renderer(engine)) {
        destroy_display(engine);
        return false;
    }

    engine->dirty = true;
    return true;
}

static void destroy_display(struct engine *engine) {
    if (engine->display == EGL_NO_DISPLAY) {
        return;
    }

    if (engine->program != 0) {
        glDeleteProgram(engine->program);
        engine->program = 0;
    }
    if (engine->vbo != 0) {
        glDeleteBuffers(1, &engine->vbo);
        engine->vbo = 0;
    }
    if (engine->vao != 0) {
        glDeleteVertexArrays(1, &engine->vao);
        engine->vao = 0;
    }

    eglMakeCurrent(
        engine->display,
        EGL_NO_SURFACE,
        EGL_NO_SURFACE,
        EGL_NO_CONTEXT
    );
    if (engine->context != EGL_NO_CONTEXT) {
        eglDestroyContext(engine->display, engine->context);
    }
    if (engine->surface != EGL_NO_SURFACE) {
        eglDestroySurface(engine->display, engine->surface);
    }
    eglTerminate(engine->display);

    engine->display = EGL_NO_DISPLAY;
    engine->surface = EGL_NO_SURFACE;
    engine->context = EGL_NO_CONTEXT;
}

static void draw_frame(struct engine *engine) {
    if (engine->display == EGL_NO_DISPLAY || engine->program == 0 ||
        engine->width <= 0 || engine->height <= 0) {
        return;
    }

    glViewport(0, 0, engine->width, engine->height);
    glUseProgram(engine->program);
    glUniform1f(
        engine->aspect_location,
        (float)engine->width / (float)engine->height
    );
    glUniform1f(engine->yaw_location, engine->yaw);
    glUniform1f(engine->pitch_location, engine->pitch);
    glUniform1f(engine->zoom_location, engine->zoom);
    glUniform2f(engine->pan_location, engine->pan[0], engine->pan[1]);

    glBindVertexArray(engine->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (!engine->logged_first_frame) {
        GLubyte pixel[4] = {0, 0, 0, 0};
        glReadPixels(
            engine->width / 2,
            engine->height / 2,
            1,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixel
        );
        LOGI(
            "first frame center rgba=%u,%u,%u,%u glError=0x%x",
            pixel[0],
            pixel[1],
            pixel[2],
            pixel[3],
            glGetError()
        );
        engine->logged_first_frame = true;
    }

    if (!eglSwapBuffers(engine->display, engine->surface)) {
        LOGE("eglSwapBuffers failed: 0x%x", eglGetError());
    }
    engine->dirty = false;
}

static float pointer_distance(const AInputEvent *event) {
    float dx = AMotionEvent_getX(event, 0) - AMotionEvent_getX(event, 1);
    float dy = AMotionEvent_getY(event, 0) - AMotionEvent_getY(event, 1);
    return hypotf(dx, dy);
}

static void pointer_midpoint(const AInputEvent *event, float *x, float *y) {
    *x = 0.5f * (AMotionEvent_getX(event, 0) + AMotionEvent_getX(event, 1));
    *y = 0.5f * (AMotionEvent_getY(event, 0) + AMotionEvent_getY(event, 1));
}

static void orbit_by_pixels(struct engine *engine, float dx, float dy) {
    engine->yaw += dx * 0.0075f;
    engine->pitch += dy * 0.0075f;

    if (engine->pitch < -1.45f) {
        engine->pitch = -1.45f;
    }
    if (engine->pitch > 1.45f) {
        engine->pitch = 1.45f;
    }
    engine->dirty = true;
}

static void pan_by_pixels(struct engine *engine, float dx, float dy) {
    if (engine->height <= 0) {
        return;
    }

    float world_per_pixel = 3.0f / ((float)engine->height * engine->zoom);
    engine->pan[0] -= dx * world_per_pixel;
    engine->pan[1] += dy * world_per_pixel;
    engine->dirty = true;
}

static int32_t handle_input(struct android_app *app, AInputEvent *event) {
    struct engine *engine = app->userData;
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
        return 0;
    }

    int32_t action = AMotionEvent_getAction(event);
    int32_t masked_action = action & AMOTION_EVENT_ACTION_MASK;
    size_t pointer_count = AMotionEvent_getPointerCount(event);

    switch (masked_action) {
        case AMOTION_EVENT_ACTION_DOWN:
            engine->gesture = GESTURE_ORBIT;
            engine->last_x = AMotionEvent_getX(event, 0);
            engine->last_y = AMotionEvent_getY(event, 0);
            return 1;

        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            if (pointer_count == 2) {
                engine->gesture = GESTURE_PINCH_PAN;
                engine->pinch_last_distance = pointer_distance(event);
                pointer_midpoint(
                    event,
                    &engine->pinch_last_mid_x,
                    &engine->pinch_last_mid_y
                );
                return 1;
            }
            engine->gesture = GESTURE_BLOCKED;
            return 1;

        case AMOTION_EVENT_ACTION_MOVE:
            if (engine->gesture == GESTURE_ORBIT && pointer_count == 1) {
                float x = AMotionEvent_getX(event, 0);
                float y = AMotionEvent_getY(event, 0);
                orbit_by_pixels(engine, x - engine->last_x, y - engine->last_y);
                engine->last_x = x;
                engine->last_y = y;
                return 1;
            }

            if (engine->gesture == GESTURE_PINCH_PAN && pointer_count >= 2) {
                float midpoint_x = 0.0f;
                float midpoint_y = 0.0f;
                pointer_midpoint(event, &midpoint_x, &midpoint_y);

                pan_by_pixels(
                    engine,
                    midpoint_x - engine->pinch_last_mid_x,
                    midpoint_y - engine->pinch_last_mid_y
                );

                float distance = pointer_distance(event);
                if (distance > 1.0f && engine->pinch_last_distance > 1.0f) {
                    engine->zoom *= distance / engine->pinch_last_distance;
                    if (engine->zoom < 0.35f) {
                        engine->zoom = 0.35f;
                    }
                    if (engine->zoom > 5.0f) {
                        engine->zoom = 5.0f;
                    }
                    engine->dirty = true;
                }

                engine->pinch_last_distance = distance;
                engine->pinch_last_mid_x = midpoint_x;
                engine->pinch_last_mid_y = midpoint_y;
                return 1;
            }
            return engine->gesture == GESTURE_BLOCKED ? 1 : 0;

        case AMOTION_EVENT_ACTION_POINTER_UP:
            if (engine->gesture == GESTURE_PINCH_PAN) {
                /*
                 * Do not silently reinterpret the remaining finger as an orbit.
                 * Wait for a fresh DOWN so gesture ownership never jumps when
                 * Android compacts pointer indices after POINTER_UP.
                 */
                engine->gesture = GESTURE_BLOCKED;
                return 1;
            }
            return engine->gesture == GESTURE_BLOCKED ? 1 : 0;

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            engine->gesture = GESTURE_NONE;
            return 1;

        default:
            return 0;
    }
}

static void handle_command(struct android_app *app, int32_t command) {
    struct engine *engine = app->userData;

    switch (command) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != NULL && engine->display == EGL_NO_DISPLAY) {
                if (!initialize_display(engine)) {
                    LOGE("display initialization failed");
                }
            }
            break;

        case APP_CMD_TERM_WINDOW:
            destroy_display(engine);
            break;

        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_WINDOW_RESIZED:
            if (engine->display != EGL_NO_DISPLAY) {
                eglQuerySurface(
                    engine->display,
                    engine->surface,
                    EGL_WIDTH,
                    &engine->width
                );
                eglQuerySurface(
                    engine->display,
                    engine->surface,
                    EGL_HEIGHT,
                    &engine->height
                );
                engine->dirty = true;
            }
            break;

        default:
            break;
    }
}

void android_main(struct android_app *app) {
    app_dummy();

    struct engine engine = {
        .app = app,
        .display = EGL_NO_DISPLAY,
        .surface = EGL_NO_SURFACE,
        .context = EGL_NO_CONTEXT,
        .width = 0,
        .height = 0,
        .program = 0,
        .vao = 0,
        .vbo = 0,
        .surface_asset = {0},
        .yaw = 0.45f,
        .pitch = -0.25f,
        .zoom = 1.25f,
        .pan = {0.0f, 0.0f},
        .gesture = GESTURE_NONE,
        .dirty = false,
        .logged_first_frame = false
    };

    app->userData = &engine;
    app->onAppCmd = handle_command;
    app->onInputEvent = handle_input;

    while (true) {
        int events = 0;
        struct android_poll_source *source = NULL;
        int timeout = engine.dirty ? 0 : -1;

        int ident;
        while ((ident = ALooper_pollOnce(
                    timeout,
                    NULL,
                    &events,
                    (void **)&source
                )) >= 0) {
            if (source != NULL) {
                source->process(app, source);
            }

            if (app->destroyRequested != 0) {
                destroy_display(&engine);
                return;
            }

            timeout = 0;
        }

        if (engine.dirty) {
            draw_frame(&engine);
        }
    }
}
