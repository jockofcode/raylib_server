#!/usr/bin/env ruby
# Timer Demo — Phase 9: Animation timers.
#
# Demonstrates:
#   TimerCreate  (repeating interval timer)
#   TimerOnce    (one-shot delay timer)
#   TimerDelete  (cancel a timer)
#   ListTimers   (introspect active timers)
#   Subscribe('TimerFired') + drain_events
#
# Visualisation:
#   A circle pulses on each tick of a fast timer (0.2 s).
#   A bar grows from a slow timer (1 s).
#   A one-shot timer fires after 5 s and flashes the screen.
#   Press D to delete / re-create the fast timer.
#   Press L to log all active timers to stdout.
#
# Usage:  ruby examples/timer_demo.rb
#         ruby examples/timer_demo.rb 7878   # custom port

$LOAD_PATH.unshift File.expand_path('../lib', __dir__)
require 'raylib_client'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS
W     = 800
H     = 600

KEY_D = 68
KEY_L = 76

rls = RaylibClient.connect(port: PORT)
rls.set_window_size(width: W, height: H)
rls.set_window_title(title: 'Timer Demo — Phase 9')
rls.set_target_fps(fps: 60)

# Create timers
fast_id  = rls.timer_create(name: 'fast',  interval: 0.2,  repeat: true)['timerId']
slow_id  = rls.timer_create(name: 'slow',  interval: 1.0,  repeat: true)['timerId']
once_id  = rls.timer_once(  name: 'flash', delay:    5.0)['timerId']

puts "Timers created:"
puts "  fast  (0.2s repeat): #{fast_id}"
puts "  slow  (1.0s repeat): #{slow_id}"
puts "  flash (5.0s once):   #{once_id}"

rls.subscribe('TimerFired', 'KeyPressed')

fast_pulse  = 0.0   # 0–1 brightness from last fast tick
slow_count  = 0     # increments on each slow tick
flash_alpha = 0     # 0–255 for flash overlay
fast_alive  = true

puts "Timer demo running.  D=delete/recreate fast timer  L=list timers  Ctrl+C=quit"

begin
  loop do
    t0 = Time.now

    rls.drain_events.each do |ev|
      case ev['event']
      when 'TimerFired'
        case ev['timerId']
        when fast_id  then fast_pulse  = 1.0
        when slow_id  then slow_count += 1
        when once_id  then flash_alpha = 200
        end
      when 'KeyPressed'
        case ev['key']
        when KEY_D
          if fast_alive
            rls.timer_delete(timerId: fast_id)
            fast_alive = false
            puts "Fast timer deleted."
          else
            fast_id   = rls.timer_create(name: 'fast', interval: 0.2, repeat: true)['timerId']
            fast_alive = true
            puts "Fast timer recreated: #{fast_id}"
          end
        when KEY_L
          timers = rls.list_timers['timers']
          puts "Active timers (#{timers.length}):"
          timers.each do |t|
            puts "  #{t['timerId']}  name=#{t['name']}  interval=#{t['interval']}s  repeat=#{t['repeat']}"
          end
        end
      end
    end

    # Decay pulse and flash each frame
    fast_pulse  = [fast_pulse  - FRAME * 4.0, 0.0].max
    flash_alpha = [flash_alpha - FRAME * 80.0, 0.0].max.round

    bar_w = [(slow_count % 11) * 60, W - 60].min

    rls.batch do
      rls.clear_background(color: [12, 14, 22, 255])

      rls.draw_text(text: 'Timer Demo — Phase 9', posX: 20, posY: 14,
                    fontSize: 24, color: 'GOLD')
      rls.draw_text(text: 'D = delete/recreate fast timer   L = list timers',
                    posX: 20, posY: 44, fontSize: 13, color: [160, 160, 200, 255])
      rls.draw_rectangle_lines_ex(rec: [6, 6, W - 12, H - 12],
                                  lineThick: 1.5, color: [60, 60, 100, 200])

      # Fast timer pulse circle
      pulse_r = (200 * fast_pulse).round
      rls.draw_text(text: "Fast timer (0.2 s repeat) — #{fast_alive ? 'ACTIVE' : 'DELETED'}",
                    posX: 20, posY: 80, fontSize: 15, color: fast_alive ? 'SKYBLUE' : 'GRAY')
      rls.draw_circle(centerX: W / 2, centerY: 150, radius: 40.0,
                      color: [pulse_r, 120, 255, 200])
      rls.draw_circle_lines(centerX: W / 2, centerY: 150, radius: 40.0, color: 'SKYBLUE')
      rls.draw_text(text: 'pulse', posX: W / 2 - 20, posY: 143, fontSize: 14, color: 'WHITE')

      # Slow timer bar
      rls.draw_text(text: "Slow timer (1.0 s repeat) — ticks: #{slow_count}",
                    posX: 20, posY: 220, fontSize: 15, color: 'LIME')
      rls.draw_rectangle(posX: 30, posY: 248, width: W - 60, height: 22,
                         color: [30, 40, 30, 255])
      rls.draw_rectangle(posX: 30, posY: 248, width: bar_w, height: 22,
                         color: [60, 200, 80, 255]) if bar_w > 0
      rls.draw_rectangle_lines_ex(rec: [30, 248, W - 60, 22],
                                  lineThick: 1.0, color: 'LIME')

      # One-shot flash banner
      if once_id && flash_alpha > 0
        rls.draw_rectangle(posX: 0, posY: H / 2 - 30, width: W, height: 60,
                           color: [255, 200, 60, flash_alpha])
        rls.draw_text(text: "ONE-SHOT TIMER FIRED!", posX: W / 2 - 130, posY: H / 2 - 14,
                      fontSize: 26, color: [40, 20, 0, 255])
      end

      # Once timer status
      once_text = once_id ? "One-shot timer (5.0 s) — #{flash_alpha > 0 ? 'FIRED!' : 'waiting...'}"
                           : "One-shot timer — expired"
      rls.draw_text(text: once_text, posX: 20, posY: 300, fontSize: 15,
                    color: flash_alpha > 0 ? 'GOLD' : 'LIGHTGRAY')

      # ListTimers hint
      rls.draw_text(text: 'Press L to print ListTimers to stdout',
                    posX: 20, posY: H - 50, fontSize: 14, color: [120, 120, 160, 200])

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
  rls.unsubscribe('TimerFired', 'KeyPressed') rescue nil
  rls.timer_delete(timerId: fast_id)  rescue nil
  rls.timer_delete(timerId: slow_id)  rescue nil
  rls.timer_delete(timerId: once_id)  rescue nil
  rls.close
end
