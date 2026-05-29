# frozen_string_literal: true

require 'socket'
require 'json'
require 'base64'
require 'securerandom'
require 'io/wait'
require 'set'

# Ruby client library for raylib_server.
#
# Connects to a running raylib_server over TCP and exposes every wire-protocol
# command as a Ruby method.  Command names are converted from snake_case to the
# server's CamelCase automatically:
#
#   client.draw_circle(centerX: 400, centerY: 300, radius: 50, color: 'RED')
#   client.clear_background(color: 'RAYWHITE')
#   width = client.get_screen_width['width']
#
# Sync commands (Load*, Get*, Is*, Measure*, Subscribe, …) block and return
# the server's result hash.  All other commands are fire-and-forget (return nil).
#
# Example — retained-mode animation:
#
#   require_relative '../lib/raylib_client'
#
#   rls = RaylibClient.connect
#
#   rls.display_list('main') do
#     rls.clear_background(color: 'RAYWHITE')
#     rls.draw_circle_v(center: [400, 300], radius: 50, color: 'RED')
#   end
#
#   loop do
#     rls.draw_fps(posX: 10, posY: 10)
#     sleep 1.0 / 60
#   end
#
#   rls.close
class RaylibClient
  VERSION = '1.0.0'

  # Commands that require a synchronous response from the server.
  # The client sends these with a unique id and blocks reading one response line.
  SYNC_CMDS = Set.new(%w[
    LoadTexture LoadRenderTexture LoadFont LoadFontEx
    LoadSound LoadMusicStream LoadShader
    UnloadTexture UnloadRenderTexture UnloadFont
    UnloadSound UnloadMusicStream UnloadShader
    UploadTexture UploadTextureRaw UploadFont UploadSound UploadMusic UploadShader
    BeginUpload UploadChunk CommitUpload AbortUpload ListUploads
    MeasureText MeasureTextEx
    GetScreenWidth GetScreenHeight GetRenderWidth GetRenderHeight
    GetWindowPosition GetWindowScaleDPI
    IsWindowReady IsWindowFullscreen IsWindowHidden IsWindowMinimized
    IsWindowMaximized IsWindowFocused IsWindowResized
    GetFPS GetFrameTime GetTime
    GetMonitorCount GetCurrentMonitor GetMonitorWidth GetMonitorHeight GetMonitorName
    IsKeyPressed IsKeyDown IsKeyReleased IsKeyUp GetKeyPressed GetCharPressed
    IsMouseButtonPressed IsMouseButtonDown IsMouseButtonReleased
    GetMousePosition GetMouseDelta GetMouseWheelMove GetMouseWheelMoveV
    IsGamepadAvailable IsGamepadButtonPressed IsGamepadButtonDown GetGamepadAxisMovement
    GetTouchPointCount GetTouchPosition GetGestureDetected
    ListHandles GetTextureInfo GetFontInfo
    GetDisplayLists GetDisplayListCommands GetServerInfo
    Subscribe Unsubscribe
    LoadModel
    TimerCreate TimerOnce ListTimers
  ]).freeze

  # Post-processing rules for the snake_case → CamelCase conversion.
  # Handles abbreviations and digit-suffixed words that String#capitalize mangles.
  CMD_FIXES = {
    'Fps'    => 'FPS',
    'Dpi'    => 'DPI',
    'Mode2d' => 'Mode2D',
    'Mode3d' => 'Mode3D',
    'Npatch' => 'NPatch',
  }.freeze

  # Raised when the server returns ok:false or the connection drops.
  class Error < StandardError; end

  # Connect to a running raylib_server and return a new client.
  #
  # @param host [String]   hostname (default 'localhost')
  # @param port [Integer]  TCP port  (default 7878)
  # @return [RaylibClient]
  def self.connect(host: 'localhost', port: 7878)
    new(TCPSocket.new(host, port))
  end

  # Initialize with an existing socket (or a test double).
  # Prefer RaylibClient.connect for normal use.
  def initialize(socket)
    @socket = socket
    @batch  = nil    # Array of encoded strings while inside a batch block
    @binary = false  # true after enable_binary_mode!
  end

  # ---------------------------------------------------------------------------
  # Low-level command API
  # ---------------------------------------------------------------------------

  # Send a fire-and-forget command.  No id is added; the server sends no reply.
  # Inside a batch block the JSON is buffered and sent all at once on exit.
  #
  # @param name [String]    wire protocol command name (e.g. 'DrawCircle')
  # @param args [Hash, nil] command arguments matching the wire protocol keys
  # @return [nil]
  def cmd(name, args = nil)
    if @binary
      frame = mp_frame({ 'cmd' => name }.tap { |h| h['args'] = args if args })
      if @batch
        @batch << frame
      else
        @socket.write(frame)
      end
    else
      msg = { cmd: name }
      msg[:args] = args if args
      json = JSON.generate(msg)
      if @batch
        @batch << json
      else
        @socket.puts json
      end
    end
    nil
  end

  # Send a synchronous command and return the server's result hash.
  # Raises RaylibClient::Error on server error or disconnection.
  # Always sent immediately, even inside a batch block.
  #
  # @param name [String]    wire protocol command name (e.g. 'GetScreenWidth')
  # @param args [Hash, nil] command arguments
  # @return [Hash, nil]     the result object from the server response
  def sync(name, args = nil)
    id = "r#{SecureRandom.hex(3)}"
    if @binary
      msg = { 'id' => id, 'cmd' => name }
      msg['args'] = args if args
      @socket.write(mp_frame(msg))
      resp = mp_read_response
    else
      msg = { id: id, cmd: name }
      msg[:args] = args if args
      @socket.puts JSON.generate(msg)
      line = @socket.gets or raise Error, 'server disconnected'
      resp = JSON.parse(line)
    end
    raise Error, "#{name} failed: #{resp['error']}" unless resp['ok']
    resp['result']
  end

  # ---------------------------------------------------------------------------
  # Batch mode
  # ---------------------------------------------------------------------------

  # Buffer all fire-and-forget commands issued in the block and flush them to
  # the socket in a single write call.  This reduces TCP round-trips when
  # sending many draw commands per frame.
  #
  # Sync commands inside the block are still sent immediately.
  # The buffer is flushed even if the block raises an exception.
  #
  # @yield block in which cmd() calls are buffered
  def batch(&block)
    @batch = []
    block.call
  ensure
    data   = @batch
    @batch = nil
    unless data.empty?
      if @binary
        @socket.write(data.join.b)
      else
        @socket.write(data.join("\n") + "\n")
      end
    end
  end

  # ---------------------------------------------------------------------------
  # Display list DSL
  # ---------------------------------------------------------------------------

  # Record draw commands into a named display list.
  # Sends DisplayListBegin before yielding and DisplayListEnd afterward,
  # even if the block raises.
  #
  # @param name [String] display list name
  # @yield block containing draw commands to record
  def display_list(name)
    cmd('DisplayListBegin', { name: name })
    begin
      yield
    ensure
      cmd('DisplayListEnd')
    end
  end

  # ---------------------------------------------------------------------------
  # Event streaming
  # ---------------------------------------------------------------------------

  # Subscribe to server-push events.
  # @param events [Array<String>, *String] event names
  # @return [Hash, nil] server result
  def subscribe(*events)
    sync('Subscribe', { events: events.flatten })
  end

  # Unsubscribe from server-push events.
  # @param events [Array<String>, *String] event names
  # @return [Hash, nil] server result
  def unsubscribe(*events)
    sync('Unsubscribe', { events: events.flatten })
  end

  # Non-blocking read of all immediately available server-pushed event lines.
  # Returns an array of parsed event hashes; empty array if none are ready.
  #
  # @return [Array<Hash>]
  def drain_events
    events = []
    while @socket.ready?
      line = @socket.gets
      break unless line
      parsed = JSON.parse(line) rescue nil
      events << parsed if parsed
    end
    events
  end

  # ---------------------------------------------------------------------------
  # Upload helpers
  # ---------------------------------------------------------------------------

  DEFAULT_CHUNK_SIZE = 48 * 1024  # 48 KiB raw → ~64 KiB base64 per chunk

  # Upload a file to the server using the chunked upload protocol.
  # Returns the resource handle integer assigned by the server.
  #
  # @param path          [String]  local file path
  # @param file_type     [String]  file extension hint (e.g. '.png', 'ogg')
  # @param resource_type [String]  'texture', 'font', 'sound', or 'music'
  # @param chunk_size    [Integer] raw bytes per chunk (default 48 KiB)
  # @return [Integer] resource handle
  def chunked_upload(path, file_type:, resource_type:, chunk_size: DEFAULT_CHUNK_SIZE)
    upload_data(File.binread(path),
                file_type:     file_type,
                resource_type: resource_type,
                name:          File.basename(path),
                chunk_size:    chunk_size)
  end

  # Upload raw binary data using the chunked upload protocol.
  # Returns the resource handle integer assigned by the server.
  #
  # @param data          [String]  binary payload (encoding: BINARY / ASCII-8BIT)
  # @param file_type     [String]  file extension hint
  # @param resource_type [String]  'texture', 'font', 'sound', or 'music'
  # @param name          [String]  logical name for the upload (informational)
  # @param chunk_size    [Integer] raw bytes per chunk
  # @return [Integer] resource handle
  def upload_data(data, file_type:, resource_type:, name: 'upload', chunk_size: DEFAULT_CHUNK_SIZE)
    total     = data.bytesize
    result    = sync('BeginUpload', { name: name, fileType: file_type, totalBytes: total })
    upload_id = result['uploadId']

    seq = 0
    pos = 0
    while pos < total
      chunk = data.byteslice(pos, chunk_size)
      sync('UploadChunk', { uploadId: upload_id, seq: seq,
                             data: Base64.strict_encode64(chunk) })
      pos += chunk.bytesize
      seq += 1
    end

    result = sync('CommitUpload', { uploadId: upload_id, type: resource_type })
    result['handle']
  end

  # ---------------------------------------------------------------------------
  # Binary (MessagePack) mode
  # ---------------------------------------------------------------------------

  # Switch this connection to MessagePack binary framing.
  # After this call, all outgoing commands are encoded as MessagePack with a
  # 4-byte little-endian length prefix.  Responses from the server are also
  # MessagePack-framed and decoded automatically.
  #
  # The msgpack gem is required:  gem install msgpack
  #
  # @raise [LoadError] if the msgpack gem is not installed
  # @raise [RaylibClient::Error] if the server rejects the switch
  def enable_binary_mode!
    require_relative 'msgpack_mini'
    # Send the plain-text switch line while still in JSON mode.
    @socket.puts 'BINARY'
    # Read back the server's JSON ACK.
    resp = JSON.parse(@socket.gets || raise(Error, 'server disconnected'))
    raise Error, "binary switch failed: #{resp['error']}" unless resp['ok']
    @binary = true
  end

  # True if the connection is in binary (MessagePack) mode.
  def binary_mode?
    @binary
  end

  # ---------------------------------------------------------------------------
  # Connection management
  # ---------------------------------------------------------------------------

  # Close the underlying socket.
  def close
    @socket.close rescue nil
  end

  # True if the socket has not been closed.
  def connected?
    !@socket.closed?
  end

  # ---------------------------------------------------------------------------
  # method_missing: snake_case → CamelCase command dispatch
  #
  # Any unknown method name is converted to a CamelCase command name via
  # RaylibClient.snake_to_cmd and dispatched to cmd() or sync() depending on
  # whether the command name appears in SYNC_CMDS.
  #
  # Arguments may be passed as keyword args or as a single hash:
  #   client.draw_circle(centerX: 400, centerY: 300, radius: 50, color: 'RED')
  #   client.draw_circle({ centerX: 400, centerY: 300, radius: 50, color: 'RED' })
  #
  # Argument keys must match the wire protocol exactly (camelCase, e.g. centerX).
  # ---------------------------------------------------------------------------

  def method_missing(name, *args, **kwargs)
    cmd_name    = self.class.snake_to_cmd(name.to_s)
    actual_args = if !kwargs.empty?
      kwargs
    elsif args.length == 1 && args.first.is_a?(Hash)
      args.first
    end

    if SYNC_CMDS.include?(cmd_name)
      sync(cmd_name, actual_args)
    else
      cmd(cmd_name, actual_args)
    end
  end

  def respond_to_missing?(_name, _include_private = false)
    true
  end

  # ---------------------------------------------------------------------------
  # Utility
  # ---------------------------------------------------------------------------

  # Convert a snake_case string to the CamelCase raylib command name.
  # Handles known abbreviations: FPS, 2D, 3D, NPatch.
  #
  # @param snake [String] snake_case name (e.g. 'draw_fps', 'begin_mode2d')
  # @return [String] CamelCase command name (e.g. 'DrawFPS', 'BeginMode2D')
  def self.snake_to_cmd(snake)
    result = snake.to_s.split('_').map(&:capitalize).join
    CMD_FIXES.each { |from, to| result = result.gsub(from, to) }
    result
  end

  private

  # Encode a Hash as a MessagePack frame (4-byte LE length + msgpack payload).
  def mp_frame(hash)
    payload = MsgpackMini.encode(hash)
    n = payload.bytesize
    [n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF, (n >> 24) & 0xFF].pack('C4') + payload
  end

  # Read one binary-mode response (4-byte LE length + msgpack payload).
  def mp_read_response
    hdr = @socket.read(4) or raise Error, 'server disconnected'
    len = hdr.unpack1('V')  # little-endian uint32
    payload = @socket.read(len) or raise Error, 'server disconnected'
    MsgpackMini.decode(payload)
  end
end
