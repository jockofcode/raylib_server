#!/usr/bin/env ruby
# Sprite demo — uploads a PNG from the client and animates it on screen.
#
# Demonstrates:
#   1. UploadTexture  — base64-encode a local PNG and send it to the server
#   2. DrawTextureEx  — draw the sprite with position, rotation, and scale
#   3. UnloadTexture  — clean up on exit
#
# Usage:  ruby examples/sprite_demo.rb
#         ruby examples/sprite_demo.rb 7878   # custom port
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'
require 'base64'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS
W     = 800
H     = 600

SPRITE_PATH = File.expand_path('../assets/starship_white_bg_up.png', __FILE__)
CHUNK_SIZE  = 48 * 1024  # 48 KB raw -> ~64 KB base64 per chunk

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

# Upload a file to the server using the chunked upload protocol.
# Returns the resource handle on success, raises on failure.
def chunked_upload(socket, path, file_type, resource_type)
  data  = File.binread(path)
  total = data.bytesize

  resp = send_sync(socket, 'BeginUpload',
                   { name: File.basename(path), fileType: file_type, totalBytes: total })
  raise "BeginUpload failed: #{resp['error']}" unless resp['ok']
  upload_id = resp.dig('result', 'uploadId')

  seq = 0
  pos = 0
  while pos < total
    chunk = data.byteslice(pos, CHUNK_SIZE)
    r = send_sync(socket, 'UploadChunk',
                  { uploadId: upload_id, seq: seq, data: Base64.strict_encode64(chunk) })
    raise "UploadChunk seq=#{seq} failed: #{r['error']}" unless r['ok']
    pos += chunk.bytesize
    seq += 1
  end
  print " (#{seq} chunks)"

  resp = send_sync(socket, 'CommitUpload', { uploadId: upload_id, type: resource_type })
  raise "CommitUpload failed: #{resp['error']}" unless resp['ok']
  resp.dig('result', 'handle')
end

send_cmd(socket, 'SetWindowSize',  { width: W, height: H })
send_cmd(socket, 'SetWindowTitle', { title: 'Sprite Demo' })

print "Uploading sprite..."
tex_handle = chunked_upload(socket, SPRITE_PATH, '.png', 'texture')
puts " handle: #{tex_handle}"

# The sprite faces up, so 0° rotation in raylib points right.
# We offset by -90° so the ship visually points in its direction of travel.
SPRITE_ROTATION_OFFSET = -90.0

angle    = 0.0      # orbit angle (radians)
rotation = 0.0      # sprite facing angle (degrees)
cx       = W / 2.0
cy       = H / 2.0
orb_rx   = 260.0    # orbit x-radius
orb_ry   = 160.0    # orbit y-radius
scale    = 2.0

puts "Sprite demo running — press Ctrl+C to stop."

begin
  loop do
    t0 = Time.now

    # Position on elliptical orbit.
    bx = cx + orb_rx * Math.cos(angle)
    by = cy + orb_ry * Math.sin(angle)

    # Tangent direction → sprite rotation.
    tx = -orb_rx * Math.sin(angle)
    ty =  orb_ry * Math.cos(angle)
    rotation = Math.atan2(ty, tx) * 180.0 / Math::PI + SPRITE_ROTATION_OFFSET

    angle += 0.02

    send_cmd(socket, 'ClearBackground', { color: [10, 10, 30, 255] })

    # Draw decorative orbit ellipse.
    send_cmd(socket, 'DrawEllipseLines', {
      centerX: cx.round, centerY: cy.round,
      radiusH: orb_rx, radiusV: orb_ry,
      color: [80, 80, 120, 255]
    })

    # Draw the sprite centred on its position.
    send_cmd(socket, 'DrawTextureEx', {
      handle:   tex_handle,
      position: [bx.round, by.round],
      rotation: rotation.round(1),
      scale:    scale,
      tint:     'WHITE'
    })

    send_cmd(socket, 'DrawFPS', { posX: 10, posY: 10 })

    elapsed = Time.now - t0
    sleep([FRAME - elapsed, 0].max)
  rescue Errno::EPIPE, Errno::ECONNRESET
    puts "Server disconnected."
    break
  end
rescue Interrupt
  puts "\nStopping..."
ensure
  send_cmd(socket, 'UnloadTexture', { handle: tex_handle }) rescue nil
  socket.close rescue nil
end
