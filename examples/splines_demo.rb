#!/usr/bin/env ruby
# Spline algorithms comparison.
#
# Draws the same set of control points through all five raylib spline
# algorithms side-by-side so the differences in curve shape are clear.
#
# The control points animate slowly so you can see how each algorithm
# responds to changing input.
#
# Usage:  ruby examples/splines_demo.rb
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS
W     = 900
H     = 600

socket = TCPSocket.new('localhost', PORT)

def send_cmd(socket, cmd, args = nil)
  msg = { cmd: cmd }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
end

send_cmd(socket, 'SetWindowSize',  { width: W, height: H })
send_cmd(socket, 'SetWindowTitle', { title: 'Spline algorithms — comparison' })

# ---------------------------------------------------------------------------
# Spline definitions
# ---------------------------------------------------------------------------

SPLINES = [
  { cmd: 'DrawSplineLinear',          name: 'Linear',          color: 'RED',    min_pts: 2 },
  { cmd: 'DrawSplineBasis',           name: 'Basis (B-spline)',color: 'ORANGE', min_pts: 4 },
  { cmd: 'DrawSplineCatmullRom',      name: 'Catmull-Rom',     color: 'GREEN',  min_pts: 4 },
  { cmd: 'DrawSplineBezierQuadratic', name: 'Bezier Quadratic',color: 'SKYBLUE',min_pts: 3 },
  { cmd: 'DrawSplineBezierCubic',     name: 'Bezier Cubic',    color: 'PURPLE', min_pts: 4 },
].freeze

COLS      = SPLINES.size
COL_W     = W / COLS
PAD       = 14
LABEL_H   = 28
DRAW_Y    = LABEL_H + PAD
DRAW_H    = H - DRAW_Y - PAD
N_CTRL    = 6  # number of control points

# Base control point positions (evenly spaced x, animated y)
BASE_X = (0...N_CTRL).map { |i| (i + 0.5) * W / N_CTRL }

puts "Splines demo running — press Ctrl+C to exit."

t = 0.0
begin
  loop do
    t0 = Time.now

    # Animate control points — each one has a slightly different phase
    ctrl_pts = BASE_X.each_with_index.map do |x, i|
      phase = i * Math::PI / 2.5
      y = H / 2.0 + (DRAW_H * 0.35) * Math.sin(t * 0.8 + phase)
      [x.to_i, y.to_i]
    end

    frame = []
    frame << JSON.generate({ cmd: 'ClearBackground', args: { color: [18, 18, 28, 255] } })

    # Column backgrounds and labels
    SPLINES.each_with_index do |spline, col|
      lx = col * COL_W
      bg_color = col.even? ? [22, 22, 34, 255] : [18, 18, 28, 255]
      frame << JSON.generate({ cmd: 'DrawRectangle',
                               args: { posX: lx, posY: 0, width: COL_W, height: H,
                                       color: bg_color } })
      frame << JSON.generate({ cmd: 'DrawText',
                               args: { text: spline[:name],
                                       posX: lx + 6, posY: 6,
                                       fontSize: 13, color: spline[:color] } })
    end

    # Vertical separator lines between columns
    (1...COLS).each do |col|
      frame << JSON.generate({ cmd: 'DrawLine',
                               args: { startPosX: col * COL_W, startPosY: 0,
                                       endPosX:   col * COL_W, endPosY:   H,
                                       color: [40, 40, 55, 255] } })
    end

    # Draw each spline through the full set of control points
    SPLINES.each do |spline|
      pts = ctrl_pts.first(N_CTRL)
      pts = pts.first(spline[:min_pts]) if pts.size < spline[:min_pts]
      next if pts.size < spline[:min_pts]

      frame << JSON.generate({ cmd: spline[:cmd],
                               args: { points: pts, thick: 2.5, color: spline[:color] } })
    end

    # Control point dots + connecting ghost line (shown once, over everything)
    ghost = ctrl_pts.each_cons(2).map { |a, b|
      JSON.generate({ cmd: 'DrawLineEx',
                      args: { startPos: a, endPos: b, thick: 1.0,
                              color: [80, 80, 100, 180] } })
    }
    frame.concat(ghost)

    ctrl_pts.each_with_index do |pt, i|
      frame << JSON.generate({ cmd: 'DrawCircleV',
                               args: { center: pt, radius: 5, color: 'WHITE' } })
      frame << JSON.generate({ cmd: 'DrawText',
                               args: { text: "P#{i + 1}", posX: pt[0] + 7, posY: pt[1] - 8,
                                       fontSize: 11, color: 'LIGHTGRAY' } })
    end

    frame << JSON.generate({ cmd: 'DrawFPS', args: { posX: W - 80, posY: H - 24 } })

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
