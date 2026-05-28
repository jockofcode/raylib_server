#!/usr/bin/env ruby
# Full Phase 2 shapes showcase.
#
# Draws a labelled grid covering every shape family: pixels, lines, circles,
# ellipses, rings, rectangles, triangles, polygons, and splines.
#
# Usage:  ruby examples/shapes_demo.rb
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS

W = 960
H = 720

socket = TCPSocket.new('localhost', PORT)

def send_cmd(socket, cmd, args = nil)
  msg = { cmd: cmd }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
end

send_cmd(socket, 'SetWindowSize',  { width: W, height: H })
send_cmd(socket, 'SetWindowTitle', { title: 'Phase 2 — shapes showcase' })

# ---------------------------------------------------------------------------
# Layout helpers
# ---------------------------------------------------------------------------

COLS   = 5
ROWS   = 4
PAD    = 16
CW     = (W - PAD * (COLS + 1)) / COLS   # cell width
CH     = (H - PAD * (ROWS + 1)) / ROWS   # cell height

def cell(col, row)
  x = PAD + col * (CW + PAD) + CW / 2
  y = PAD + row * (CH + PAD) + CH / 2
  [x, y]
end

def cell_rect(col, row)
  x = PAD + col * (CW + PAD)
  y = PAD + row * (CH + PAD)
  [x, y, CW, CH]
end

# ---------------------------------------------------------------------------
# Scene builder — returns a payload string of all draw commands
# ---------------------------------------------------------------------------

def build_frame(socket, t)
  cmds = []
  cmds << JSON.generate({ cmd: 'ClearBackground', args: { color: [20, 20, 30, 255] } })

  # Helper to push a command
  add = ->(cmd, args) { cmds << JSON.generate({ cmd: cmd, args: args }) }
  lbl = ->(col, row, text) {
    rx, ry, _rw, _rh = cell_rect(col, row)
    add.('DrawText', { text: text, posX: rx + 4, posY: ry + 4, fontSize: 12, color: 'LIGHTGRAY' })
  }

  # ---- Row 0 ----------------------------------------------------------------

  # 0,0  Lines
  cx, cy = cell(0, 0)
  lbl.(0, 0, 'DrawLine / DrawLineEx')
  add.('DrawLine',   { startPosX: cx - 50, startPosY: cy,      endPosX: cx + 50, endPosY: cy,      color: 'WHITE' })
  add.('DrawLineEx', { startPos: [cx - 50, cy + 15], endPos: [cx + 50, cy + 15], thick: 4.0, color: 'YELLOW' })
  add.('DrawLineBezier', { startPos: [cx - 50, cy + 35], endPos: [cx + 50, cy + 35], thick: 2.5, color: 'SKYBLUE' })
  add.('DrawLineEx',    { startPos: [cx - 50, cy + 55], endPos: [cx + 50, cy + 55],
                          thick: 1.5, color: 'LIGHTGRAY' })

  # 1,0  LineStrip
  lbl.(1, 0, 'DrawLineStrip')
  cx, cy = cell(1, 0)
  pts = (0..8).map { |i| [cx - 50 + i * 12, cy + (i.even? ? -25 : 25)] }
  add.('DrawLineStrip', { points: pts, color: 'LIME' })

  # 2,0  Circle variants
  lbl.(2, 0, 'Circle variants')
  cx, cy = cell(2, 0)
  add.('DrawCircleV',        { center: [cx - 35, cy], radius: 22, color: 'RED' })
  add.('DrawCircleLines',    { centerX: cx - 35, centerY: cy, radius: 22, color: 'ORANGE' })
  add.('DrawCircleSector',   { center: [cx + 35, cy], radius: 28, startAngle: 0, endAngle: 240, segments: 20, color: 'GOLD' })
  add.('DrawCircleSectorLines', { center: [cx + 35, cy], radius: 28, startAngle: 0, endAngle: 240, segments: 20, color: 'YELLOW' })

  # 3,0  CircleGradient
  lbl.(3, 0, 'DrawCircleGradient')
  cx, cy = cell(3, 0)
  add.('DrawCircleGradient', { centerX: cx, centerY: cy, radius: 50, inner: 'WHITE', outer: 'DARKBLUE' })

  # 4,0  Ellipses
  lbl.(4, 0, 'Ellipse / Ring')
  cx, cy = cell(4, 0)
  add.('DrawEllipse',     { centerX: cx, centerY: cy - 20, radiusH: 55, radiusV: 22, color: 'SKYBLUE' })
  add.('DrawEllipseLines',{ centerX: cx, centerY: cy - 20, radiusH: 55, radiusV: 22, color: 'BLUE' })
  add.('DrawRing',        { center: [cx, cy + 30], innerRadius: 14, outerRadius: 28,
                            startAngle: 0, endAngle: 360, segments: 32, color: 'VIOLET' })

  # ---- Row 1 ----------------------------------------------------------------

  # 0,1  Rectangle variants
  lbl.(0, 1, 'Rectangle variants')
  cx, cy = cell(0, 1)
  add.('DrawRectangleV',  { position: [cx - 50, cy - 35], size: [40, 30], color: 'RED' })
  add.('DrawRectangleLines', { posX: cx - 50, posY: cy - 35, width: 40, height: 30, color: 'ORANGE' })
  add.('DrawRectanglePro', { rec: [cx, cy - 35, 44, 28], origin: [22, 14], rotation: t * 30 % 360, color: 'GOLD' })
  add.('DrawRectangleRounded', { rec: [cx - 50, cy + 5, 44, 28], roundness: 0.4, segments: 8, color: 'GREEN' })
  add.('DrawRectangleRoundedLinesEx', { rec: [cx + 5, cy + 5, 44, 28], roundness: 0.4, segments: 8,
                                         lineThick: 2.0, color: 'LIME' })

  # 1,1  Gradient rectangles
  lbl.(1, 1, 'Gradient rects')
  cx, cy = cell(1, 1)
  add.('DrawRectangleGradientV', { posX: cx - 55, posY: cy - 40, width: 50, height: 80,
                                   top: 'WHITE', bottom: 'DARKBLUE' })
  add.('DrawRectangleGradientH', { posX: cx + 5,  posY: cy - 40, width: 50, height: 80,
                                   left: 'RED', right: 'BLUE' })

  # 2,1  Triangle variants
  lbl.(2, 1, 'Triangle variants')
  cx, cy = cell(2, 1)
  add.('DrawTriangle',         { v1: [cx - 40, cy + 30], v2: [cx - 80, cy - 20], v3: [cx,      cy - 20], color: 'RED' })
  add.('DrawTriangleLines',    { v1: [cx + 5,  cy + 30], v2: [cx - 35, cy - 20], v3: [cx + 45, cy - 20], color: 'ORANGE' })
  add.('DrawTriangle',         { v1: [cx - 40, cy + 60], v2: [cx - 80, cy + 10], v3: [cx,      cy + 10],
                                 color: 'MAGENTA' })

  # 3,1  TriangleFan / TriangleStrip
  lbl.(3, 1, 'Fan / Strip')
  cx, cy = cell(3, 1)
  fan_pts = [[cx, cy]] + (0..5).map { |i|
    a = i * 60 * Math::PI / 180
    [cx + (35 * Math.cos(a)).to_i, cy + (35 * Math.sin(a)).to_i]
  }
  add.('DrawTriangleFan', { points: fan_pts, color: 'GOLD' })
  strip = (0..4).flat_map { |i| [[cx - 30 + i * 15, cy + 35], [cx - 22 + i * 15, cy + 65]] }
  add.('DrawTriangleStrip', { points: strip, color: 'SKYBLUE' })

  # 4,1  Polygons
  lbl.(4, 1, 'Polygons')
  cx, cy = cell(4, 1)
  add.('DrawPoly',        { center: [cx - 30, cy], sides: 6, radius: 32, rotation: 0,  color: 'PURPLE' })
  add.('DrawPolyLines',   { center: [cx + 35, cy], sides: 5, radius: 30, rotation: 18, color: 'VIOLET' })
  add.('DrawPolyLinesEx', { center: [cx - 30, cy + 50], sides: 8, radius: 25,
                            rotation: 22.5, lineThick: 3.0, color: 'PINK' })

  # ---- Row 2 ----------------------------------------------------------------

  # 0-4, 2  Splines (full width)
  lbl.(0, 2, 'DrawSplineLinear')
  lbl.(1, 2, 'DrawSplineBasis')
  lbl.(2, 2, 'DrawSplineCatmullRom')
  lbl.(3, 2, 'DrawSplineBezierQuadratic')
  lbl.(4, 2, 'DrawSplineBezierCubic')

  y_base = PAD + 2 * (CH + PAD) + CH / 2
  pts = (0..5).map { |i| [PAD + i * (W - 2 * PAD) / 5.0, y_base + (i.even? ? -30 : 30)] }

  add.('DrawSplineLinear',         { points: pts, thick: 2.0, color: 'RED' })
  add.('DrawSplineBasis',          { points: pts, thick: 2.0, color: 'GREEN' })
  add.('DrawSplineCatmullRom',     { points: pts, thick: 2.0, color: 'BLUE' })
  add.('DrawSplineBezierQuadratic',{ points: pts.first(5), thick: 2.0, color: 'ORANGE' })
  add.('DrawSplineBezierCubic',    { points: pts, thick: 2.0, color: 'PURPLE' })

  # Control point dots
  pts.each { |p| add.('DrawCircleV', { center: p, radius: 4, color: 'WHITE' }) }

  # ---- Row 3 ----------------------------------------------------------------

  # 0,3  BeginMode2D / EndMode2D demo (animated zoom)
  lbl.(0, 3, 'BeginMode2D (camera)')
  rx, ry, rw, rh = cell_rect(0, 3)
  add.('BeginScissorMode', { x: rx, y: ry, width: rw, height: rh })
  zoom = 1.0 + 0.5 * Math.sin(t * 1.2)
  add.('BeginMode2D', { camera: { offset: [rx + rw / 2, ry + rh / 2],
                                  target: [0, 0],
                                  rotation: 0,
                                  zoom: zoom } })
  add.('DrawPoly',    { center: [0, 0], sides: 6, radius: 30, rotation: t * 45 % 360, color: 'GOLD' })
  add.('DrawCircleV', { center: [0, 0], radius: 6, color: 'WHITE' })
  add.('EndMode2D', {})
  add.('EndScissorMode', {})
  add.('DrawText', { text: "zoom #{'%.2f' % zoom}", posX: rx + 4, posY: ry + rh - 20,
                     fontSize: 12, color: 'LIGHTGRAY' })

  # 1,3  BeginBlendMode demo
  lbl.(1, 3, 'BeginBlendMode (additive)')
  cx, cy = cell(1, 3)
  add.('BeginBlendMode', { mode: 1 })   # BLEND_ADDITIVE
  add.('DrawCircleV', { center: [cx - 18, cy], radius: 30, color: [255, 0, 0, 180] })
  add.('DrawCircleV', { center: [cx + 18, cy], radius: 30, color: [0, 0, 255, 180] })
  add.('DrawCircleV', { center: [cx, cy - 20], radius: 30, color: [0, 255, 0, 180] })
  add.('EndBlendMode', {})

  # 2,3  BeginScissorMode demo
  lbl.(2, 3, 'BeginScissorMode')
  rx, ry, rw, rh = cell_rect(2, 3)
  clip_x = rx + rw / 4
  add.('BeginScissorMode', { x: clip_x, y: ry + 20, width: rw / 2, height: rh - 24 })
  add.('DrawRectangleGradientH', { posX: rx, posY: ry + 20, width: rw, height: rh - 24,
                                   left: 'RED', right: 'BLUE' })
  add.('DrawText', { text: 'CLIPPED', posX: rx + 10, posY: ry + rh / 2 - 10,
                     fontSize: 18, color: 'WHITE' })
  add.('EndScissorMode', {})

  # 3-4, 3  FPS + info
  lbl.(3, 3, 'FPS')
  rx, ry, _rw, _rh = cell_rect(3, 3)
  add.('DrawFPS', { posX: rx + 10, posY: ry + 24 })

  cmds.join("\n") + "\n"
end

puts "Shapes showcase running — press Ctrl+C to exit."

t = 0.0
begin
  loop do
    t0 = Time.now
    socket.write(build_frame(socket, t))
    t += FRAME
    elapsed = Time.now - t0
    sleep([FRAME - elapsed, 0].max)
  rescue Errno::EPIPE, Errno::ECONNRESET
    puts "Server disconnected."
    break
  end
rescue Interrupt
  puts "\nStopping..."
ensure
  send_cmd(socket, 'ClearBackground', { color: 'BLACK' }) rescue nil
  socket.close rescue nil
end
