#!/usr/bin/env ruby
# msgpack_bench.rb — Phase 11: binary (MessagePack) protocol benchmark.
#
# Compares throughput of fire-and-forget draw commands sent in JSON mode vs
# binary MessagePack mode, both with batch flushing.
#
# The test scene draws 200 rectangles per frame repeatedly for N seconds.
# Throughput is measured as commands/second sent to the server.
#
# Usage:  ruby examples/msgpack_bench.rb
#         ruby examples/msgpack_bench.rb 7878   # custom port
#         ruby examples/msgpack_bench.rb 7878 5 # 5-second runs
#
# Expected: MessagePack frames are ~25–40% smaller than JSON for typical draw
# commands, so throughput should be higher.

$LOAD_PATH.unshift File.expand_path('../lib', __dir__)
require 'raylib_client'

PORT     = (ARGV[0] || 7878).to_i
DURATION = (ARGV[1] || 3).to_f
RECTS    = 200  # draw commands per iteration

def run_bench(label, rls)
  # Use display lists so commands are handled in the connection thread and
  # don't saturate the main-thread command queue.
  iterations = 0
  t_start = Time.now
  t_end   = t_start + DURATION

  until Time.now >= t_end
    rls.display_list('bench') do
      rls.batch do
        rls.clear_background(color: [12, 14, 22, 255])
        RECTS.times do |i|
          x = i * 4
          y = (i * 3) % 600
          rls.draw_rectangle(posX: x, posY: y, width: 3, height: 3,
                             color: [i % 255, (i * 2) % 255, 200, 255])
        end
      end
    end
    iterations += 1
  end

  elapsed     = Time.now - t_start
  total_cmds  = iterations * (RECTS + 1)
  cmds_per_sec = (total_cmds / elapsed).round
  printf "  %-14s  %8d cmd/s  (%d iters, %d cmds in %.2fs)\n",
         label, cmds_per_sec, iterations, total_cmds, elapsed
  cmds_per_sec
end

puts "=== MessagePack benchmark (#{DURATION}s per mode, #{RECTS} cmds/iter) ==="
puts ""

# JSON mode
puts "Starting JSON mode..."
rls_json = RaylibClient.connect(port: PORT)
json_rate = run_bench("JSON", rls_json)
rls_json.close

sleep 0.1

# Binary mode
puts "Starting binary (MessagePack) mode..."
rls_bin = RaylibClient.connect(port: PORT)
rls_bin.enable_binary_mode!
bin_rate = run_bench("MessagePack", rls_bin)
rls_bin.close

puts ""
ratio = bin_rate.to_f / json_rate
puts "  Speedup: %.2fx  (%s)" % [
  ratio,
  ratio >= 1.0 ? "#{((ratio - 1) * 100).round}% faster" : "#{((1 - ratio) * 100).round}% slower"
]
puts ""
puts "Binary mode is working if the server accepted both connections without errors."
