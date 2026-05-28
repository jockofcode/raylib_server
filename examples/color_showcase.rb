#!/usr/bin/env ruby
# Named color showcase.
#
# Usage:  ruby examples/color_showcase.rb
#
# Draws a grid of swatches — one for each raylib named color — with the
# color name printed inside each swatch.
#
# The scene is redrawn every frame because raylib double-buffers: without
# resending, only alternating frames contain the scene and the display
# flickers.  Phase 4 (display lists / retained mode) will remove this need.
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS

COLORS = %w[
  LIGHTGRAY GRAY DARKGRAY
  YELLOW GOLD ORANGE PINK
  RED MAROON
  GREEN LIME DARKGREEN
  SKYBLUE BLUE DARKBLUE
  PURPLE VIOLET DARKPURPLE
  BEIGE BROWN DARKBROWN
  WHITE BLACK BLANK
  MAGENTA CYAN RAYWHITE
].freeze

DARK_BG = %w[DARKGRAY DARKGREEN DARKBLUE DARKPURPLE DARKBROWN BLACK BLANK
             MAROON MAGENTA PURPLE VIOLET BLUE GRAY].freeze

COLS   = 6
CELL_W = 120
CELL_H = 70
PAD    = 10
WIN_W  = COLS * CELL_W + PAD * 2
WIN_H  = ((COLORS.size + COLS - 1) / COLS) * CELL_H + PAD * 2 + 40

socket = TCPSocket.new('localhost', PORT)

def send_cmd(socket, cmd, args = nil)
  msg = { cmd: cmd }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
end

send_cmd(socket, 'SetWindowSize',  { width: WIN_W, height: WIN_H })
send_cmd(socket, 'SetWindowTitle', { title: 'raylib named colors' })

# Pre-build the per-frame command list so JSON serialisation only happens once.
frame_cmds = []
frame_cmds << JSON.generate({ cmd: 'ClearBackground', args: { color: [30, 30, 30, 255] } })
frame_cmds << JSON.generate({ cmd: 'DrawText',
                               args: { text: 'raylib named colors',
                                       posX: PAD, posY: PAD,
                                       fontSize: 22, color: 'WHITE' } })

COLORS.each_with_index do |name, i|
  col = i % COLS
  row = i / COLS
  x   = PAD + col * CELL_W
  y   = PAD + 40 + row * CELL_H

  frame_cmds << JSON.generate({ cmd: 'DrawRectangle',
                                 args: { posX: x, posY: y,
                                         width: CELL_W - 4, height: CELL_H - 4,
                                         color: name } })

  text_color = DARK_BG.include?(name) ? 'WHITE' : 'BLACK'
  label      = name.length > 10 ? name[0..9] : name
  frame_cmds << JSON.generate({ cmd: 'DrawText',
                                 args: { text: label,
                                         posX: x + 6,
                                         posY: y + (CELL_H - 4) / 2 - 8,
                                         fontSize: 14, color: text_color } })
end

# Join all commands into one write per frame to reduce syscall overhead.
frame_payload = frame_cmds.join("\n") + "\n"

puts "Color showcase running — press Ctrl+C to exit."

begin
  loop do
    t0 = Time.now
    socket.write(frame_payload)
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
