#!/usr/bin/env ruby
# Input demo — Phase 5 introspection for keyboard, mouse, and gamepad.
#
# Demonstrates:
#   IsKeyDown, GetKeyPressed, GetCharPressed
#   IsMouseButtonDown, GetMousePosition, GetMouseDelta, GetMouseWheelMove
#   IsGamepadAvailable, IsGamepadButtonDown, GetGamepadAxisMovement
#   GetTouchPointCount
#
# Polls all input state with sync commands each frame.
#
# Usage:  ruby examples/input_demo.rb
#         ruby examples/input_demo.rb 7878   # custom port
#
# Requires: a running raylib_server on localhost:7878.

$LOAD_PATH.unshift File.expand_path('../lib', __dir__)
require 'raylib_client'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 20.0   # modest rate — each frame fires many sync round-trips
FRAME = 1.0 / FPS
W     = 800
H     = 600

rls = RaylibClient.connect(port: PORT)
rls.set_window_size(width: W, height: H)
rls.set_window_title(title: 'Input Demo — Phase 5 Introspection')

# Keyboard keys to monitor: label => key code
WATCH_KEYS = {
  'W' => 87, 'A' => 65, 'S' => 83, 'D' => 68,
  'SPACE' => 32, 'SHIFT' => 340, 'ENTER' => 257, 'ESC' => 256
}.freeze

# Gamepad button labels (Xbox layout)
GAMEPAD_BTNS = { 0 => 'A', 1 => 'B', 2 => 'X', 3 => 'Y',
                 4 => 'LB', 5 => 'RB', 6 => 'SEL', 7 => 'START' }.freeze

GAMEPAD_AXIS_LABELS = { 0 => 'LX', 1 => 'LY', 2 => 'RX', 3 => 'RY' }.freeze

wheel_accum = 0.0
char_log    = []   # recent chars from GetCharPressed

puts "Input demo running — interact with the window. Ctrl+C to stop."

begin
  loop do
    t0 = Time.now

    # Poll all input state with Phase 5 sync commands.
    mouse_pos   = rls.get_mouse_position
    mouse_delta = rls.get_mouse_delta
    wheel       = rls.get_mouse_wheel_move['move']
    wheel_accum = (wheel_accum + wheel).round(1)

    mouse_buttons = 3.times.map { |b| rls.is_mouse_button_down(button: b)['down'] }

    key_states = WATCH_KEYS.transform_values { |code| rls.is_key_down(key: code)['down'] }

    # Drain queued key presses for the char log.
    loop do
      ch = rls.get_char_pressed['char']
      break if ch == 0
      char_str = begin; ch.chr(Encoding::UTF_8); rescue; '?'; end
      char_log.unshift(char_str)
      char_log = char_log.first(8)
    end

    touch_count = rls.get_touch_point_count['count']
    gamepad_ok  = rls.is_gamepad_available(gamepad: 0)['available']
    gp_axes     = {}
    gp_btns     = {}
    if gamepad_ok
      GAMEPAD_AXIS_LABELS.each_key do |a|
        gp_axes[a] = rls.get_gamepad_axis_movement(gamepad: 0, axis: a)['movement'].round(3)
      end
      GAMEPAD_BTNS.each_key do |b|
        gp_btns[b] = rls.is_gamepad_button_down(gamepad: 0, button: b)['down']
      end
    end

    mx = mouse_pos['x'].round(1)
    my = mouse_pos['y'].round(1)
    dx = mouse_delta['x'].round(1)
    dy = mouse_delta['y'].round(1)

    # --- Draw ---
    rls.batch do
      rls.clear_background(color: [12, 14, 22, 255])
      rls.draw_text(text: 'Input Demo — Phase 5 Introspection',
                    posX: 20, posY: 12, fontSize: 22, color: 'GOLD')
      rls.draw_rectangle_lines_ex(rec: [6, 6, W - 12, H - 12],
                                  lineThick: 1.5, color: [60, 60, 100, 200])

      # ---- Mouse section -----------------------------------------------
      rls.draw_text(text: 'MOUSE', posX: 20, posY: 50,
                    fontSize: 15, color: 'SKYBLUE')
      rls.draw_text(text: "GetMousePosition  : (#{mx}, #{my})",
                    posX: 20, posY: 70, fontSize: 13, color: 'LIGHTGRAY')
      rls.draw_text(text: "GetMouseDelta     : (#{dx}, #{dy})",
                    posX: 20, posY: 86, fontSize: 13, color: 'LIGHTGRAY')
      rls.draw_text(text: "GetMouseWheelMove : #{wheel_accum} (accumulated)",
                    posX: 20, posY: 102, fontSize: 13, color: 'LIGHTGRAY')
      rls.draw_text(text: "GetTouchPointCount: #{touch_count}",
                    posX: 20, posY: 118, fontSize: 13, color: 'LIGHTGRAY')

      # Mouse button indicators
      ['LEFT', 'MID', 'RIGHT'].each_with_index do |label, i|
        col = mouse_buttons[i] ? [80, 200, 120, 255] : [45, 45, 70, 255]
        rls.draw_rectangle(posX: 20 + i * 86, posY: 138, width: 78, height: 26, color: col)
        rls.draw_text(text: label, posX: 28 + i * 86, posY: 145,
                      fontSize: 12, color: mouse_buttons[i] ? 'WHITE' : [130, 130, 160, 255])
      end

      # Cursor dot
      rls.draw_circle_v(center: [mx.round, my.round], radius: 5,
                        color: [100, 220, 100, 200])
      rls.draw_circle_lines(centerX: mx.round, centerY: my.round, radius: 12,
                            color: [100, 220, 100, 100])

      # ---- Keyboard section --------------------------------------------
      rls.draw_text(text: 'KEYBOARD', posX: 20, posY: 182,
                    fontSize: 15, color: 'SKYBLUE')

      WATCH_KEYS.each_with_index do |(label, _), i|
        col = key_states[label] ? [80, 200, 120, 255] : [45, 45, 70, 255]
        x   = 20 + (i % 4) * 96
        y   = 202 + (i / 4) * 36
        rls.draw_rectangle(posX: x, posY: y, width: 86, height: 28, color: col)
        rls.draw_text(text: label, posX: x + 7, posY: y + 7,
                      fontSize: 13, color: key_states[label] ? 'WHITE' : [130, 130, 160, 255])
      end

      rls.draw_text(text: "GetCharPressed log: #{char_log.join(' ')}",
                    posX: 20, posY: 282, fontSize: 13, color: [160, 160, 200, 255])

      # ---- Gamepad section ---------------------------------------------
      rls.draw_text(text: 'GAMEPAD 0', posX: 20, posY: 314,
                    fontSize: 15, color: 'SKYBLUE')

      if gamepad_ok
        rls.draw_text(text: 'Connected', posX: 140, posY: 314, fontSize: 15,
                      color: [80, 200, 120, 255])

        # Axes
        GAMEPAD_AXIS_LABELS.each do |a, name|
          val   = gp_axes[a] || 0.0
          x_off = a * 120
          rls.draw_text(text: "#{name}: #{format('%+.2f', val)}",
                        posX: 20 + x_off, posY: 336, fontSize: 13, color: 'LIGHTGRAY')
          filled = (val.abs * 50).round
          col    = val >= 0 ? [80, 180, 255, 255] : [255, 120, 80, 255]
          rls.draw_rectangle(posX: 20 + x_off, posY: 354, width: 100, height: 7,
                             color: [40, 40, 60, 255])
          rls.draw_rectangle(posX: 20 + x_off, posY: 354, width: filled, height: 7, color: col)
        end

        # Buttons
        GAMEPAD_BTNS.each do |b, label|
          col = gp_btns[b] ? [80, 200, 120, 255] : [45, 45, 70, 255]
          x   = 20 + b * 74
          rls.draw_rectangle(posX: x, posY: 370, width: 66, height: 26, color: col)
          rls.draw_text(text: label, posX: x + 7, posY: 377,
                        fontSize: 13, color: gp_btns[b] ? 'WHITE' : [130, 130, 160, 255])
        end
      else
        rls.draw_text(text: 'Not connected — plug in a gamepad to see axis/button state.',
                      posX: 140, posY: 314, fontSize: 13, color: [160, 80, 80, 255])
      end

      # ---- Commands reference ------------------------------------------
      rls.draw_text(text: 'Commands: IsKeyDown  GetCharPressed  IsMouseButtonDown  ' \
                           'GetMousePosition  GetMouseDelta  GetMouseWheelMove  ' \
                           'GetTouchPointCount  IsGamepadAvailable  ' \
                           'GetGamepadAxisMovement  IsGamepadButtonDown',
                    posX: 20, posY: H - 32, fontSize: 10, color: [80, 80, 120, 200])

      rls.draw_fps(posX: W - 80, posY: 10)
    end

    elapsed = Time.now - t0
    sleep([FRAME - elapsed, 0].max)
  rescue Errno::EPIPE, Errno::ECONNRESET
    puts 'Server disconnected.'
    break
  end
rescue Interrupt
  puts "\nStopping..."
ensure
  rls.close
end
