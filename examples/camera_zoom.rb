#!/usr/bin/env ruby
# Camera2D zoom and pan demo.
#
# Draws a world of shapes, then uses BeginMode2D / EndMode2D with an
# animated Camera2D to zoom and orbit around the scene.  A HUD drawn
# after EndMode2D shows current camera parameters in screen space.
#
# Usage:  ruby examples/camera_zoom.rb
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS
W     = 800
H     = 600

socket = TCPSocket.new('localhost', PORT)

def send_cmd(socket, cmd, args = nil)
  msg = { cmd: cmd }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
end

def cmd(c, a = nil)
  JSON.generate(a ? { cmd: c, args: a } : { cmd: c })
end

send_cmd(socket, 'SetWindowSize',  { width: W, height: H })
send_cmd(socket, 'SetWindowTitle', { title: 'Camera2D — zoom & pan demo' })

# ---------------------------------------------------------------------------
# Fixed world geometry (world-space coordinates centred near 0,0)
# ---------------------------------------------------------------------------

WORLD_SHAPES = [
  # Ground grid lines
  *(-6..6).map { |i|
    [cmd('DrawLine', { startPosX: i * 60, startPosY: -360, endPosX: i * 60, endPosY: 360,
                       color: [50, 50, 60, 255] }),
     cmd('DrawLine', { startPosX: -360, startPosY: i * 60, endPosX: 360, endPosY: i * 60,
                       color: [50, 50, 60, 255] })]
  }.flatten,

  # Colourful shapes scattered in world space
  cmd('DrawPoly',        { center: [0,    0],   sides: 6, radius: 60, rotation: 0,   color: 'GOLD' }),
  cmd('DrawCircleV',     { center: [-150, 0],   radius: 40, color: 'RED' }),
  cmd('DrawCircleV',     { center: [ 150, 0],   radius: 40, color: 'BLUE' }),
  cmd('DrawRectangleRounded', { rec: [-200, -200, 80, 80], roundness: 0.3, segments: 8, color: 'GREEN' }),
  cmd('DrawRectangleRounded', { rec: [ 120, -200, 80, 80], roundness: 0.3, segments: 8, color: 'PURPLE' }),
  cmd('DrawTriangle',         { v1: [0, -180], v2: [-60, -80], v3: [60, -80], color: 'ORANGE' }),
  cmd('DrawEllipse',     { centerX: 0, centerY: 200, radiusH: 100, radiusV: 35, color: 'SKYBLUE' }),
  cmd('DrawRing',        { center: [-200, 150], innerRadius: 20, outerRadius: 45,
                           startAngle: 0, endAngle: 360, segments: 32, color: 'LIME' }),
  cmd('DrawSplineCatmullRom', { points: [[-300,  50], [-200, -50], [-100,  50],
                                          [   0, -50], [ 100,  50], [ 200, -50], [300, 50]],
                                thick: 3.0, color: 'PINK' }),
  # Origin marker
  cmd('DrawCircleV',      { center: [0, 0], radius: 5, color: 'WHITE' }),
  cmd('DrawLine',         { startPosX: -10, startPosY: 0, endPosX: 10, endPosY: 0, color: 'WHITE' }),
  cmd('DrawLine',         { startPosX: 0, startPosY: -10, endPosX: 0, endPosY: 10, color: 'WHITE' }),
].freeze

puts "Camera2D demo running — press Ctrl+C to exit."

t = 0.0
begin
  loop do
    t0 = Time.now

    # Animate camera
    zoom      = 0.8 + 0.6 * (0.5 + 0.5 * Math.sin(t * 0.7))
    orbit_r   = 80.0
    target_x  = orbit_r * Math.cos(t * 0.4)
    target_y  = orbit_r * Math.sin(t * 0.4) * 0.5
    rotation  = Math.sin(t * 0.3) * 8.0  # gentle tilt

    frame = []
    frame << JSON.generate({ cmd: 'ClearBackground', args: { color: [15, 15, 25, 255] } })
    frame << JSON.generate({ cmd: 'BeginMode2D',
                             args: { camera: {
                               offset:   [W / 2, H / 2],
                               target:   [target_x, target_y],
                               rotation: rotation,
                               zoom:     zoom
                             } } })
    frame.concat(WORLD_SHAPES)
    frame << JSON.generate({ cmd: 'EndMode2D' })

    # HUD (screen space)
    frame << JSON.generate({ cmd: 'DrawRectangle',
                             args: { posX: 10, posY: 10, width: 230, height: 90,
                                     color: [0, 0, 0, 160] } })
    frame << JSON.generate({ cmd: 'DrawText',
                             args: { text: 'Camera2D',
                                     posX: 18, posY: 15, fontSize: 16, color: 'GOLD' } })
    frame << JSON.generate({ cmd: 'DrawText',
                             args: { text: "zoom:     #{'%.3f' % zoom}",
                                     posX: 18, posY: 35, fontSize: 14, color: 'WHITE' } })
    frame << JSON.generate({ cmd: 'DrawText',
                             args: { text: "target:   #{'%.0f' % target_x}, #{'%.0f' % target_y}",
                                     posX: 18, posY: 53, fontSize: 14, color: 'WHITE' } })
    frame << JSON.generate({ cmd: 'DrawText',
                             args: { text: "rotation: #{'%.1f' % rotation}°",
                                     posX: 18, posY: 71, fontSize: 14, color: 'WHITE' } })
    frame << JSON.generate({ cmd: 'DrawFPS', args: { posX: W - 80, posY: 10 } })

    socket.write(frame.join("\n") + "\n")

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
