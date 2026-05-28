#!/usr/bin/env ruby
# Event-streaming demo — subscribes to keyboard and mouse events and
# visualises them in the window.
#
# Demonstrates:
#   1. Subscribe     — register interest in input events
#   2. Unsubscribe   — remove event subscriptions on exit
#   3. Event push    — server sends event JSON on the subscribed connection
#
# Usage:  ruby examples/event_streaming_demo.rb
#         ruby examples/event_streaming_demo.rb 7878   # custom port
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'
require 'io/wait'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 30.0
FRAME = 1.0 / FPS
W     = 800
H     = 600

MAX_LOG = 14   # event lines shown on screen

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
  line = socket.gets or raise 'server disconnected'
  JSON.parse(line)
end

# Read all complete lines that are immediately available (non-blocking).
def drain_events(socket)
  lines = []
  while socket.ready?
    line = socket.gets
    break unless line
    lines << JSON.parse(line) rescue nil
  end
  lines.compact
end

send_cmd(socket, 'SetWindowSize',  { width: W, height: H })
send_cmd(socket, 'SetWindowTitle', { title: 'Event Streaming Demo' })

# Subscribe to input events.
resp = send_sync(socket, 'Subscribe', {
  events: %w[KeyPressed KeyReleased MouseMoved MouseButtonPressed MouseButtonReleased]
})
raise "Subscribe failed: #{resp['error']}" unless resp['ok']

event_log  = []
mouse_x    = 0
mouse_y    = 0

puts "Event-streaming demo running — interact with the window."
puts "Press Ctrl+C to stop."

begin
  loop do
    t0 = Time.now

    # Collect any event notifications pushed by the server.
    drain_events(socket).each do |ev|
      kind = ev['event'] || 'unknown'
      case kind
      when 'MouseMoved'
        mouse_x = ev['x'].to_i
        mouse_y = ev['y'].to_i
        # Mouse-moved events are high-frequency; skip adding to log.
        next
      when 'KeyPressed'
        entry = "KeyPressed  key=#{ev['key']}"
      when 'KeyReleased'
        entry = "KeyReleased key=#{ev['key']}"
      when 'MouseButtonPressed'
        entry = "MouseButtonPressed  btn=#{ev['button']}"
      when 'MouseButtonReleased'
        entry = "MouseButtonReleased btn=#{ev['button']}"
      else
        entry = kind
      end
      event_log.unshift(entry)
      event_log = event_log.first(MAX_LOG)
    end

    # Draw.
    send_cmd(socket, 'ClearBackground', { color: [15, 15, 25, 255] })

    # Title.
    send_cmd(socket, 'DrawText', {
      text: 'Event Streaming Demo',
      posX: 20, posY: 18,
      fontSize: 22,
      color: 'RAYWHITE'
    })
    send_cmd(socket, 'DrawText', {
      text: 'Press keys and click the window to generate events.',
      posX: 20, posY: 48,
      fontSize: 14,
      color: [160, 160, 200, 255]
    })

    # Mouse position indicator.
    send_cmd(socket, 'DrawText', {
      text: format('Mouse: (%d, %d)', mouse_x, mouse_y),
      posX: 20, posY: 80,
      fontSize: 16,
      color: [100, 220, 100, 255]
    })
    send_cmd(socket, 'DrawCircle', {
      centerX: mouse_x, centerY: mouse_y,
      radius: 6,
      color: [100, 220, 100, 200]
    })

    # Divider.
    send_cmd(socket, 'DrawLine', {
      startPosX: 20, startPosY: 108,
      endPosX: W - 20, endPosY: 108,
      color: [60, 60, 100, 255]
    })

    # Event log.
    send_cmd(socket, 'DrawText', {
      text: 'Recent events (newest first):',
      posX: 20, posY: 116,
      fontSize: 14,
      color: [140, 140, 180, 255]
    })

    event_log.each_with_index do |entry, i|
      alpha = 255 - i * 14
      alpha = [alpha, 60].max
      send_cmd(socket, 'DrawText', {
        text: entry,
        posX: 28, posY: 138 + i * 22,
        fontSize: 16,
        color: [200, 200, 255, alpha]
      })
    end

    send_cmd(socket, 'DrawFPS', { posX: W - 80, posY: 10 })

    elapsed = Time.now - t0
    sleep([FRAME - elapsed, 0].max)
  rescue Errno::EPIPE, Errno::ECONNRESET
    puts 'Server disconnected.'
    break
  end
rescue Interrupt
  puts "\nStopping..."
ensure
  send_sync(socket, 'Unsubscribe', {
    events: %w[KeyPressed KeyReleased MouseMoved MouseButtonPressed MouseButtonReleased]
  }) rescue nil
  socket.close rescue nil
end
