#!/usr/bin/env ruby
# RenderTexture demo — Phase 3 resource management.
#
# Demonstrates:
#   1. LoadRenderTexture   — server allocates an off-screen render target
#   2. BeginTextureMode    — draw into the render texture each frame
#   3. EndTextureMode
#   4. DrawTexturePro      — blit the render texture back to the window
#   5. UnloadRenderTexture — clean up on exit
#
# The demo draws a rotating kaleidoscope into the render texture and then
# displays it tiled across the screen with a mirrored effect.
#
# Usage:  ruby examples/render_texture_demo.rb
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS
W     = 800
H     = 600
RT_W  = 256
RT_H  = 256

socket = TCPSocket.new('localhost', PORT)

def send_cmd(socket, cmd, args = nil)
  msg = { cmd: cmd }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
end

def send_sync(socket, cmd, args = nil)
  id  = "r#{rand(0xFFFFFF).to_s(16)}"
  msg = { id: id, cmd: cmd }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
  line = socket.gets or raise "server disconnected"
  JSON.parse(line)
end

# Configure window
send_cmd(socket, 'SetWindowSize',  { width: W, height: H })
send_cmd(socket, 'SetWindowTitle', { title: 'RenderTexture demo — Phase 3' })

# Allocate off-screen render texture
resp = send_sync(socket, 'LoadRenderTexture', { width: RT_W, height: RT_H })
unless resp['ok']
  warn "LoadRenderTexture failed: #{resp['error']}"
  socket.close
  exit 1
end
rt_handle = resp.dig('result', 'handle')
puts "RenderTexture handle: #{rt_handle}"

# Pre-build the world shapes drawn into the render texture each frame
def shapes_cmds(t, w, h)
  cx = w / 2.0
  cy = h / 2.0
  cmds = []
  cmds << JSON.generate({ cmd: 'ClearBackground', args: { color: [10, 10, 30, 255] } })

  # Rotating spokes
  8.times do |i|
    angle = t + i * Math::PI / 4.0
    ex = cx + (w * 0.45) * Math.cos(angle)
    ey = cy + (h * 0.45) * Math.sin(angle)
    r = (128 + 127 * Math.sin(t + i)).to_i
    g = (128 + 127 * Math.cos(t + i * 0.7)).to_i
    b = (200 + 55 * Math.sin(t * 1.3 + i)).to_i
    cmds << JSON.generate({ cmd: 'DrawLineEx',
                             args: { startPos: [cx, cy], endPos: [ex, ey],
                                     thick: 3.0, color: [r, g, b, 200] } })
  end

  # Pulsing circles
  4.times do |i|
    r = 20 + 15 * Math.sin(t * 2.0 + i * 1.5)
    angle = t * 0.5 + i * Math::PI / 2.0
    px = cx + 60 * Math.cos(angle)
    py = cy + 60 * Math.sin(angle)
    hue = (i * 60 + t * 30) % 360
    cmds << JSON.generate({ cmd: 'DrawCircleV',
                             args: { center: [px, py], radius: r,
                                     color: [255, (hue % 200).to_i, 50, 220] } })
  end

  # Center polygon
  sides = 5 + (2.5 * Math.sin(t * 0.4)).round
  sides = [sides, 3].max
  cmds << JSON.generate({ cmd: 'DrawPoly',
                           args: { center: [cx, cy], sides: sides,
                                   radius: 35 + 15 * Math.sin(t * 1.2),
                                   rotation: t * 30,
                                   color: 'GOLD' } })
  cmds
end

puts "RenderTexture demo running — press Ctrl+C to exit."

t = 0.0
begin
  loop do
    t0 = Time.now

    shapes = shapes_cmds(t, RT_W, RT_H)

    frame = []

    # Draw into the render texture
    frame << JSON.generate({ cmd: 'BeginTextureMode', args: { handle: rt_handle } })
    frame.concat(shapes)
    frame << JSON.generate({ cmd: 'EndTextureMode' })

    # Clear main window
    frame << JSON.generate({ cmd: 'ClearBackground', args: { color: [20, 20, 40, 255] } })

    # Tile the render texture 2×2 with alternating mirror/flip
    [[0, 0, 1, 1], [RT_W, 0, -1, 1], [0, RT_H, 1, -1], [RT_W, RT_H, -1, -1]].each do |dx, dy, sx, sy|
      # source: the render texture is stored upside-down in OpenGL, so flip V
      src = [0, 0, RT_W * sx, RT_H * sy]
      dst = [dx + (W - RT_W * 2) / 2, dy + (H - RT_H * 2) / 2, RT_W, RT_H]
      frame << JSON.generate({ cmd: 'DrawTexturePro',
                                args: { handle: rt_handle,
                                        source: src, dest: dst,
                                        origin: [0, 0], rotation: 0,
                                        tint: 'WHITE' } })
    end

    # HUD
    frame << JSON.generate({ cmd: 'DrawText',
                             args: { text: 'RenderTexture demo',
                                     posX: 10, posY: 10, fontSize: 18, color: 'GOLD' } })
    frame << JSON.generate({ cmd: 'DrawText',
                             args: { text: "t = #{'%.2f' % t}",
                                     posX: 10, posY: 32, fontSize: 14, color: 'LIGHTGRAY' } })
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
  send_cmd(socket, 'UnloadRenderTexture', { handle: rt_handle }) rescue nil
  send_cmd(socket, 'ClearBackground', { color: 'BLACK' })        rescue nil
  socket.close rescue nil
end
