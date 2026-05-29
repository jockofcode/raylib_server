# raylib_server

A long-running server process that opens a [raylib](https://www.raylib.com) graphics window and accepts drawing commands over a local TCP socket. Clients connect from any language, send JSON commands to draw shapes, load resources, query input state, and subscribe to events — then the server renders everything to the window at a steady frame rate.

This is spiritually similar to what X11 did for networked graphics, purpose-built for raylib's API over a simple, modern wire protocol.

---

## Table of Contents

- [Architecture](#architecture)
- [Building](#building)
- [Running](#running)
- [Wire Protocol](#wire-protocol)
- [Client Libraries](#client-libraries)
- [Command Reference](#command-reference)
- [Examples](#examples)
- [Testing](#testing)
- [Implementation Notes](#implementation-notes)

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    raylib_server process                  │
│                                                          │
│  Main Thread                  Connection Threads         │
│  ┌──────────────┐             ┌───────────────────┐      │
│  │ raylib loop  │◄────queue───│  TCP listener     │      │
│  │ BeginDrawing │             │  parse commands   │      │
│  │ drain queue  │────reply───►│  send responses   │      │
│  │ tick timers  │             └───────────────────┘      │
│  │ push events  │                                        │
│  │ EndDrawing   │                                        │
│  └──────────────┘                                        │
└─────────────────────────────────────────────────────────┘
         ▲
   graphics window on screen
```

**Threading model:**

- The **main thread** runs the raylib game loop (required by macOS/GLFW). Each frame it drains the command queue, replays display lists, advances timers, and pushes input events to subscribers.
- A **listener thread** accepts incoming connections and spawns a detached **connection thread** per client.
- **Connection threads** receive lines, parse JSON, send optimistic ACKs for fire-and-forget commands, and push commands onto a shared ring-buffer queue for the main thread to execute.
- **Sync commands** (queries that return values) skip the optimistic ACK; the main thread sends the real response after executing the raylib call.

---

## Building

**Requirements:**

- CMake 3.15+
- A C11 compiler (clang or gcc)
- Git (for FetchContent dependencies)
- macOS or Linux with a display (or a virtual framebuffer)

Dependencies are fetched automatically at configure time:

| Dependency | Version | Purpose |
|---|---|---|
| [raylib](https://github.com/raysan5/raylib) | 5.0 | Graphics, audio, input |
| [cJSON](https://github.com/DaveGamble/cJSON) | 1.7.18 | JSON parsing |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | 2.5.2 | Unit test framework |

```bash
cmake -B build
cmake --build build -j$(nproc)
```

The server binary is `build/raylib_server`.

---

## Running

```bash
# Default: port 7878, log level info
./build/raylib_server

# Custom port
./build/raylib_server 9000

# Verbose logging
./build/raylib_server --log-level debug

# Suppress all output
./build/raylib_server --log-level none
```

**Log levels:** `debug` | `info` | `warning` | `error` | `none`

Log output goes to stderr, format: `[HH:MM:SS.mmm] [LEVEL  ] message`

---

## Wire Protocol

### Encoding

Newline-delimited JSON over TCP (`localhost:7878` by default). One JSON object per line, `\n`-terminated. Easy to test with `nc` or `socat`.

Binary mode (MessagePack) is available per-connection for high-throughput clients — see [Binary Mode](#binary-mode-messagepack).

### Command envelope

**Client → server:**
```json
{"id": "req-001", "cmd": "DrawCircle", "args": {"centerX": 400, "centerY": 300, "radius": 50, "color": "RED"}}
```

- `id` — optional, echoed in the response. Omit for fire-and-forget (no reply sent).
- `cmd` — command name, matching the raylib function name.
- `args` — command-specific parameters.

**Server → client:**
```json
{"id": "req-001", "ok": true, "result": {"width": 800}}
{"id": "req-001", "ok": false, "error": "unknown command"}
```

Fire-and-forget commands (no `id`) are silent. Commands with an `id` get `{"id":"...","ok":true}` on success.

### Shared types

| Raylib type | JSON encoding |
|---|---|
| `Color` | `[r, g, b, a]` (0–255) or named string: `"RED"`, `"RAYWHITE"`, … |
| `Vector2` | `[x, y]` |
| `Vector3` | `[x, y, z]` |
| `Rectangle` | `[x, y, width, height]` |
| `Camera2D` | `{"offset":[x,y],"target":[x,y],"rotation":f,"zoom":f}` |
| `Camera3D` | `{"position":[x,y,z],"target":[x,y,z],"up":[x,y,z],"fovy":f,"projection":0}` |
| Resource handle | integer (returned by `Load*` / `Upload*` commands) |

**Named colors:** `BLACK`, `WHITE`, `RED`, `GREEN`, `BLUE`, `YELLOW`, `MAGENTA`, `CYAN`, `ORANGE`, `PINK`, `PURPLE`, `SKYBLUE`, `BEIGE`, `BROWN`, `DARKGRAY`, `GRAY`, `LIGHTGRAY`, `DARKGREEN`, `DARKBLUE`, `DARKPURPLE`, `DARKBROWN`, `GOLD`, `LIME`, `MAROON`, `VIOLET`, `RAYWHITE`, `BLANK`

### Drawing modes

The server controls `BeginDrawing`/`EndDrawing` internally. Clients have two drawing modes:

**Immediate mode** — Send `ClearBackground` and draw commands each frame. They execute inside the server's current frame.

**Retained mode (display lists)** — Record a named display list once; the server replays it every frame automatically. Update only the parts that change. This is the recommended mode for animations.

```
# Record once:
{"cmd":"DisplayListBegin","args":{"name":"scene"}}
{"cmd":"ClearBackground","args":{"color":"RAYWHITE"}}
{"cmd":"DrawCircleV","args":{"center":[400,300],"radius":50,"color":"RED"}}
{"cmd":"DisplayListEnd"}

# Update just the moving part:
{"cmd":"DisplayListBegin","args":{"name":"scene"}}
{"cmd":"ClearBackground","args":{"color":"RAYWHITE"}}
{"cmd":"DrawCircleV","args":{"center":[450,310],"radius":50,"color":"RED"}}
{"cmd":"DisplayListEnd"}
```

### Binary mode (MessagePack)

Send `BINARY\n` as the very first line to switch the connection to MessagePack framing. The server sends a JSON ACK (`{"ok":true}`), then all subsequent frames use a 4-byte little-endian length prefix followed by a MessagePack payload with the same logical structure as the JSON envelope.

```
BINARY\n
← {"ok":true}\n
[4-byte LE length][msgpack payload]
```

---

## Client Libraries

### Ruby

`lib/raylib_client.rb` — zero external dependencies.

```ruby
require_relative 'lib/raylib_client'

rls = RaylibClient.connect(port: 7878)

# Retained-mode scene — recorded once, replayed every frame
rls.display_list('main') do
  rls.clear_background(color: 'RAYWHITE')
  rls.draw_circle_v(center: [400, 300], radius: 50, color: 'RED')
  rls.draw_text(text: 'Hello from Ruby', posX: 260, posY: 20, fontSize: 24, color: 'BLACK')
end

# Sync query — blocks until the server replies
info = rls.get_server_info
puts "fps=#{info['fps']} frame=#{info['frame']}"

# Batch — flush many fire-and-forget commands in a single write
rls.batch do
  rls.draw_fps(posX: 10, posY: 10)
  rls.draw_rectangle(posX: 100, posY: 100, width: 200, height: 50, color: 'BLUE')
end

# Events
rls.subscribe('KeyPressed', 'MouseMoved')
loop do
  rls.drain_events.each { |ev| puts ev.inspect }
  sleep 0.016
end

rls.close
```

**Key API:**

| Method | Description |
|---|---|
| `RaylibClient.connect(port:)` | Open a connection |
| `cmd(name, args)` | Fire-and-forget command |
| `sync(name, args)` | Blocking command, returns result hash |
| `batch { }` | Buffer commands and flush in one write |
| `display_list(name) { }` | Record a named display list |
| `subscribe(*events)` | Subscribe to server-push events |
| `drain_events` | Non-blocking read of pending events |
| `chunked_upload(path, ...)` | Upload a file in chunks |
| `upload_data(bytes, ...)` | Upload raw bytes in chunks |
| `enable_binary_mode!` | Switch to MessagePack framing |
| `close` | Close the socket |

All raylib commands are available as snake_case methods via `method_missing`:

```ruby
rls.draw_circle(centerX: 400, centerY: 300, radius: 50, color: 'RED')
rls.load_texture(path: '/path/to/sprite.png')   # sync, returns {"handle": 1}
rls.is_key_down(key: 65)                         # sync, returns {"down": true}
```

**Binary mode** (no external gems required):
```ruby
rls.enable_binary_mode!   # uses built-in lib/msgpack_mini.rb
rls.draw_circle(centerX: 400, centerY: 300, radius: 50, color: 'RED')
```

### Python

`lib/raylib_client.py` — requires Python 3.9+, no external packages.

```python
from lib.raylib_client import RaylibClient

with RaylibClient.connect() as rls:
    with rls.display_list('main'):
        rls.clear_background(color='RAYWHITE')
        rls.draw_circle_v(center=[400, 300], radius=50, color='RED')

    info = rls.get_server_info()
    print(f"Server v{info['version']} on port {info['port']}")
```

### Node.js

`lib/raylib_client.js` — requires Node.js, no npm packages.

```javascript
const { RaylibClient } = require('./lib/raylib_client');

const rls = await RaylibClient.connect();

await rls.displayList('main', async () => {
  await rls.clearBackground({ color: 'RAYWHITE' });
  await rls.drawCircleV({ center: [400, 300], radius: 50, color: 'RED' });
});

const info = await rls.getServerInfo();
console.log(`fps=${info.fps}`);

await rls.close();
```

### Bash

`lib/raylib_shell.sh` — uses bash's built-in `/dev/tcp`, no external tools.

```bash
source lib/raylib_shell.sh
rls_connect
rls_cmd DrawCircle '{"centerX":400,"centerY":300,"radius":50,"color":"RED"}'
rls_sync GetScreenWidth | jq .width
rls_disconnect
```

---

## Command Reference

### Window management

| Command | Args | Notes |
|---|---|---|
| `SetWindowTitle` | `{"title":s}` | |
| `SetWindowSize` | `{"width":i,"height":i}` | |
| `SetWindowPosition` | `{"x":i,"y":i}` | |
| `SetWindowMinSize` | `{"width":i,"height":i}` | |
| `SetWindowMaxSize` | `{"width":i,"height":i}` | |
| `SetTargetFPS` | `{"fps":i}` | |
| `SetWindowOpacity` | `{"opacity":f}` | 0.0–1.0 |
| `ToggleFullscreen` | — | |
| `ToggleBorderlessWindowed` | — | |
| `MaximizeWindow` | — | |
| `MinimizeWindow` | — | |
| `RestoreWindow` | — | |
| `SetWindowFocused` | — | |
| `SetWindowState` | `{"flags":i}` | `ConfigFlags` bitfield |
| `ClearWindowState` | `{"flags":i}` | |
| `CloseWindow` | — | Shuts down the server |

### Drawing lifecycle

Client `BeginDrawing`/`EndDrawing` are no-ops (the server controls the frame). Use them only for documentation purposes.

| Command | Args | Notes |
|---|---|---|
| `ClearBackground` | `{"color":Color}` | |
| `BeginMode2D` | `{"camera":Camera2D}` | |
| `EndMode2D` | — | |
| `BeginMode3D` | `{"camera":Camera3D}` | |
| `EndMode3D` | — | |
| `BeginTextureMode` | `{"handle":i}` | Render to a RenderTexture |
| `EndTextureMode` | — | |
| `BeginBlendMode` | `{"mode":i}` | `BlendMode` enum |
| `EndBlendMode` | — | |
| `BeginScissorMode` | `{"x":i,"y":i,"width":i,"height":i}` | |
| `EndScissorMode` | — | |
| `BeginShaderMode` | `{"handle":i}` | |
| `EndShaderMode` | — | |

### 2D shape drawing

| Command | Args |
|---|---|
| `DrawPixel` | `{"posX":i,"posY":i,"color":Color}` |
| `DrawLine` | `{"startPosX":i,"startPosY":i,"endPosX":i,"endPosY":i,"color":Color}` |
| `DrawLineEx` | `{"startPos":V2,"endPos":V2,"thick":f,"color":Color}` |
| `DrawLineStrip` | `{"points":[V2,...],"color":Color}` |
| `DrawLineBezier` | `{"startPos":V2,"endPos":V2,"thick":f,"color":Color}` |
| `DrawCircle` | `{"centerX":i,"centerY":i,"radius":f,"color":Color}` |
| `DrawCircleV` | `{"center":V2,"radius":f,"color":Color}` |
| `DrawCircleGradient` | `{"centerX":i,"centerY":i,"radius":f,"inner":Color,"outer":Color}` |
| `DrawCircleSector` | `{"center":V2,"radius":f,"startAngle":f,"endAngle":f,"segments":i,"color":Color}` |
| `DrawCircleSectorLines` | same as above |
| `DrawCircleLines` | `{"centerX":i,"centerY":i,"radius":f,"color":Color}` |
| `DrawEllipse` | `{"centerX":i,"centerY":i,"radiusH":f,"radiusV":f,"color":Color}` |
| `DrawEllipseLines` | same as above |
| `DrawRing` | `{"center":V2,"innerRadius":f,"outerRadius":f,"startAngle":f,"endAngle":f,"segments":i,"color":Color}` |
| `DrawRingLines` | same as above |
| `DrawRectangle` | `{"posX":i,"posY":i,"width":i,"height":i,"color":Color}` |
| `DrawRectangleV` | `{"position":V2,"size":V2,"color":Color}` |
| `DrawRectangleRec` | `{"rec":Rect,"color":Color}` |
| `DrawRectanglePro` | `{"rec":Rect,"origin":V2,"rotation":f,"color":Color}` |
| `DrawRectangleGradientV` | `{"posX":i,"posY":i,"width":i,"height":i,"top":Color,"bottom":Color}` |
| `DrawRectangleGradientH` | same with `left`/`right` |
| `DrawRectangleGradientEx` | `{"rec":Rect,"topLeft":Color,"bottomLeft":Color,"bottomRight":Color,"topRight":Color}` |
| `DrawRectangleLines` | `{"posX":i,"posY":i,"width":i,"height":i,"color":Color}` |
| `DrawRectangleLinesEx` | `{"rec":Rect,"lineThick":f,"color":Color}` |
| `DrawRectangleRounded` | `{"rec":Rect,"roundness":f,"segments":i,"color":Color}` |
| `DrawRectangleRoundedLinesEx` | `{"rec":Rect,"roundness":f,"segments":i,"lineThick":f,"color":Color}` |
| `DrawTriangle` | `{"v1":V2,"v2":V2,"v3":V2,"color":Color}` |
| `DrawTriangleLines` | same |
| `DrawTriangleFan` | `{"points":[V2,...],"color":Color}` |
| `DrawTriangleStrip` | same |
| `DrawPoly` | `{"center":V2,"sides":i,"radius":f,"rotation":f,"color":Color}` |
| `DrawPolyLines` | same |
| `DrawPolyLinesEx` | `{"center":V2,"sides":i,"radius":f,"rotation":f,"lineThick":f,"color":Color}` |
| `DrawSplineLinear` | `{"points":[V2,...],"thick":f,"color":Color}` |
| `DrawSplineBasis` | same (min 4 points) |
| `DrawSplineCatmullRom` | same (min 4 points) |
| `DrawSplineBezierQuadratic` | same (min 3 points) |
| `DrawSplineBezierCubic` | same (min 4 points) |

### 3D shape drawing

All 3D commands must be issued between `BeginMode3D` / `EndMode3D`.

| Command | Args |
|---|---|
| `DrawLine3D` | `{"startPos":V3,"endPos":V3,"color":Color}` |
| `DrawPoint3D` | `{"position":V3,"color":Color}` |
| `DrawCircle3D` | `{"center":V3,"radius":f,"rotationAxis":V3,"rotationAngle":f,"color":Color}` |
| `DrawTriangle3D` | `{"v1":V3,"v2":V3,"v3":V3,"color":Color}` |
| `DrawCube` | `{"position":V3,"width":f,"height":f,"length":f,"color":Color}` |
| `DrawCubeV` | `{"position":V3,"size":V3,"color":Color}` |
| `DrawCubeWires` | same as `DrawCube` |
| `DrawSphere` | `{"centerPos":V3,"radius":f,"color":Color}` |
| `DrawSphereEx` | `{"centerPos":V3,"radius":f,"rings":i,"slices":i,"color":Color}` |
| `DrawSphereWires` | same as `DrawSphereEx` |
| `DrawCylinder` | `{"position":V3,"radiusTop":f,"radiusBottom":f,"height":f,"slices":i,"color":Color}` |
| `DrawCylinderEx` | `{"startPos":V3,"endPos":V3,"startRadius":f,"endRadius":f,"sides":i,"color":Color}` |
| `DrawCylinderWires` | same as `DrawCylinder` |
| `DrawCylinderWiresEx` | same as `DrawCylinderEx` |
| `DrawCapsule` | `{"startPos":V3,"endPos":V3,"radius":f,"slices":i,"rings":i,"color":Color}` |
| `DrawCapsuleWires` | same |
| `DrawPlane` | `{"centerPos":V3,"size":[w,h],"color":Color}` |
| `DrawGrid` | `{"slices":i,"spacing":f}` |
| `DrawRay` | `{"position":V3,"direction":V3,"color":Color}` |
| `DrawBoundingBox` | `{"min":V3,"max":V3,"color":Color}` |
| `DrawModel` | `{"handle":i,"position":V3,"scale":f,"tint":Color}` |
| `DrawModelEx` | `{"handle":i,"position":V3,"rotationAxis":V3,"rotationAngle":f,"scale":V3,"tint":Color}` |
| `DrawModelWires` | same as `DrawModel` |
| `DrawModelWiresEx` | same as `DrawModelEx` |

### Text drawing

| Command | Args | Returns |
|---|---|---|
| `DrawFPS` | `{"posX":i,"posY":i}` | — |
| `DrawText` | `{"text":s,"posX":i,"posY":i,"fontSize":i,"color":Color}` | — |
| `DrawTextEx` | `{"font":handle,"text":s,"position":V2,"fontSize":f,"spacing":f,"tint":Color}` | — |
| `DrawTextPro` | `{"font":handle,"text":s,"position":V2,"origin":V2,"rotation":f,"fontSize":f,"spacing":f,"tint":Color}` | — |
| `MeasureText` | `{"text":s,"fontSize":i}` | `{"width":i}` |
| `MeasureTextEx` | `{"font":handle,"text":s,"fontSize":f,"spacing":f}` | `{"x":f,"y":f}` |

### Resource management

Resources are referenced by integer handles in all subsequent commands.

| Command | Args | Returns |
|---|---|---|
| `LoadTexture` | `{"path":s}` | `{"handle":i}` |
| `UnloadTexture` | `{"handle":i}` | — |
| `LoadRenderTexture` | `{"width":i,"height":i}` | `{"handle":i}` |
| `UnloadRenderTexture` | `{"handle":i}` | — |
| `LoadFont` | `{"path":s}` | `{"handle":i}` |
| `LoadFontEx` | `{"path":s,"fontSize":i,"codepoints":[i,...]}` | `{"handle":i}` |
| `UnloadFont` | `{"handle":i}` | — |
| `LoadSound` | `{"path":s}` | `{"handle":i}` |
| `UnloadSound` | `{"handle":i}` | — |
| `LoadMusicStream` | `{"path":s}` | `{"handle":i}` |
| `UnloadMusicStream` | `{"handle":i}` | — |
| `LoadShader` | `{"vsPath":s,"fsPath":s}` | `{"handle":i}` |
| `UnloadShader` | `{"handle":i}` | — |
| `LoadModel` | `{"path":s}` | `{"handle":i}` |
| `UnloadModel` | `{"handle":i}` | — |

### Inline data upload (small assets, ≤ ~750 KB)

Data is base64-encoded and embedded in the JSON command. `fileType` must include the leading dot (`.png`, `.ttf`, `.wav`, etc.).

| Command | Args | Returns |
|---|---|---|
| `UploadTexture` | `{"fileType":s,"data":"base64..."}` | `{"handle":i}` |
| `UploadTextureRaw` | `{"width":i,"height":i,"format":i,"data":"base64..."}` | `{"handle":i}` |
| `UploadFont` | `{"fileType":s,"data":"base64...","fontSize":i}` | `{"handle":i}` |
| `UploadSound` | `{"fileType":s,"data":"base64..."}` | `{"handle":i}` |
| `UploadMusic` | `{"fileType":s,"data":"base64..."}` | `{"handle":i}` |
| `UploadShader` | `{"vsSource":s,"fsSource":s}` | `{"handle":i}` |

### Chunked upload (large assets)

For assets too large to inline (e.g. high-res textures, long audio files). Chunks are sent sequentially; recommended chunk size is 48 KB raw (~64 KB base64).

```
→ {"id":"b1","cmd":"BeginUpload","args":{"name":"bg","fileType":".ogg","totalBytes":4194304}}
← {"id":"b1","ok":true,"result":{"uploadId":"u-1"}}

→ {"id":"c0","cmd":"UploadChunk","args":{"uploadId":"u-1","seq":0,"data":"T2dnU..."}}
← {"id":"c0","ok":true}
→ {"id":"c1","cmd":"UploadChunk","args":{"uploadId":"u-1","seq":1,"data":"T2dnU..."}}
← {"id":"c1","ok":true}

→ {"id":"cm","cmd":"CommitUpload","args":{"uploadId":"u-1","type":"music"}}
← {"id":"cm","ok":true,"result":{"handle":3}}
```

| Command | Args | Returns |
|---|---|---|
| `BeginUpload` | `{"name":s,"fileType":s,"totalBytes":i}` | `{"uploadId":s}` |
| `UploadChunk` | `{"uploadId":s,"seq":i,"data":"base64..."}` | — |
| `CommitUpload` | `{"uploadId":s,"type":s}` | `{"handle":i}` — type: `"texture"`, `"font"`, `"sound"`, `"music"` |
| `AbortUpload` | `{"uploadId":s}` | — |
| `ListUploads` | — | `{"uploads":[...]}` |

### Texture drawing

| Command | Args |
|---|---|
| `DrawTexture` | `{"handle":i,"posX":i,"posY":i,"tint":Color}` |
| `DrawTextureV` | `{"handle":i,"position":V2,"tint":Color}` |
| `DrawTextureEx` | `{"handle":i,"position":V2,"rotation":f,"scale":f,"tint":Color}` |
| `DrawTextureRec` | `{"handle":i,"source":Rect,"position":V2,"tint":Color}` |
| `DrawTexturePro` | `{"handle":i,"source":Rect,"dest":Rect,"origin":V2,"rotation":f,"tint":Color}` |
| `DrawTextureNPatch` | `{"handle":i,"nPatch":{...},"dest":Rect,"origin":V2,"rotation":f,"tint":Color}` |
| `SetTextureFilter` | `{"handle":i,"filter":i}` |
| `SetTextureWrap` | `{"handle":i,"wrap":i}` |

### Audio

| Command | Args |
|---|---|
| `InitAudioDevice` | — |
| `CloseAudioDevice` | — |
| `PlaySound` | `{"handle":i}` |
| `StopSound` | `{"handle":i}` |
| `PauseSound` | `{"handle":i}` |
| `ResumeSound` | `{"handle":i}` |
| `SetSoundVolume` | `{"handle":i,"volume":f}` |
| `SetSoundPitch` | `{"handle":i,"pitch":f}` |
| `PlayMusicStream` | `{"handle":i}` |
| `StopMusicStream` | `{"handle":i}` |
| `PauseMusicStream` | `{"handle":i}` |
| `ResumeMusicStream` | `{"handle":i}` |
| `SetMusicVolume` | `{"handle":i,"volume":f}` |
| `SetMusicPitch` | `{"handle":i,"pitch":f}` |
| `SeekMusicStream` | `{"handle":i,"position":f}` |

### Shader uniforms

| Command | Args |
|---|---|
| `SetShaderValue` | `{"handle":i,"locName":s,"value":any,"type":i}` |
| `SetShaderValueV` | `{"handle":i,"locName":s,"values":[...],"type":i,"count":i}` |
| `SetShaderValueMatrix` | `{"handle":i,"locName":s,"mat":[16 floats]}` |
| `SetShaderValueTexture` | `{"handle":i,"locName":s,"texHandle":i}` |

### Display lists (retained mode)

| Command | Args | Notes |
|---|---|---|
| `DisplayListBegin` | `{"name":s}` | Start recording; subsequent draw commands go into the list |
| `DisplayListEnd` | — | Stop recording |
| `DisplayListClear` | `{"name":s}` | Empty the list without deleting it |
| `DisplayListDelete` | `{"name":s}` | Remove the list entirely |
| `DisplayListSetOrder` | `{"names":[s,...]}` | Set replay order across all lists |

### Introspection — window state

| Command | Returns |
|---|---|
| `GetScreenWidth` | `{"width":i}` |
| `GetScreenHeight` | `{"height":i}` |
| `GetRenderWidth` | `{"width":i}` |
| `GetRenderHeight` | `{"height":i}` |
| `GetWindowPosition` | `{"x":f,"y":f}` |
| `GetWindowScaleDPI` | `{"x":f,"y":f}` |
| `IsWindowReady` | `{"ready":bool}` |
| `IsWindowFullscreen` | `{"fullscreen":bool}` |
| `IsWindowHidden` | `{"hidden":bool}` |
| `IsWindowMinimized` | `{"minimized":bool}` |
| `IsWindowMaximized` | `{"maximized":bool}` |
| `IsWindowFocused` | `{"focused":bool}` |
| `IsWindowResized` | `{"resized":bool}` |
| `GetFPS` | `{"fps":i}` |
| `GetFrameTime` | `{"delta":f}` |
| `GetTime` | `{"time":f}` |
| `GetMonitorCount` | `{"count":i}` |
| `GetCurrentMonitor` | `{"monitor":i}` |
| `GetMonitorWidth` | `{"monitor":i}` → `{"width":i}` |
| `GetMonitorHeight` | `{"monitor":i}` → `{"height":i}` |
| `GetMonitorName` | `{"monitor":i}` → `{"name":s}` |

### Introspection — input state

| Command | Args | Returns |
|---|---|---|
| `IsKeyPressed` | `{"key":i}` | `{"pressed":bool}` |
| `IsKeyDown` | `{"key":i}` | `{"down":bool}` |
| `IsKeyReleased` | `{"key":i}` | `{"released":bool}` |
| `IsKeyUp` | `{"key":i}` | `{"up":bool}` |
| `GetKeyPressed` | — | `{"key":i}` |
| `GetCharPressed` | — | `{"char":i}` |
| `IsMouseButtonPressed` | `{"button":i}` | `{"pressed":bool}` |
| `IsMouseButtonDown` | `{"button":i}` | `{"down":bool}` |
| `IsMouseButtonReleased` | `{"button":i}` | `{"released":bool}` |
| `GetMousePosition` | — | `{"x":f,"y":f}` |
| `GetMouseDelta` | — | `{"x":f,"y":f}` |
| `GetMouseWheelMove` | — | `{"move":f}` |
| `IsGamepadAvailable` | `{"gamepad":i}` | `{"available":bool}` |
| `IsGamepadButtonPressed` | `{"gamepad":i,"button":i}` | `{"pressed":bool}` |
| `IsGamepadButtonDown` | `{"gamepad":i,"button":i}` | `{"down":bool}` |
| `GetGamepadAxisMovement` | `{"gamepad":i,"axis":i}` | `{"movement":f}` |
| `GetTouchPointCount` | — | `{"count":i}` |
| `GetTouchPosition` | `{"index":i}` | `{"x":f,"y":f}` |
| `GetGestureDetected` | — | `{"gesture":i}` |

### Introspection — resources and server

| Command | Args | Returns |
|---|---|---|
| `ListHandles` | — | `{"textures":[i,...],"fonts":[i,...],"sounds":[i,...],"music":[i,...],"renderTextures":[i,...],"shaders":[i,...],"models":[i,...]}` |
| `GetTextureInfo` | `{"handle":i}` | `{"width":i,"height":i,"mipmaps":i,"format":i}` |
| `GetFontInfo` | `{"handle":i}` | `{"baseSize":i,"glyphCount":i}` |
| `GetDisplayLists` | — | `{"lists":[{"name":s,"commandCount":i},...]}` |
| `GetDisplayListCommands` | `{"name":s}` | `{"commands":[{...},...]}` |
| `GetServerInfo` | — | `{"version":s,"port":i,"fps":i,"frame":i,"clients":i}` |

### Event streaming

Subscribe to server-push events so clients don't need to poll.

```
→ {"id":"s1","cmd":"Subscribe","args":{"events":["KeyPressed","MouseMoved"]}}
← {"id":"s1","ok":true}
← {"event":"KeyPressed","key":65,"frame":512}
← {"event":"MouseMoved","x":320.0,"y":240.0,"frame":513}
```

| Command | Args |
|---|---|
| `Subscribe` | `{"events":[...]}` |
| `Unsubscribe` | `{"events":[...]}` |

**Available events:** `KeyPressed`, `KeyReleased`, `MouseMoved`, `MouseButtonPressed`, `MouseButtonReleased`, `MouseWheel`, `WindowResized`, `WindowFocused`, `WindowUnfocused`, `WindowClosed`, `GestureDetected`, `TimerFired`

**Event message formats:**

```json
{"event":"KeyPressed","key":65,"frame":512}
{"event":"MouseMoved","x":320.0,"y":240.0,"frame":513}
{"event":"MouseButtonPressed","button":0,"x":320.0,"y":240.0,"frame":513}
{"event":"WindowResized","width":1280,"height":720,"frame":514}
{"event":"TimerFired","timerId":"t3a9f12","name":"tick","frame":600}
```

### Animation timers

Timers fire `TimerFired` events through the event subscription system. Clients must be subscribed to `TimerFired` to receive them.

| Command | Args | Returns |
|---|---|---|
| `TimerCreate` | `{"name":s,"interval":f,"repeat":true}` | `{"timerId":s}` |
| `TimerOnce` | `{"name":s,"delay":f}` | `{"timerId":s}` |
| `TimerDelete` | `{"timerId":s}` | — |
| `ListTimers` | — | `{"timers":[{"timerId":s,"name":s,"interval":f,"repeat":bool,"elapsed":f},...]}` |

```
→ {"id":"t1","cmd":"Subscribe","args":{"events":["TimerFired"]}}
← {"id":"t1","ok":true}
→ {"id":"t2","cmd":"TimerCreate","args":{"name":"tick","interval":0.5,"repeat":true}}
← {"id":"t2","ok":true,"result":{"timerId":"t3a9f12"}}
← {"event":"TimerFired","timerId":"t3a9f12","name":"tick","frame":30}
← {"event":"TimerFired","timerId":"t3a9f12","name":"tick","frame":60}
```

---

## Examples

All examples require a running server (`./build/raylib_server`) and Ruby. Run from the project root:

```bash
ruby examples/<name>.rb
```

| Example | Features demonstrated |
|---|---|
| `bouncing_ball.rb` | Animation loop, `DrawCircle`, `DrawRectangle`, `DrawText`, `DrawFPS` |
| `color_showcase.rb` | Named colors, shape drawing |
| `shapes_demo.rb` | Full 2D shape API |
| `camera_zoom.rb` | `BeginMode2D` / `EndMode2D`, Camera2D |
| `splines_demo.rb` | `DrawSpline*` family |
| `render_texture_demo.rb` | `LoadRenderTexture`, `BeginTextureMode`, inline texture upload |
| `display_list_demo.rb` | Display list recording, partial update, `DisplayListSetOrder` |
| `sprite_demo.rb` | Chunked upload, `DrawTextureEx` |
| `window_management_demo.rb` | Window management commands |
| `event_streaming_demo.rb` | `Subscribe` / `Unsubscribe`, server-push events |
| `audio_demo.rb` | `InitAudioDevice`, `UploadSound`, `UploadMusic`, volume/pitch control |
| `input_demo.rb` | Keyboard, mouse, gamepad, touch introspection |
| `library_demo.rb` | Ruby client library — display list DSL, batch, sync queries, events |
| `3d_demo.rb` | `BeginMode3D`, Camera3D, all 3D shapes, `LoadModel`/`DrawModel` |
| `timer_demo.rb` | `TimerCreate`, `TimerOnce`, `TimerDelete`, `TimerFired` events |
| `msgpack_bench.rb` | JSON vs MessagePack throughput comparison |

Run all examples against a live server and verify they connect/disconnect cleanly:

```bash
bash scripts/test_examples.sh
```

---

## Testing

```bash
# Build and run all tests
cmake -B build && cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure

# Or via make
make -C build test
```

Tests are split into two categories:

**Unit tests (headless)** — test everything that does not touch raylib directly:

| Test | What it covers |
|---|---|
| `test_queue` | Ring-buffer queue mechanics |
| `test_protocol` | JSON parsing, line buffering, partial reads |
| `test_handle_registry` | Handle allocation, lookup, double-free detection |
| `test_b64` | Base64 decoder |
| `test_display_list` | Display list recording, ordering, clear, delete |
| `test_upload_registry` | Chunked upload slot management |
| `test_event_registry` | Subscription add/remove, event filtering |
| `test_timer_registry` | Timer create/delete/tick, one-shot and repeating |
| `test_msgpack` | MessagePack encode/decode round-trips |

**Integration test** — starts a real TCP server (no raylib window) and exercises the full command path:

| `test_integration` covers |
|---|
| Socket round-trips for all major command families |
| Sync ACK behaviour for query commands |
| Display list record → `GetDisplayListCommands` → update cycle |
| Subscribe/Unsubscribe ACKs |
| Invalid handle resilience |

**Ruby client unit tests** — 52 tests using a `FakeSocket`, no server required:

```bash
ruby tests/test_ruby_client.rb
```

---

## Implementation Notes

### Source layout

```
src/
  main.c              Entry point; raylib loop; initialises all subsystems
  commands.c/h        Raylib command dispatch (main thread only)
  server.c/h          TCP listener + per-connection threads
  protocol.c/h        ParsedCmd, JSON parse/format, LineBuffer
  queue.c/h           Thread-safe CmdQueue (mutex + condvar, capacity 2048)
  handle_registry.c/h Integer handle → void* resource map (max 4096 entries)
  display_list.c/h    DisplayListRegistry; recording and replay
  upload_registry.c/h In-progress chunked upload slots
  event_registry.c/h  Per-connection event subscriptions
  timer_registry.c/h  Animation timers; tick and fire callbacks
  color.c/h           Named color lookup + JSON parse
  b64.c/h             Base64 decoder
  msgpack.c/h         Minimal MessagePack encoder/decoder (no external dep)
  rls_log.c/h         Thread-safe timestamped logger
tests/
  test_*.c            Unity unit tests
  test_integration.c  TCP integration test (no raylib required)
  test_ruby_client.rb Ruby client unit tests (FakeSocket)
lib/
  raylib_client.rb    Ruby client library
  raylib_client.py    Python client library
  raylib_client.js    Node.js client library
  raylib_shell.sh     Bash helper functions
  msgpack_mini.rb     Pure-Ruby MessagePack encoder/decoder
examples/
  *.rb                Runnable end-to-end examples
scripts/
  test_examples.sh    Integration test runner for all examples
```

### Threading and locking

- **Main thread** owns all raylib/OpenGL calls. Nothing else touches raylib.
- **Connection threads** parse JSON, send ACKs, and push `CmdEntry` structs onto the shared `CmdQueue`.
- Certain commands are intercepted by the connection thread before the queue (display lists, chunked uploads, Subscribe/Unsubscribe, timer commands) because they don't need raylib and benefit from immediate handling.
- `er_push` (event delivery) is called from the main thread and writes directly to client socket fds while holding the `EventRegistry` mutex.

### ACK strategy

- **Fire-and-forget** (no `id`): no response sent.
- **Fire-and-forget with `id`**: optimistic ACK sent immediately by the connection thread after successful parse and queue push, before raylib executes the command.
- **Sync commands** (`Load*`, `Upload*`, `Get*`, `Is*`, `Measure*`, etc.): connection thread skips the ACK; the main thread sends the real response with the result after execution.

### Security

- Bound to `127.0.0.1` only — never reachable from the network.
- All handle IDs are validated on use; out-of-range or freed handles return an error and never crash.
- `points` arrays in shape commands are capped to prevent oversized allocations.

### raylib 5.0 API notes

The following functions from the original design do not exist in raylib 5.0 and are not implemented:

- `DrawLineDashed` — removed; use `DrawLineEx` instead
- `DrawEllipseV`, `DrawEllipseLinesV` — removed
- `DrawTriangleGradient` — removed
- `DrawCircleGradient` — takes `(int centerX, int centerY, ...)`, not `Vector2`
- `DrawRectangleRoundedLines` — merged into `DrawRectangleRoundedLinesEx`
