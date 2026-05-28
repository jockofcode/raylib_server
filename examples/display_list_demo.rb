#!/usr/bin/env ruby
# Display list (retained-mode) demo.
#
# Usage:  ruby examples/display_list_demo.rb
#         ruby examples/display_list_demo.rb 7878      # custom port
#
# Demonstrates retained-mode rendering:
#   - A static "background" display list is recorded once and replayed every
#     frame automatically — no resending required.
#   - A "scene" display list is updated each frame with a new position for the
#     moving ball.  Only the changed list needs to be resent.
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'

PORT = (ARGV[0] || 7878).to_i

WIDTH  = 800
HEIGHT = 600
FPS    = 60.0
FRAME  = 1.0 / FPS

socket = TCPSocket.new('localhost', PORT)

def cmd(socket, name, args = nil)
  msg = { cmd: name }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
end

def cmd_id(socket, id, name, args = nil)
  msg = { id: id, cmd: name }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
  # Read and discard the ACK response.
  socket.gets
end

# ---------------------------------------------------------------------------
# Build the static background display list (sent once).
# ---------------------------------------------------------------------------

cmd(socket, 'DisplayListBegin', { name: 'background' })

cmd(socket, 'ClearBackground', { color: 'RAYWHITE' })

# Decorative grid of small circles.
(0..7).each do |col|
  (0..5).each do |row|
    cx = 50 + col * 100
    cy = 50 + row * 100
    cmd(socket, 'DrawCircleV', { center: [cx, cy], radius: 6,
                                  color: [180, 200, 220, 180] })
  end
end

# Border rectangle.
cmd(socket, 'DrawRectangleLinesEx', { rec: [8, 8, WIDTH - 16, HEIGHT - 16],
                                      lineThick: 3.0, color: 'DARKGRAY' })

cmd(socket, 'DrawText', { text: 'Display List Demo — background recorded once',
                           posX: 20, posY: HEIGHT - 30,
                           fontSize: 14, color: 'GRAY' })

cmd(socket, 'DisplayListEnd')

# Set render order: background first, then scene.
cmd(socket, 'DisplayListSetOrder', { names: %w[background scene] })

puts "Static background recorded. Animating ball — press Ctrl+C to stop."

# ---------------------------------------------------------------------------
# Animation loop — only the "scene" list changes each frame.
# ---------------------------------------------------------------------------

angle = 0.0
cx    = WIDTH  / 2.0
cy    = HEIGHT / 2.0
r_orb = 180.0  # orbit radius

begin
  loop do
    t0 = Time.now

    # Compute ball position on a circular orbit.
    bx = (cx + Math.cos(angle) * r_orb).round
    by = (cy + Math.sin(angle) * r_orb * 0.5).round
    angle += 0.04

    # Re-record only the scene list.
    cmd(socket, 'DisplayListBegin', { name: 'scene' })

    # Shadow.
    cmd(socket, 'DrawCircleV', { center: [bx + 5, by + 5], radius: 28,
                                  color: [0, 0, 0, 50] })
    # Ball.
    cmd(socket, 'DrawCircleV', { center: [bx, by], radius: 28, color: 'RED' })
    # Highlight.
    cmd(socket, 'DrawCircleV', { center: [bx - 8, by - 8], radius: 8,
                                  color: [255, 180, 180, 200] })

    cmd(socket, 'DrawFPS', { posX: 10, posY: 10 })

    cmd(socket, 'DisplayListEnd')

    elapsed = Time.now - t0
    sleep([FRAME - elapsed, 0].max)
  rescue Errno::EPIPE, Errno::ECONNRESET
    puts "Server disconnected."
    break
  end
rescue Interrupt
  puts "\nStopping..."
ensure
  # Clean up display lists on exit.
  cmd(socket, 'DisplayListDelete', { name: 'scene' })      rescue nil
  cmd(socket, 'DisplayListDelete', { name: 'background' }) rescue nil
  socket.close rescue nil
end
