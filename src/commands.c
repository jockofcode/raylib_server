#include "commands.h"
#include "handle_registry.h"
#include "b64.h"
#include "color.h"
#include "protocol.h"
#include "rls_log.h"
#include "raylib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Resource wrappers
// Font and Music need the underlying data buffer kept alive after loading.
// Both structs have the raylib type as their FIRST member so that a pointer
// to a FontResource / MusicResource can be safely cast to Font* / Music*.
// ---------------------------------------------------------------------------

typedef struct {
    Font          font;
    unsigned char *data;
    size_t         data_len;
} FontResource;

typedef struct {
    Music         music;
    unsigned char *data;
    size_t         data_len;
} MusicResource;

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

static HandleRegistry *g_registry = NULL;

void commands_init(HandleRegistry *reg) {
    g_registry = reg;
}

static void update_music_cb(int id, void *ptr, void *userdata) {
    (void)id; (void)userdata;
    // MusicResource has Music as first member; plain Music* also works.
    UpdateMusicStream(*(Music *)ptr);
}

void commands_update_music_streams(void) {
    if (g_registry)
        handle_iterate(g_registry, HANDLE_MUSIC, update_music_cb, NULL);
}

// ---------------------------------------------------------------------------
// Scalar helpers
// ---------------------------------------------------------------------------

static Color parse_color(cJSON *val, Color fallback) {
    if (!val) return fallback;
    uint8_t r, g, b, a;
    if (color_from_json(val, &r, &g, &b, &a)) return (Color){r, g, b, a};
    return fallback;
}

#define COL(key, fb) parse_color(cJSON_GetObjectItemCaseSensitive(args, (key)), (fb))

static int get_int(cJSON *obj, const char *key, int fallback) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return (int)item->valuedouble;
    return fallback;
}

static float get_float(cJSON *obj, const char *key, float fallback) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return (float)item->valuedouble;
    return fallback;
}

static const char *get_string(cJSON *obj, const char *key, const char *fallback) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) return item->valuestring;
    return fallback;
}

// ---------------------------------------------------------------------------
// Vector2 / Rectangle helpers
// ---------------------------------------------------------------------------

static Vector2 get_vec2(cJSON *obj, const char *key, Vector2 fallback) {
    float x, y;
    if (proto_parse_vec2(cJSON_GetObjectItemCaseSensitive(obj, key), &x, &y))
        return (Vector2){x, y};
    return fallback;
}

static Rectangle get_rect(cJSON *obj, const char *key, Rectangle fallback) {
    float x, y, w, h;
    if (proto_parse_rect(cJSON_GetObjectItemCaseSensitive(obj, key), &x, &y, &w, &h))
        return (Rectangle){x, y, w, h};
    return fallback;
}

static Camera2D get_camera2d(cJSON *obj, const char *key) {
    Camera2D cam = { .zoom = 1.0f };
    cJSON *c = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!c) return cam;
    cam.offset   = get_vec2(c, "offset",   (Vector2){0, 0});
    cam.target   = get_vec2(c, "target",   (Vector2){0, 0});
    cam.rotation = get_float(c, "rotation", 0.0f);
    cam.zoom     = get_float(c, "zoom",     1.0f);
    return cam;
}

#define MAX_POINTS 65536
static int parse_points(cJSON *arr, Vector2 **out) {
    if (!cJSON_IsArray(arr)) return -1;
    int n = cJSON_GetArraySize(arr);
    if (n <= 0) { *out = NULL; return 0; }
    if (n > MAX_POINTS) return -1;
    Vector2 *pts = malloc((size_t)n * sizeof(Vector2));
    if (!pts) return -1;
    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        float x, y;
        if (!proto_parse_vec2(item, &x, &y)) { free(pts); return -1; }
        pts[idx++] = (Vector2){x, y};
    }
    *out = pts;
    return n;
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

static void send_handle_response(int conn_fd, const char *id, int handle_id) {
    if (!id || conn_fd < 0) return;
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "handle", handle_id);
    char *resp = protocol_ok(id, result);
    cJSON_Delete(result);
    protocol_send(conn_fd, resp);
    free(resp);
}

static void send_error_response(int conn_fd, const char *id, const char *msg) {
    if (!id || conn_fd < 0) return;
    char *resp = protocol_error(id, msg);
    protocol_send(conn_fd, resp);
    free(resp);
}

// ---------------------------------------------------------------------------
// Handle lookup helpers
// ---------------------------------------------------------------------------

// Returns Texture2D* for HANDLE_TEXTURE, or &rt->texture for HANDLE_RENDER_TEXTURE.
static Texture2D *get_tex_any(int id) {
    if (!g_registry) return NULL;
    Texture2D *t = handle_get(g_registry, id, HANDLE_TEXTURE);
    if (t) return t;
    RenderTexture2D *rt = handle_get(g_registry, id, HANDLE_RENDER_TEXTURE);
    return rt ? &rt->texture : NULL;
}

// FontResource has Font as first member — safe to cast.
static Font *get_font_ptr(int id) {
    if (!g_registry) return NULL;
    return (Font *)handle_get(g_registry, id, HANDLE_FONT);
}

// ---------------------------------------------------------------------------
// Codepoint array helper (shared by LoadFontEx and UploadFont)
// ---------------------------------------------------------------------------

static int *parse_codepoints(cJSON *arr, int *out_count) {
    *out_count = 0;
    if (!cJSON_IsArray(arr)) return NULL;
    int n = cJSON_GetArraySize(arr);
    if (n <= 0 || n > 65536) return NULL;
    int *cp = malloc((size_t)n * sizeof(int));
    if (!cp) return NULL;
    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        cp[idx++] = (int)item->valuedouble;
    }
    *out_count = n;
    return cp;
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void commands_execute(const ParsedCmd *cmd, int conn_fd) {
    const char *name = cmd->cmd;
    cJSON      *args = cmd->args;

    RLS_DEBUG("execute: %s", name);

    // -----------------------------------------------------------------------
    // Window management
    // -----------------------------------------------------------------------

    if (strcmp(name, "InitWindow") == 0) {
        SetWindowSize(get_int(args, "width", 800), get_int(args, "height", 600));
        SetWindowTitle(get_string(args, "title", "raylib_server"));
        return;
    }
    if (strcmp(name, "SetWindowTitle") == 0) {
        SetWindowTitle(get_string(args, "title", "raylib_server"));
        return;
    }
    if (strcmp(name, "SetWindowSize") == 0) {
        SetWindowSize(get_int(args, "width", 800), get_int(args, "height", 600));
        return;
    }
    if (strcmp(name, "SetWindowPosition") == 0) {
        SetWindowPosition(get_int(args, "x", 0), get_int(args, "y", 0));
        return;
    }
    if (strcmp(name, "SetTargetFPS") == 0) {
        SetTargetFPS(get_int(args, "fps", 60));
        return;
    }
    if (strcmp(name, "CloseWindow") == 0) {
        RLS_INFO("CloseWindow requested by client");
        return;
    }

    // -----------------------------------------------------------------------
    // Drawing lifecycle (server controls frames; client calls are no-ops)
    // -----------------------------------------------------------------------

    if (strcmp(name, "BeginDrawing") == 0 ||
        strcmp(name, "EndDrawing")   == 0) {
        return;
    }

    // -----------------------------------------------------------------------
    // Mode stacks
    // -----------------------------------------------------------------------

    if (strcmp(name, "BeginMode2D") == 0) {
        BeginMode2D(get_camera2d(args, "camera"));
        return;
    }
    if (strcmp(name, "EndMode2D") == 0) {
        EndMode2D();
        return;
    }
    if (strcmp(name, "BeginBlendMode") == 0) {
        BeginBlendMode(get_int(args, "mode", BLEND_ALPHA));
        return;
    }
    if (strcmp(name, "EndBlendMode") == 0) {
        EndBlendMode();
        return;
    }
    if (strcmp(name, "BeginScissorMode") == 0) {
        BeginScissorMode(get_int(args, "x", 0), get_int(args, "y", 0),
                         get_int(args, "width", 1), get_int(args, "height", 1));
        return;
    }
    if (strcmp(name, "EndScissorMode") == 0) {
        EndScissorMode();
        return;
    }
    if (strcmp(name, "BeginTextureMode") == 0) {
        int h = get_int(args, "handle", 0);
        RenderTexture2D *rt = g_registry
            ? handle_get(g_registry, h, HANDLE_RENDER_TEXTURE) : NULL;
        if (!rt) { RLS_WARNING("BeginTextureMode: invalid handle %d", h); return; }
        BeginTextureMode(*rt);
        return;
    }
    if (strcmp(name, "EndTextureMode") == 0) {
        EndTextureMode();
        return;
    }
    if (strcmp(name, "BeginShaderMode") == 0) {
        int h = get_int(args, "handle", 0);
        Shader *sh = g_registry
            ? handle_get(g_registry, h, HANDLE_SHADER) : NULL;
        if (!sh) { RLS_WARNING("BeginShaderMode: invalid handle %d", h); return; }
        BeginShaderMode(*sh);
        return;
    }
    if (strcmp(name, "EndShaderMode") == 0) {
        EndShaderMode();
        return;
    }

    // -----------------------------------------------------------------------
    // Background / FPS
    // -----------------------------------------------------------------------

    if (strcmp(name, "ClearBackground") == 0) {
        ClearBackground(COL("color", RAYWHITE));
        return;
    }
    if (strcmp(name, "DrawFPS") == 0) {
        DrawFPS(get_int(args, "posX", 10), get_int(args, "posY", 10));
        return;
    }

    // -----------------------------------------------------------------------
    // Pixels
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawPixel") == 0) {
        DrawPixel(get_int(args, "posX", 0), get_int(args, "posY", 0),
                  COL("color", BLACK));
        return;
    }
    if (strcmp(name, "DrawPixelV") == 0) {
        DrawPixelV(get_vec2(args, "position", (Vector2){0, 0}),
                   COL("color", BLACK));
        return;
    }

    // -----------------------------------------------------------------------
    // Lines
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawLine") == 0) {
        DrawLine(get_int(args, "startPosX", 0), get_int(args, "startPosY", 0),
                 get_int(args, "endPosX",   0), get_int(args, "endPosY",   0),
                 COL("color", BLACK));
        return;
    }
    if (strcmp(name, "DrawLineV") == 0) {
        DrawLineV(get_vec2(args, "startPos", (Vector2){0, 0}),
                  get_vec2(args, "endPos",   (Vector2){0, 0}),
                  COL("color", BLACK));
        return;
    }
    if (strcmp(name, "DrawLineEx") == 0) {
        DrawLineEx(get_vec2(args,  "startPos", (Vector2){0, 0}),
                   get_vec2(args,  "endPos",   (Vector2){0, 0}),
                   get_float(args, "thick",    1.0f),
                   COL("color", BLACK));
        return;
    }
    if (strcmp(name, "DrawLineStrip") == 0) {
        Vector2 *pts = NULL;
        int n = parse_points(cJSON_GetObjectItemCaseSensitive(args, "points"), &pts);
        if (n >= 2) DrawLineStrip(pts, n, COL("color", BLACK));
        free(pts);
        return;
    }
    if (strcmp(name, "DrawLineBezier") == 0) {
        DrawLineBezier(get_vec2(args,  "startPos", (Vector2){0, 0}),
                       get_vec2(args,  "endPos",   (Vector2){0, 0}),
                       get_float(args, "thick",    1.0f),
                       COL("color", BLACK));
        return;
    }

    // -----------------------------------------------------------------------
    // Circles
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawCircle") == 0) {
        DrawCircle(get_int(args, "centerX", 0), get_int(args, "centerY", 0),
                   get_float(args, "radius", 10.0f), COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawCircleV") == 0) {
        DrawCircleV(get_vec2(args, "center", (Vector2){0, 0}),
                    get_float(args, "radius", 10.0f), COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawCircleGradient") == 0) {
        DrawCircleGradient(get_int(args,   "centerX", 0), get_int(args, "centerY", 0),
                           get_float(args, "radius",  10.0f),
                           COL("inner", WHITE), COL("outer", BLACK));
        return;
    }
    if (strcmp(name, "DrawCircleSector") == 0) {
        DrawCircleSector(get_vec2(args,  "center",     (Vector2){0, 0}),
                         get_float(args, "radius",     10.0f),
                         get_float(args, "startAngle", 0.0f),
                         get_float(args, "endAngle",   90.0f),
                         get_int(args,   "segments",   16),
                         COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawCircleSectorLines") == 0) {
        DrawCircleSectorLines(get_vec2(args,  "center",     (Vector2){0, 0}),
                              get_float(args, "radius",     10.0f),
                              get_float(args, "startAngle", 0.0f),
                              get_float(args, "endAngle",   90.0f),
                              get_int(args,   "segments",   16),
                              COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawCircleLines") == 0) {
        DrawCircleLines(get_int(args, "centerX", 0), get_int(args, "centerY", 0),
                        get_float(args, "radius", 10.0f), COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawCircleLinesV") == 0) {
        DrawCircleLinesV(get_vec2(args, "center", (Vector2){0, 0}),
                         get_float(args, "radius", 10.0f), COL("color", RED));
        return;
    }

    // -----------------------------------------------------------------------
    // Ellipses
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawEllipse") == 0) {
        DrawEllipse(get_int(args,   "centerX", 0), get_int(args, "centerY", 0),
                    get_float(args, "radiusH", 10.0f),
                    get_float(args, "radiusV", 10.0f),
                    COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawEllipseLines") == 0) {
        DrawEllipseLines(get_int(args,   "centerX", 0), get_int(args, "centerY", 0),
                         get_float(args, "radiusH", 10.0f),
                         get_float(args, "radiusV", 10.0f),
                         COL("color", RED));
        return;
    }

    // -----------------------------------------------------------------------
    // Rings
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawRing") == 0) {
        DrawRing(get_vec2(args,  "center",      (Vector2){0, 0}),
                 get_float(args, "innerRadius", 5.0f),
                 get_float(args, "outerRadius", 10.0f),
                 get_float(args, "startAngle",  0.0f),
                 get_float(args, "endAngle",    360.0f),
                 get_int(args,   "segments",    32),
                 COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawRingLines") == 0) {
        DrawRingLines(get_vec2(args,  "center",      (Vector2){0, 0}),
                      get_float(args, "innerRadius", 5.0f),
                      get_float(args, "outerRadius", 10.0f),
                      get_float(args, "startAngle",  0.0f),
                      get_float(args, "endAngle",    360.0f),
                      get_int(args,   "segments",    32),
                      COL("color", RED));
        return;
    }

    // -----------------------------------------------------------------------
    // Rectangles
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawRectangle") == 0) {
        DrawRectangle(get_int(args, "posX", 0), get_int(args, "posY", 0),
                      get_int(args, "width", 10), get_int(args, "height", 10),
                      COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawRectangleV") == 0) {
        DrawRectangleV(get_vec2(args, "position", (Vector2){0, 0}),
                       get_vec2(args, "size",     (Vector2){10, 10}),
                       COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawRectangleRec") == 0) {
        DrawRectangleRec(get_rect(args, "rec", (Rectangle){0, 0, 10, 10}),
                         COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawRectanglePro") == 0) {
        DrawRectanglePro(get_rect(args,  "rec",      (Rectangle){0, 0, 10, 10}),
                         get_vec2(args,  "origin",   (Vector2){0, 0}),
                         get_float(args, "rotation", 0.0f),
                         COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawRectangleGradientV") == 0) {
        DrawRectangleGradientV(get_int(args, "posX", 0), get_int(args, "posY", 0),
                               get_int(args, "width", 10), get_int(args, "height", 10),
                               COL("top", WHITE), COL("bottom", BLACK));
        return;
    }
    if (strcmp(name, "DrawRectangleGradientH") == 0) {
        DrawRectangleGradientH(get_int(args, "posX", 0), get_int(args, "posY", 0),
                               get_int(args, "width", 10), get_int(args, "height", 10),
                               COL("left", WHITE), COL("right", BLACK));
        return;
    }
    if (strcmp(name, "DrawRectangleGradientEx") == 0) {
        DrawRectangleGradientEx(get_rect(args, "rec", (Rectangle){0, 0, 10, 10}),
                                COL("topLeft",     WHITE), COL("bottomLeft",  BLACK),
                                COL("bottomRight", BLACK), COL("topRight",    WHITE));
        return;
    }
    if (strcmp(name, "DrawRectangleLines") == 0) {
        DrawRectangleLines(get_int(args, "posX", 0), get_int(args, "posY", 0),
                           get_int(args, "width", 10), get_int(args, "height", 10),
                           COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawRectangleLinesEx") == 0) {
        DrawRectangleLinesEx(get_rect(args,  "rec",       (Rectangle){0, 0, 10, 10}),
                             get_float(args, "lineThick", 1.0f),
                             COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawRectangleRounded") == 0) {
        DrawRectangleRounded(get_rect(args,  "rec",       (Rectangle){0, 0, 10, 10}),
                             get_float(args, "roundness", 0.2f),
                             get_int(args,   "segments",  8),
                             COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawRectangleRoundedLines") == 0 ||
        strcmp(name, "DrawRectangleRoundedLinesEx") == 0) {
        DrawRectangleRoundedLines(get_rect(args,  "rec",       (Rectangle){0, 0, 10, 10}),
                                  get_float(args, "roundness", 0.2f),
                                  get_int(args,   "segments",  8),
                                  get_float(args, "lineThick", 1.0f),
                                  COL("color", RED));
        return;
    }

    // -----------------------------------------------------------------------
    // Triangles
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawTriangle") == 0) {
        DrawTriangle(get_vec2(args, "v1", (Vector2){0, 0}),
                     get_vec2(args, "v2", (Vector2){0, 0}),
                     get_vec2(args, "v3", (Vector2){0, 0}),
                     COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawTriangleLines") == 0) {
        DrawTriangleLines(get_vec2(args, "v1", (Vector2){0, 0}),
                          get_vec2(args, "v2", (Vector2){0, 0}),
                          get_vec2(args, "v3", (Vector2){0, 0}),
                          COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawTriangleFan") == 0) {
        Vector2 *pts = NULL;
        int n = parse_points(cJSON_GetObjectItemCaseSensitive(args, "points"), &pts);
        if (n >= 3) DrawTriangleFan(pts, n, COL("color", RED));
        free(pts);
        return;
    }
    if (strcmp(name, "DrawTriangleStrip") == 0) {
        Vector2 *pts = NULL;
        int n = parse_points(cJSON_GetObjectItemCaseSensitive(args, "points"), &pts);
        if (n >= 3) DrawTriangleStrip(pts, n, COL("color", RED));
        free(pts);
        return;
    }

    // -----------------------------------------------------------------------
    // Polygons
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawPoly") == 0) {
        DrawPoly(get_vec2(args,  "center",   (Vector2){0, 0}),
                 get_int(args,   "sides",    6),
                 get_float(args, "radius",   20.0f),
                 get_float(args, "rotation", 0.0f),
                 COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawPolyLines") == 0) {
        DrawPolyLines(get_vec2(args,  "center",   (Vector2){0, 0}),
                      get_int(args,   "sides",    6),
                      get_float(args, "radius",   20.0f),
                      get_float(args, "rotation", 0.0f),
                      COL("color", RED));
        return;
    }
    if (strcmp(name, "DrawPolyLinesEx") == 0) {
        DrawPolyLinesEx(get_vec2(args,  "center",    (Vector2){0, 0}),
                        get_int(args,   "sides",     6),
                        get_float(args, "radius",    20.0f),
                        get_float(args, "rotation",  0.0f),
                        get_float(args, "lineThick", 1.0f),
                        COL("color", RED));
        return;
    }

    // -----------------------------------------------------------------------
    // Splines
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawSplineLinear") == 0) {
        Vector2 *pts = NULL;
        int n = parse_points(cJSON_GetObjectItemCaseSensitive(args, "points"), &pts);
        if (n >= 2)
            DrawSplineLinear(pts, n, get_float(args, "thick", 1.0f), COL("color", RED));
        free(pts);
        return;
    }
    if (strcmp(name, "DrawSplineBasis") == 0) {
        Vector2 *pts = NULL;
        int n = parse_points(cJSON_GetObjectItemCaseSensitive(args, "points"), &pts);
        if (n >= 4)
            DrawSplineBasis(pts, n, get_float(args, "thick", 1.0f), COL("color", RED));
        free(pts);
        return;
    }
    if (strcmp(name, "DrawSplineCatmullRom") == 0) {
        Vector2 *pts = NULL;
        int n = parse_points(cJSON_GetObjectItemCaseSensitive(args, "points"), &pts);
        if (n >= 4)
            DrawSplineCatmullRom(pts, n, get_float(args, "thick", 1.0f), COL("color", RED));
        free(pts);
        return;
    }
    if (strcmp(name, "DrawSplineBezierQuadratic") == 0) {
        Vector2 *pts = NULL;
        int n = parse_points(cJSON_GetObjectItemCaseSensitive(args, "points"), &pts);
        if (n >= 3)
            DrawSplineBezierQuadratic(pts, n, get_float(args, "thick", 1.0f), COL("color", RED));
        free(pts);
        return;
    }
    if (strcmp(name, "DrawSplineBezierCubic") == 0) {
        Vector2 *pts = NULL;
        int n = parse_points(cJSON_GetObjectItemCaseSensitive(args, "points"), &pts);
        if (n >= 4)
            DrawSplineBezierCubic(pts, n, get_float(args, "thick", 1.0f), COL("color", RED));
        free(pts);
        return;
    }

    // -----------------------------------------------------------------------
    // Text drawing (default font)
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawText") == 0) {
        DrawText(get_string(args, "text", ""),
                 get_int(args, "posX", 0), get_int(args, "posY", 0),
                 get_int(args, "fontSize", 20),
                 COL("color", BLACK));
        return;
    }

    // -----------------------------------------------------------------------
    // Text drawing (custom font)
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawTextEx") == 0) {
        Font *fp = get_font_ptr(get_int(args, "font", 0));
        Font  font = fp ? *fp : GetFontDefault();
        DrawTextEx(font,
                   get_string(args, "text", ""),
                   get_vec2(args,   "position", (Vector2){0, 0}),
                   get_float(args,  "fontSize",  10.0f),
                   get_float(args,  "spacing",    1.0f),
                   COL("tint", WHITE));
        return;
    }
    if (strcmp(name, "DrawTextPro") == 0) {
        Font *fp = get_font_ptr(get_int(args, "font", 0));
        Font  font = fp ? *fp : GetFontDefault();
        DrawTextPro(font,
                    get_string(args, "text", ""),
                    get_vec2(args,   "position", (Vector2){0, 0}),
                    get_vec2(args,   "origin",   (Vector2){0, 0}),
                    get_float(args,  "rotation",  0.0f),
                    get_float(args,  "fontSize",  10.0f),
                    get_float(args,  "spacing",    1.0f),
                    COL("tint", WHITE));
        return;
    }
    if (strcmp(name, "DrawTextCodepoint") == 0) {
        Font *fp = get_font_ptr(get_int(args, "font", 0));
        Font  font = fp ? *fp : GetFontDefault();
        DrawTextCodepoint(font,
                          get_int(args,   "codepoint", 63),
                          get_vec2(args,  "position",  (Vector2){0, 0}),
                          get_float(args, "fontSize",   10.0f),
                          COL("tint", WHITE));
        return;
    }

    // MeasureText — sync: sends result on conn_fd
    if (strcmp(name, "MeasureText") == 0) {
        int width = MeasureText(get_string(args, "text", ""),
                                get_int(args, "fontSize", 10));
        if (cmd->id && conn_fd >= 0) {
            cJSON *result = cJSON_CreateObject();
            cJSON_AddNumberToObject(result, "width", width);
            char *resp = protocol_ok(cmd->id, result);
            cJSON_Delete(result);
            protocol_send(conn_fd, resp);
            free(resp);
        }
        return;
    }
    // MeasureTextEx — sync
    if (strcmp(name, "MeasureTextEx") == 0) {
        Font *fp = get_font_ptr(get_int(args, "font", 0));
        Font  font = fp ? *fp : GetFontDefault();
        Vector2 sz = MeasureTextEx(font,
                                   get_string(args, "text",     ""),
                                   get_float(args,  "fontSize", 10.0f),
                                   get_float(args,  "spacing",   1.0f));
        if (cmd->id && conn_fd >= 0) {
            cJSON *result = cJSON_CreateObject();
            cJSON_AddNumberToObject(result, "x", sz.x);
            cJSON_AddNumberToObject(result, "y", sz.y);
            char *resp = protocol_ok(cmd->id, result);
            cJSON_Delete(result);
            protocol_send(conn_fd, resp);
            free(resp);
        }
        return;
    }

    // -----------------------------------------------------------------------
    // Texture drawing
    // -----------------------------------------------------------------------

    if (strcmp(name, "DrawTexture") == 0) {
        Texture2D *tex = get_tex_any(get_int(args, "handle", 0));
        if (!tex) { RLS_WARNING("DrawTexture: invalid handle"); return; }
        DrawTexture(*tex,
                    get_int(args, "posX", 0),
                    get_int(args, "posY", 0),
                    COL("tint", WHITE));
        return;
    }
    if (strcmp(name, "DrawTextureV") == 0) {
        Texture2D *tex = get_tex_any(get_int(args, "handle", 0));
        if (!tex) { RLS_WARNING("DrawTextureV: invalid handle"); return; }
        DrawTextureV(*tex,
                     get_vec2(args, "position", (Vector2){0, 0}),
                     COL("tint", WHITE));
        return;
    }
    if (strcmp(name, "DrawTextureEx") == 0) {
        Texture2D *tex = get_tex_any(get_int(args, "handle", 0));
        if (!tex) { RLS_WARNING("DrawTextureEx: invalid handle"); return; }
        DrawTextureEx(*tex,
                      get_vec2(args,  "position", (Vector2){0, 0}),
                      get_float(args, "rotation", 0.0f),
                      get_float(args, "scale",    1.0f),
                      COL("tint", WHITE));
        return;
    }
    if (strcmp(name, "DrawTextureRec") == 0) {
        Texture2D *tex = get_tex_any(get_int(args, "handle", 0));
        if (!tex) { RLS_WARNING("DrawTextureRec: invalid handle"); return; }
        DrawTextureRec(*tex,
                       get_rect(args,  "source",   (Rectangle){0, 0, 0, 0}),
                       get_vec2(args,  "position", (Vector2){0, 0}),
                       COL("tint", WHITE));
        return;
    }
    if (strcmp(name, "DrawTexturePro") == 0) {
        Texture2D *tex = get_tex_any(get_int(args, "handle", 0));
        if (!tex) { RLS_WARNING("DrawTexturePro: invalid handle"); return; }
        DrawTexturePro(*tex,
                       get_rect(args,  "source",   (Rectangle){0, 0, 0, 0}),
                       get_rect(args,  "dest",     (Rectangle){0, 0, 0, 0}),
                       get_vec2(args,  "origin",   (Vector2){0, 0}),
                       get_float(args, "rotation", 0.0f),
                       COL("tint", WHITE));
        return;
    }
    if (strcmp(name, "DrawTextureNPatch") == 0) {
        Texture2D *tex = get_tex_any(get_int(args, "handle", 0));
        if (!tex) { RLS_WARNING("DrawTextureNPatch: invalid handle"); return; }
        NPatchInfo np = {0};
        cJSON *npj = cJSON_GetObjectItemCaseSensitive(args, "nPatch");
        if (npj) {
            float rx, ry, rw, rh;
            if (proto_parse_rect(cJSON_GetObjectItemCaseSensitive(npj, "source"),
                                 &rx, &ry, &rw, &rh))
                np.source = (Rectangle){rx, ry, rw, rh};
            np.left   = get_int(npj, "left",   0);
            np.top    = get_int(npj, "top",    0);
            np.right  = get_int(npj, "right",  0);
            np.bottom = get_int(npj, "bottom", 0);
            np.layout = get_int(npj, "layout", NPATCH_NINE_PATCH);
        }
        DrawTextureNPatch(*tex, np,
                          get_rect(args,  "dest",     (Rectangle){0, 0, 0, 0}),
                          get_vec2(args,  "origin",   (Vector2){0, 0}),
                          get_float(args, "rotation", 0.0f),
                          COL("tint", WHITE));
        return;
    }
    if (strcmp(name, "SetTextureFilter") == 0) {
        Texture2D *tex = get_tex_any(get_int(args, "handle", 0));
        if (!tex) { RLS_WARNING("SetTextureFilter: invalid handle"); return; }
        SetTextureFilter(*tex, get_int(args, "filter", TEXTURE_FILTER_BILINEAR));
        return;
    }
    if (strcmp(name, "SetTextureWrap") == 0) {
        Texture2D *tex = get_tex_any(get_int(args, "handle", 0));
        if (!tex) { RLS_WARNING("SetTextureWrap: invalid handle"); return; }
        SetTextureWrap(*tex, get_int(args, "wrap", TEXTURE_WRAP_CLAMP));
        return;
    }

    // -----------------------------------------------------------------------
    // Shader uniforms
    // -----------------------------------------------------------------------

    if (strcmp(name, "SetShaderValue") == 0) {
        if (!g_registry) return;
        Shader *sh = handle_get(g_registry, get_int(args, "handle", 0), HANDLE_SHADER);
        if (!sh) { RLS_WARNING("SetShaderValue: invalid handle"); return; }
        const char *loc_name = get_string(args, "locName", NULL);
        if (!loc_name) return;
        int loc  = GetShaderLocation(*sh, loc_name);
        int type = get_int(args, "type", SHADER_UNIFORM_FLOAT);
        cJSON *val = cJSON_GetObjectItemCaseSensitive(args, "value");
        if (!val) return;
        if (type >= SHADER_UNIFORM_INT) {
            int iv[4] = {0};
            if (cJSON_IsArray(val)) {
                int n = cJSON_GetArraySize(val);
                for (int i = 0; i < n && i < 4; i++)
                    iv[i] = (int)cJSON_GetArrayItem(val, i)->valuedouble;
            } else {
                iv[0] = (int)val->valuedouble;
            }
            SetShaderValue(*sh, loc, iv, type);
        } else {
            float fv[4] = {0};
            if (cJSON_IsArray(val)) {
                int n = cJSON_GetArraySize(val);
                for (int i = 0; i < n && i < 4; i++)
                    fv[i] = (float)cJSON_GetArrayItem(val, i)->valuedouble;
            } else {
                fv[0] = (float)val->valuedouble;
            }
            SetShaderValue(*sh, loc, fv, type);
        }
        return;
    }
    if (strcmp(name, "SetShaderValueV") == 0) {
        if (!g_registry) return;
        Shader *sh = handle_get(g_registry, get_int(args, "handle", 0), HANDLE_SHADER);
        if (!sh) { RLS_WARNING("SetShaderValueV: invalid handle"); return; }
        const char *loc_name = get_string(args, "locName", NULL);
        if (!loc_name) return;
        int   loc    = GetShaderLocation(*sh, loc_name);
        int   type   = get_int(args, "type",  SHADER_UNIFORM_FLOAT);
        int   count  = get_int(args, "count", 1);
        cJSON *vals  = cJSON_GetObjectItemCaseSensitive(args, "values");
        if (!cJSON_IsArray(vals) || count <= 0) return;
        int n = cJSON_GetArraySize(vals);
        if (n <= 0) return;
        if (type >= SHADER_UNIFORM_INT) {
            int *buf = calloc((size_t)n, sizeof(int));
            if (!buf) return;
            int i = 0;
            cJSON *item;
            cJSON_ArrayForEach(item, vals) buf[i++] = (int)item->valuedouble;
            SetShaderValueV(*sh, loc, buf, type, count);
            free(buf);
        } else {
            float *buf = calloc((size_t)n, sizeof(float));
            if (!buf) return;
            int i = 0;
            cJSON *item;
            cJSON_ArrayForEach(item, vals) buf[i++] = (float)item->valuedouble;
            SetShaderValueV(*sh, loc, buf, type, count);
            free(buf);
        }
        return;
    }
    if (strcmp(name, "SetShaderValueMatrix") == 0) {
        if (!g_registry) return;
        Shader *sh = handle_get(g_registry, get_int(args, "handle", 0), HANDLE_SHADER);
        if (!sh) { RLS_WARNING("SetShaderValueMatrix: invalid handle"); return; }
        const char *loc_name = get_string(args, "locName", NULL);
        if (!loc_name) return;
        int   loc = GetShaderLocation(*sh, loc_name);
        cJSON *mat = cJSON_GetObjectItemCaseSensitive(args, "mat");
        Matrix m = {0};
        if (cJSON_IsArray(mat) && cJSON_GetArraySize(mat) >= 16) {
            float *mp = (float *)&m;
            for (int i = 0; i < 16; i++)
                mp[i] = (float)cJSON_GetArrayItem(mat, i)->valuedouble;
        }
        SetShaderValueMatrix(*sh, loc, m);
        return;
    }
    if (strcmp(name, "SetShaderValueTexture") == 0) {
        if (!g_registry) return;
        Shader    *sh  = handle_get(g_registry, get_int(args, "handle",    0), HANDLE_SHADER);
        Texture2D *tex = get_tex_any(get_int(args, "texHandle", 0));
        if (!sh)  { RLS_WARNING("SetShaderValueTexture: invalid shader handle");  return; }
        if (!tex) { RLS_WARNING("SetShaderValueTexture: invalid texture handle"); return; }
        const char *loc_name = get_string(args, "locName", NULL);
        if (!loc_name) return;
        int loc = GetShaderLocation(*sh, loc_name);
        SetShaderValueTexture(*sh, loc, *tex);
        return;
    }

    // -----------------------------------------------------------------------
    // Audio control (fire-and-forget)
    // -----------------------------------------------------------------------

    if (strcmp(name, "InitAudioDevice") == 0) {
        InitAudioDevice();
        return;
    }
    if (strcmp(name, "CloseAudioDevice") == 0) {
        CloseAudioDevice();
        return;
    }
    if (strcmp(name, "PlaySound") == 0) {
        Sound *snd = g_registry
            ? handle_get(g_registry, get_int(args, "handle", 0), HANDLE_SOUND) : NULL;
        if (!snd) { RLS_WARNING("PlaySound: invalid handle"); return; }
        PlaySound(*snd);
        return;
    }
    if (strcmp(name, "StopSound") == 0) {
        Sound *snd = g_registry
            ? handle_get(g_registry, get_int(args, "handle", 0), HANDLE_SOUND) : NULL;
        if (!snd) { RLS_WARNING("StopSound: invalid handle"); return; }
        StopSound(*snd);
        return;
    }
    if (strcmp(name, "PauseSound") == 0) {
        Sound *snd = g_registry
            ? handle_get(g_registry, get_int(args, "handle", 0), HANDLE_SOUND) : NULL;
        if (!snd) { RLS_WARNING("PauseSound: invalid handle"); return; }
        PauseSound(*snd);
        return;
    }
    if (strcmp(name, "ResumeSound") == 0) {
        Sound *snd = g_registry
            ? handle_get(g_registry, get_int(args, "handle", 0), HANDLE_SOUND) : NULL;
        if (!snd) { RLS_WARNING("ResumeSound: invalid handle"); return; }
        ResumeSound(*snd);
        return;
    }
    if (strcmp(name, "SetSoundVolume") == 0) {
        Sound *snd = g_registry
            ? handle_get(g_registry, get_int(args, "handle", 0), HANDLE_SOUND) : NULL;
        if (!snd) { RLS_WARNING("SetSoundVolume: invalid handle"); return; }
        SetSoundVolume(*snd, get_float(args, "volume", 1.0f));
        return;
    }
    if (strcmp(name, "SetSoundPitch") == 0) {
        Sound *snd = g_registry
            ? handle_get(g_registry, get_int(args, "handle", 0), HANDLE_SOUND) : NULL;
        if (!snd) { RLS_WARNING("SetSoundPitch: invalid handle"); return; }
        SetSoundPitch(*snd, get_float(args, "pitch", 1.0f));
        return;
    }
    if (strcmp(name, "PlayMusicStream") == 0) {
        Music *m = g_registry
            ? (Music *)handle_get(g_registry, get_int(args, "handle", 0), HANDLE_MUSIC) : NULL;
        if (!m) { RLS_WARNING("PlayMusicStream: invalid handle"); return; }
        PlayMusicStream(*m);
        return;
    }
    if (strcmp(name, "StopMusicStream") == 0) {
        Music *m = g_registry
            ? (Music *)handle_get(g_registry, get_int(args, "handle", 0), HANDLE_MUSIC) : NULL;
        if (!m) { RLS_WARNING("StopMusicStream: invalid handle"); return; }
        StopMusicStream(*m);
        return;
    }
    if (strcmp(name, "PauseMusicStream") == 0) {
        Music *m = g_registry
            ? (Music *)handle_get(g_registry, get_int(args, "handle", 0), HANDLE_MUSIC) : NULL;
        if (!m) { RLS_WARNING("PauseMusicStream: invalid handle"); return; }
        PauseMusicStream(*m);
        return;
    }
    if (strcmp(name, "ResumeMusicStream") == 0) {
        Music *m = g_registry
            ? (Music *)handle_get(g_registry, get_int(args, "handle", 0), HANDLE_MUSIC) : NULL;
        if (!m) { RLS_WARNING("ResumeMusicStream: invalid handle"); return; }
        ResumeMusicStream(*m);
        return;
    }
    if (strcmp(name, "SetMusicVolume") == 0) {
        Music *m = g_registry
            ? (Music *)handle_get(g_registry, get_int(args, "handle", 0), HANDLE_MUSIC) : NULL;
        if (!m) { RLS_WARNING("SetMusicVolume: invalid handle"); return; }
        SetMusicVolume(*m, get_float(args, "volume", 1.0f));
        return;
    }
    if (strcmp(name, "SetMusicPitch") == 0) {
        Music *m = g_registry
            ? (Music *)handle_get(g_registry, get_int(args, "handle", 0), HANDLE_MUSIC) : NULL;
        if (!m) { RLS_WARNING("SetMusicPitch: invalid handle"); return; }
        SetMusicPitch(*m, get_float(args, "pitch", 1.0f));
        return;
    }
    if (strcmp(name, "SeekMusicStream") == 0) {
        Music *m = g_registry
            ? (Music *)handle_get(g_registry, get_int(args, "handle", 0), HANDLE_MUSIC) : NULL;
        if (!m) { RLS_WARNING("SeekMusicStream: invalid handle"); return; }
        SeekMusicStream(*m, get_float(args, "position", 0.0f));
        return;
    }

    // -----------------------------------------------------------------------
    // Resource unloading (fire-and-forget)
    // -----------------------------------------------------------------------

    if (strcmp(name, "UnloadTexture") == 0) {
        if (!g_registry) return;
        int h = get_int(args, "handle", 0);
        Texture2D *tex = handle_get(g_registry, h, HANDLE_TEXTURE);
        if (!tex) { RLS_WARNING("UnloadTexture: invalid handle %d", h); return; }
        UnloadTexture(*tex);
        free(tex);
        handle_free(g_registry, h);
        return;
    }
    if (strcmp(name, "UnloadRenderTexture") == 0) {
        if (!g_registry) return;
        int h = get_int(args, "handle", 0);
        RenderTexture2D *rt = handle_get(g_registry, h, HANDLE_RENDER_TEXTURE);
        if (!rt) { RLS_WARNING("UnloadRenderTexture: invalid handle %d", h); return; }
        UnloadRenderTexture(*rt);
        free(rt);
        handle_free(g_registry, h);
        return;
    }
    if (strcmp(name, "UnloadFont") == 0) {
        if (!g_registry) return;
        int h = get_int(args, "handle", 0);
        FontResource *fr = handle_get(g_registry, h, HANDLE_FONT);
        if (!fr) { RLS_WARNING("UnloadFont: invalid handle %d", h); return; }
        UnloadFont(fr->font);
        free(fr->data);
        free(fr);
        handle_free(g_registry, h);
        return;
    }
    if (strcmp(name, "UnloadSound") == 0) {
        if (!g_registry) return;
        int h = get_int(args, "handle", 0);
        Sound *snd = handle_get(g_registry, h, HANDLE_SOUND);
        if (!snd) { RLS_WARNING("UnloadSound: invalid handle %d", h); return; }
        UnloadSound(*snd);
        free(snd);
        handle_free(g_registry, h);
        return;
    }
    if (strcmp(name, "UnloadMusicStream") == 0) {
        if (!g_registry) return;
        int h = get_int(args, "handle", 0);
        MusicResource *mr = handle_get(g_registry, h, HANDLE_MUSIC);
        if (!mr) { RLS_WARNING("UnloadMusicStream: invalid handle %d", h); return; }
        UnloadMusicStream(mr->music);
        free(mr->data);
        free(mr);
        handle_free(g_registry, h);
        return;
    }
    if (strcmp(name, "UnloadShader") == 0) {
        if (!g_registry) return;
        int h = get_int(args, "handle", 0);
        Shader *sh = handle_get(g_registry, h, HANDLE_SHADER);
        if (!sh) { RLS_WARNING("UnloadShader: invalid handle %d", h); return; }
        UnloadShader(*sh);
        free(sh);
        handle_free(g_registry, h);
        return;
    }

    // -----------------------------------------------------------------------
    // Resource loading — sync commands (send result on conn_fd)
    // -----------------------------------------------------------------------

    if (strcmp(name, "LoadTexture") == 0) {
        const char *path = get_string(args, "path", NULL);
        if (!path || !FileExists(path)) {
            send_error_response(conn_fd, cmd->id, "file not found"); return;
        }
        Texture2D *tex = malloc(sizeof(Texture2D));
        if (!tex) { send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        *tex = LoadTexture(path);
        if (tex->id == 0) {
            free(tex);
            send_error_response(conn_fd, cmd->id, "LoadTexture failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_TEXTURE, tex) : 0;
        if (new_h == 0) {
            UnloadTexture(*tex); free(tex);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "LoadRenderTexture") == 0) {
        int w  = get_int(args, "width",  256);
        int ht = get_int(args, "height", 256);
        RenderTexture2D *rt = malloc(sizeof(RenderTexture2D));
        if (!rt) { send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        *rt = LoadRenderTexture(w, ht);
        if (rt->id == 0) {
            free(rt);
            send_error_response(conn_fd, cmd->id, "LoadRenderTexture failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_RENDER_TEXTURE, rt) : 0;
        if (new_h == 0) {
            UnloadRenderTexture(*rt); free(rt);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "LoadFont") == 0) {
        const char *path = get_string(args, "path", NULL);
        if (!path || !FileExists(path)) {
            send_error_response(conn_fd, cmd->id, "file not found"); return;
        }
        FontResource *fr = calloc(1, sizeof(FontResource));
        if (!fr) { send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        fr->font = LoadFont(path);
        if (fr->font.texture.id == 0) {
            free(fr);
            send_error_response(conn_fd, cmd->id, "LoadFont failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_FONT, fr) : 0;
        if (new_h == 0) {
            UnloadFont(fr->font); free(fr);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "LoadFontEx") == 0) {
        const char *path = get_string(args, "path", NULL);
        if (!path || !FileExists(path)) {
            send_error_response(conn_fd, cmd->id, "file not found"); return;
        }
        int  font_size = get_int(args, "fontSize", 32);
        int  cp_count  = 0;
        int *codepoints = parse_codepoints(
            cJSON_GetObjectItemCaseSensitive(args, "codepoints"), &cp_count);
        FontResource *fr = calloc(1, sizeof(FontResource));
        if (!fr) { free(codepoints); send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        fr->font = LoadFontEx(path, font_size, codepoints, cp_count);
        free(codepoints);
        if (fr->font.texture.id == 0) {
            free(fr);
            send_error_response(conn_fd, cmd->id, "LoadFontEx failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_FONT, fr) : 0;
        if (new_h == 0) {
            UnloadFont(fr->font); free(fr);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "LoadSound") == 0) {
        const char *path = get_string(args, "path", NULL);
        if (!path || !FileExists(path)) {
            send_error_response(conn_fd, cmd->id, "file not found"); return;
        }
        Sound *snd = malloc(sizeof(Sound));
        if (!snd) { send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        *snd = LoadSound(path);
        if (!snd->stream.buffer) {
            free(snd);
            send_error_response(conn_fd, cmd->id, "LoadSound failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_SOUND, snd) : 0;
        if (new_h == 0) {
            UnloadSound(*snd); free(snd);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "LoadMusicStream") == 0) {
        const char *path = get_string(args, "path", NULL);
        if (!path || !FileExists(path)) {
            send_error_response(conn_fd, cmd->id, "file not found"); return;
        }
        MusicResource *mr = calloc(1, sizeof(MusicResource));
        if (!mr) { send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        mr->music = LoadMusicStream(path);
        if (!mr->music.stream.buffer) {
            free(mr);
            send_error_response(conn_fd, cmd->id, "LoadMusicStream failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_MUSIC, mr) : 0;
        if (new_h == 0) {
            UnloadMusicStream(mr->music); free(mr);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "LoadShader") == 0) {
        const char *vs = get_string(args, "vsPath", NULL);
        const char *fs = get_string(args, "fsPath", NULL);
        Shader *sh = malloc(sizeof(Shader));
        if (!sh) { send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        *sh = LoadShader(vs, fs);
        if (sh->id == 0) {
            free(sh);
            send_error_response(conn_fd, cmd->id, "LoadShader failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_SHADER, sh) : 0;
        if (new_h == 0) {
            UnloadShader(*sh); free(sh);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }

    // -----------------------------------------------------------------------
    // Inline upload — sync commands (send result on conn_fd)
    // -----------------------------------------------------------------------

    if (strcmp(name, "UploadTexture") == 0) {
        const char *ft  = get_string(args, "fileType", NULL);
        const char *b64 = get_string(args, "data",     NULL);
        if (!ft || !b64) {
            send_error_response(conn_fd, cmd->id, "missing fileType or data"); return;
        }
        size_t        data_len;
        unsigned char *data = b64_decode(b64, strlen(b64), &data_len);
        if (!data) { send_error_response(conn_fd, cmd->id, "base64 decode failed"); return; }
        Image img = LoadImageFromMemory(ft, data, (int)data_len);
        free(data);
        if (!img.data) { send_error_response(conn_fd, cmd->id, "image decode failed"); return; }
        Texture2D *tex = malloc(sizeof(Texture2D));
        if (!tex) { UnloadImage(img); send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        *tex = LoadTextureFromImage(img);
        UnloadImage(img);
        if (tex->id == 0) {
            free(tex);
            send_error_response(conn_fd, cmd->id, "texture upload failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_TEXTURE, tex) : 0;
        if (new_h == 0) {
            UnloadTexture(*tex); free(tex);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "UploadTextureRaw") == 0) {
        int         w    = get_int(args,    "width",  0);
        int         ht   = get_int(args,    "height", 0);
        int         fmt  = get_int(args,    "format", PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        const char *b64  = get_string(args, "data",   NULL);
        if (w <= 0 || ht <= 0 || !b64) {
            send_error_response(conn_fd, cmd->id, "invalid args"); return;
        }
        size_t        data_len;
        unsigned char *data = b64_decode(b64, strlen(b64), &data_len);
        if (!data) { send_error_response(conn_fd, cmd->id, "base64 decode failed"); return; }
        Image img = { .data = data, .width = w, .height = ht,
                      .format = fmt, .mipmaps = 1 };
        Texture2D *tex = malloc(sizeof(Texture2D));
        if (!tex) { free(data); send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        *tex = LoadTextureFromImage(img);
        free(data);
        if (tex->id == 0) {
            free(tex);
            send_error_response(conn_fd, cmd->id, "texture upload failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_TEXTURE, tex) : 0;
        if (new_h == 0) {
            UnloadTexture(*tex); free(tex);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "UploadFont") == 0) {
        const char *ft  = get_string(args, "fileType", NULL);
        const char *b64 = get_string(args, "data",     NULL);
        if (!ft || !b64) {
            send_error_response(conn_fd, cmd->id, "missing fileType or data"); return;
        }
        size_t        data_len;
        unsigned char *data = b64_decode(b64, strlen(b64), &data_len);
        if (!data) { send_error_response(conn_fd, cmd->id, "base64 decode failed"); return; }
        int  font_size  = get_int(args, "fontSize", 32);
        int  cp_count   = 0;
        int *codepoints = parse_codepoints(
            cJSON_GetObjectItemCaseSensitive(args, "codepoints"), &cp_count);
        FontResource *fr = calloc(1, sizeof(FontResource));
        if (!fr) { free(data); free(codepoints); send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        fr->font = LoadFontFromMemory(ft, data, (int)data_len, font_size,
                                      codepoints, cp_count);
        free(codepoints);
        if (fr->font.texture.id == 0) {
            free(data); free(fr);
            send_error_response(conn_fd, cmd->id, "font load failed");
            return;
        }
        // Keep data alive; stb_truetype may reference it for glyph queries.
        fr->data     = data;
        fr->data_len = data_len;
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_FONT, fr) : 0;
        if (new_h == 0) {
            UnloadFont(fr->font); free(fr->data); free(fr);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "UploadSound") == 0) {
        const char *ft  = get_string(args, "fileType", NULL);
        const char *b64 = get_string(args, "data",     NULL);
        if (!ft || !b64) {
            send_error_response(conn_fd, cmd->id, "missing fileType or data"); return;
        }
        size_t        data_len;
        unsigned char *data = b64_decode(b64, strlen(b64), &data_len);
        if (!data) { send_error_response(conn_fd, cmd->id, "base64 decode failed"); return; }
        Wave w = LoadWaveFromMemory(ft, data, (int)data_len);
        free(data);
        if (!w.data) { send_error_response(conn_fd, cmd->id, "wave decode failed"); return; }
        Sound *snd = malloc(sizeof(Sound));
        if (!snd) { UnloadWave(w); send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        *snd = LoadSoundFromWave(w);
        UnloadWave(w);
        if (!snd->stream.buffer) {
            free(snd);
            send_error_response(conn_fd, cmd->id, "sound upload failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_SOUND, snd) : 0;
        if (new_h == 0) {
            UnloadSound(*snd); free(snd);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "UploadMusic") == 0) {
        const char *ft  = get_string(args, "fileType", NULL);
        const char *b64 = get_string(args, "data",     NULL);
        if (!ft || !b64) {
            send_error_response(conn_fd, cmd->id, "missing fileType or data"); return;
        }
        size_t        data_len;
        unsigned char *data = b64_decode(b64, strlen(b64), &data_len);
        if (!data) { send_error_response(conn_fd, cmd->id, "base64 decode failed"); return; }
        MusicResource *mr = calloc(1, sizeof(MusicResource));
        if (!mr) { free(data); send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        mr->music = LoadMusicStreamFromMemory(ft, data, (int)data_len);
        if (!mr->music.stream.buffer) {
            free(data); free(mr);
            send_error_response(conn_fd, cmd->id, "music load failed");
            return;
        }
        // Keep data alive; the streaming decoder reads from it at runtime.
        mr->data     = data;
        mr->data_len = data_len;
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_MUSIC, mr) : 0;
        if (new_h == 0) {
            UnloadMusicStream(mr->music); free(mr->data); free(mr);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }
    if (strcmp(name, "UploadShader") == 0) {
        const char *vs_src = get_string(args, "vsSource", NULL);
        const char *fs_src = get_string(args, "fsSource", NULL);
        Shader *sh = malloc(sizeof(Shader));
        if (!sh) { send_error_response(conn_fd, cmd->id, "out of memory"); return; }
        *sh = LoadShaderFromMemory(vs_src, fs_src);
        if (sh->id == 0) {
            free(sh);
            send_error_response(conn_fd, cmd->id, "shader compile failed");
            return;
        }
        int new_h = g_registry ? handle_alloc(g_registry, HANDLE_SHADER, sh) : 0;
        if (new_h == 0) {
            UnloadShader(*sh); free(sh);
            send_error_response(conn_fd, cmd->id, "handle registry full");
            return;
        }
        send_handle_response(conn_fd, cmd->id, new_h);
        return;
    }

    RLS_WARNING("unknown command: %s", name);
}
