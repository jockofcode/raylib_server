#!/usr/bin/env ruby
# Animated bouncing ball.
#
# Usage:  ruby examples/bouncing_ball.rb
#         ruby examples/bouncing_ball.rb 7878      # custom port
#         ruby examples/bouncing_ball.rb 7878 60   # custom port + fps
#
# Sends a new frame every ~1/FPS seconds.  The server drains the queue
# inside its own render loop; the client and server run independently so
# occasional frame jitter is normal.
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'

PORT  = (ARGV[0] || 7878).to_i
FPS   = (ARGV[1] || 60).to_f
FRAME = 1.0 / FPS

WIDTH  = 800
HEIGHT = 600

socket = TCPSocket.new('localhost', PORT)

def send_cmd(socket, cmd, args = nil)
  msg = { cmd: cmd }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
end

x  = WIDTH  / 2.0
y  = HEIGHT / 2.0
vx = 4.5
vy = 3.0
r  = 30

puts "Bouncing ball running — press Ctrl+C to stop."

begin
  loop do
    t0 = Time.now

    # Physics
    x += vx
    y += vy
    vx = -vx if x - r <= 0 || x + r >= WIDTH
    vy = -vy if y - r <= 0 || y + r >= HEIGHT

    # Draw frame
    send_cmd(socket, 'ClearBackground', { color: 'RAYWHITE' })

    # Shadow (slightly offset dark circle)
    send_cmd(socket, 'DrawCircle', { centerX: x.to_i + 4, centerY: y.to_i + 4,
                                     radius: r, color: [0, 0, 0, 60] })
    # Ball
    send_cmd(socket, 'DrawCircle', { centerX: x.to_i, centerY: y.to_i,
                                     radius: r, color: 'RED' })

    # Velocity indicator line (shows direction)
    ex = (x + vx * 6).to_i
    ey = (y + vy * 6).to_i
    send_cmd(socket, 'DrawRectangle', { posX: [x.to_i, ex].min,
                                        posY: [y.to_i, ey].min,
                                        width:  (vx.abs * 6).ceil.clamp(1, 999),
                                        height: (vy.abs * 6).ceil.clamp(1, 999),
                                        color: [200, 50, 50, 120] })

    send_cmd(socket, 'DrawFPS', { posX: 10, posY: 10 })
    send_cmd(socket, 'DrawText', { text: "pos: #{x.to_i}, #{y.to_i}",
                                   posX: 10, posY: HEIGHT - 30,
                                   fontSize: 16, color: 'DARKGRAY' })

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
