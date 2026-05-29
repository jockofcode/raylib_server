#!/usr/bin/env ruby
# Library demo — shows the RaylibClient library API.
#
# Demonstrates:
#   1. RaylibClient.connect        — connect to the server
#   2. method_missing dispatch     — snake_case Ruby methods for every command
#   3. display_list DSL            — retained-mode drawing with a block
#   4. batch mode                  — flush many commands in one write
#   5. sync introspection          — get_screen_width, get_server_info
#   6. event subscription          — subscribe / drain_events
#
# Usage:  ruby examples/library_demo.rb
#         ruby examples/library_demo.rb 7878   # custom port
#
# Requires: a running raylib_server on localhost:7878.

$LOAD_PATH.unshift File.expand_path('../lib', __dir__)
require 'raylib_client'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS
W     = 800
H     = 600

rls = RaylibClient.connect(port: PORT)

rls.set_window_size(width: W, height: H)
rls.set_window_title(title: 'RaylibClient Library Demo')

# Query server info using a sync command via method_missing.
info = rls.get_server_info
puts "Connected — server v#{info['version']} on port #{info['port']}"

width  = rls.get_screen_width['width']
height = rls.get_screen_height['height']
puts "Screen: #{width}×#{height}"

# Subscribe to keyboard and mouse events.
rls.subscribe('KeyPressed', 'MouseButtonPressed', 'MouseMoved')

# Build the static background display list once.
rls.display_list('background') do
  rls.clear_background(color: [18, 18, 28, 255])

  # Decorative grid.
  (0..6).each do |col|
    (0..4).each do |row|
      rls.draw_circle_v(center: [80 + col * 110, 80 + row * 110],
                        radius: 4, color: [60, 60, 100, 200])
    end
  end

  # Border.
  rls.draw_rectangle_lines_ex(rec: [6, 6, W - 12, H - 12],
                              lineThick: 2.0, color: [80, 80, 120, 255])

  rls.draw_text(text: 'RaylibClient Ruby Library Demo',
                posX: 20, posY: 18, fontSize: 22, color: 'RAYWHITE')
  rls.draw_text(text: 'Retained background + animated scene + event log',
                posX: 20, posY: 46, fontSize: 14, color: [140, 140, 200, 255])
end

rls.display_list_set_order(names: %w[background scene])

event_log  = []
mouse_x    = W / 2
mouse_y    = H / 2
angle      = 0.0

puts "Demo running — interact with the window. Press Ctrl+C to stop."

begin
  loop do
    t0 = Time.now

    # Collect events pushed by the server.
    rls.drain_events.each do |ev|
      case ev['event']
      when 'MouseMoved'
        mouse_x = ev['x'].to_i
        mouse_y = ev['y'].to_i
        next
      when 'KeyPressed'
        event_log.unshift("Key #{ev['key']} pressed  (frame #{ev['frame']})")
      when 'MouseButtonPressed'
        event_log.unshift("Mouse btn #{ev['button']} at (#{ev['x'].to_i}, #{ev['y'].to_i})")
      end
      event_log = event_log.first(6)
    end

    # Update animated scene list each frame — batch all commands in one write.
    rls.batch do
      rls.display_list('scene') do
        # Orbiting ball.
        bx = (W / 2.0 + Math.cos(angle) * 200).round
        by = (H / 2.0 + Math.sin(angle) * 120).round
        rls.draw_circle_v(center: [bx + 4, by + 4], radius: 22, color: [0, 0, 0, 60])
        rls.draw_circle_v(center: [bx, by], radius: 22, color: 'SKYBLUE')
        rls.draw_circle_v(center: [bx - 6, by - 6], radius: 6,
                          color: [200, 230, 255, 180])

        # Mouse cursor indicator.
        rls.draw_circle_v(center: [mouse_x, mouse_y], radius: 7,
                          color: [100, 220, 100, 220])
        rls.draw_circle_lines(centerX: mouse_x, centerY: mouse_y,
                              radius: 14, color: [100, 220, 100, 120])

        # Event log.
        rls.draw_text(text: 'Recent events:', posX: 20, posY: 110,
                      fontSize: 13, color: [140, 140, 180, 255])
        event_log.each_with_index do |entry, i|
          alpha = [255 - i * 30, 60].max
          rls.draw_text(text: entry, posX: 28, posY: 130 + i * 22,
                        fontSize: 14, color: [200, 200, 255, alpha])
        end

        rls.draw_fps(posX: W - 80, posY: 10)
      end
    end

    angle += 0.03

    elapsed = Time.now - t0
    sleep([FRAME - elapsed, 0].max)
  rescue Errno::EPIPE, Errno::ECONNRESET
    puts 'Server disconnected.'
    break
  end
rescue Interrupt
  puts "\nStopping..."
ensure
  rls.unsubscribe('KeyPressed', 'MouseButtonPressed', 'MouseMoved') rescue nil
  rls.display_list_delete(name: 'scene')      rescue nil
  rls.display_list_delete(name: 'background') rescue nil
  rls.close
end
