#!/usr/bin/env ruby
# frozen_string_literal: true
# Unit tests for lib/raylib_client.rb
#
# Uses a FakeSocket so no server is required.
# Run with:  ruby tests/test_ruby_client.rb

require 'minitest/autorun'
require 'json'
require_relative '../lib/raylib_client'

# ---------------------------------------------------------------------------
# FakeSocket — captures writes and replays preset response lines.
# ---------------------------------------------------------------------------
class FakeSocket
  attr_reader :lines  # every line written via puts or write, in order

  def initialize(responses = [])
    @lines     = []
    @responses = responses.dup
    @closed    = false
  end

  def puts(s)
    @lines << s.to_s.chomp
  end

  # batch writes arrive as one string with embedded newlines
  def write(s)
    s.to_s.split("\n").each { |l| @lines << l unless l.empty? }
  end

  def gets
    @responses.shift
  end

  def ready?
    !@responses.empty?
  end

  def close
    @closed = true
  end

  def closed?
    @closed
  end

  # Convenience helpers
  def last_cmd = JSON.parse(@lines.last)
  def all_cmds = @lines.map { |l| JSON.parse(l) }
end

# ok_response builds a JSON string the server would send for a sync command.
def ok_response(result = nil)
  JSON.generate({ id: 'r000001', ok: true, result: result })
end

def err_response(msg)
  JSON.generate({ id: 'r000001', ok: false, error: msg })
end

# ===========================================================================
class TestSnakeToCmdConversion < Minitest::Test
  def c(s) = RaylibClient.snake_to_cmd(s)

  def test_simple_multi_word
    assert_equal 'DrawCircle',      c('draw_circle')
    assert_equal 'ClearBackground', c('clear_background')
    assert_equal 'DrawText',        c('draw_text')
    assert_equal 'BeginDrawing',    c('begin_drawing')
    assert_equal 'EndDrawing',      c('end_drawing')
  end

  def test_fps_abbreviation
    assert_equal 'DrawFPS', c('draw_fps')
    assert_equal 'GetFPS',  c('get_fps')
  end

  def test_2d_3d_suffixes
    assert_equal 'BeginMode2D', c('begin_mode2d')
    assert_equal 'EndMode2D',   c('end_mode2d')
    assert_equal 'BeginMode3D', c('begin_mode3d')
    assert_equal 'EndMode3D',   c('end_mode3d')
  end

  def test_npatch
    assert_equal 'DrawTextureNPatch', c('draw_texture_npatch')
  end

  def test_single_letter_suffixes
    assert_equal 'DrawCircleV',   c('draw_circle_v')
    assert_equal 'DrawTextureEx', c('draw_texture_ex')
    assert_equal 'DrawTextPro',   c('draw_text_pro')
    assert_equal 'DrawLineEx',    c('draw_line_ex')
    assert_equal 'DrawCircleV',   c('draw_circle_v')
  end

  def test_long_names
    assert_equal 'DrawRectangleRoundedLinesEx', c('draw_rectangle_rounded_lines_ex')
    assert_equal 'DrawSplineBezierCubic',       c('draw_spline_bezier_cubic')
    assert_equal 'SetShaderValueMatrix',        c('set_shader_value_matrix')
    assert_equal 'GetWindowScaleDPI',           c('get_window_scale_dpi')
    assert_equal 'DrawRectangleGradientEx',     c('draw_rectangle_gradient_ex')
  end

  def test_introspection_commands
    assert_equal 'GetScreenWidth',   c('get_screen_width')
    assert_equal 'GetScreenHeight',  c('get_screen_height')
    assert_equal 'IsWindowFocused',  c('is_window_focused')
    assert_equal 'IsKeyPressed',     c('is_key_pressed')
    assert_equal 'GetMousePosition', c('get_mouse_position')
    assert_equal 'GetServerInfo',    c('get_server_info')
    assert_equal 'ListHandles',      c('list_handles')
    assert_equal 'GetTextureInfo',   c('get_texture_info')
  end

  def test_display_list_commands
    assert_equal 'DisplayListBegin',    c('display_list_begin')
    assert_equal 'DisplayListEnd',      c('display_list_end')
    assert_equal 'DisplayListClear',    c('display_list_clear')
    assert_equal 'DisplayListDelete',   c('display_list_delete')
    assert_equal 'DisplayListSetOrder', c('display_list_set_order')
  end

  def test_upload_commands
    assert_equal 'BeginUpload',   c('begin_upload')
    assert_equal 'UploadChunk',   c('upload_chunk')
    assert_equal 'CommitUpload',  c('commit_upload')
    assert_equal 'AbortUpload',   c('abort_upload')
    assert_equal 'ListUploads',   c('list_uploads')
    assert_equal 'UploadTexture', c('upload_texture')
  end
end

# ===========================================================================
class TestCmdMethod < Minitest::Test
  def setup
    @sock   = FakeSocket.new
    @client = RaylibClient.new(@sock)
  end

  def test_sends_cmd_and_args
    @client.cmd('DrawCircle', { centerX: 400, centerY: 300, radius: 50, color: 'RED' })
    msg = @sock.last_cmd
    assert_equal 'DrawCircle', msg['cmd']
    assert_equal 400,          msg.dig('args', 'centerX')
    assert_equal 300,          msg.dig('args', 'centerY')
    assert_equal 'RED',        msg.dig('args', 'color')
  end

  def test_sends_cmd_without_args
    @client.cmd('BeginDrawing')
    msg = @sock.last_cmd
    assert_equal 'BeginDrawing', msg['cmd']
    assert_nil msg['args']
  end

  def test_no_id_in_fire_and_forget
    @client.cmd('DrawText', { text: 'hi', posX: 0, posY: 0, fontSize: 12, color: 'WHITE' })
    msg = @sock.last_cmd
    assert_nil msg['id']
  end

  def test_returns_nil
    assert_nil @client.cmd('ClearBackground', { color: 'BLACK' })
  end

  def test_multiple_commands_sent_in_order
    @client.cmd('DrawCircle', { centerX: 10, centerY: 10, radius: 5, color: 'RED' })
    @client.cmd('DrawText',   { text: 'hi', posX: 0, posY: 0, fontSize: 12, color: 'WHITE' })
    cmds = @sock.all_cmds
    assert_equal 2, cmds.length
    assert_equal 'DrawCircle', cmds[0]['cmd']
    assert_equal 'DrawText',   cmds[1]['cmd']
  end
end

# ===========================================================================
class TestSyncMethod < Minitest::Test
  def test_sends_with_id_and_returns_result
    sock   = FakeSocket.new([ok_response({ 'width' => 800 })])
    client = RaylibClient.new(sock)
    result = client.sync('GetScreenWidth')
    assert_equal({ 'width' => 800 }, result)
    msg = sock.last_cmd
    assert_equal 'GetScreenWidth', msg['cmd']
    refute_nil msg['id']
    refute_empty msg['id']
  end

  def test_sends_args
    sock   = FakeSocket.new([ok_response({ 'width' => 64 })])
    client = RaylibClient.new(sock)
    client.sync('GetMonitorWidth', { monitor: 0 })
    msg = sock.last_cmd
    assert_equal 'GetMonitorWidth', msg['cmd']
    assert_equal 0, msg.dig('args', 'monitor')
  end

  def test_raises_on_server_error
    sock   = FakeSocket.new([err_response('invalid handle')])
    client = RaylibClient.new(sock)
    err = assert_raises(RaylibClient::Error) do
      client.sync('GetTextureInfo', { handle: 99 })
    end
    assert_match 'invalid handle', err.message
  end

  def test_raises_on_disconnect
    sock   = FakeSocket.new  # empty responses → gets returns nil
    client = RaylibClient.new(sock)
    assert_raises(RaylibClient::Error) { client.sync('GetScreenWidth') }
  end

  def test_result_nil_for_commands_with_no_result
    sock   = FakeSocket.new([ok_response(nil)])
    client = RaylibClient.new(sock)
    result = client.sync('Subscribe', { events: ['KeyPressed'] })
    assert_nil result
  end
end

# ===========================================================================
class TestMethodMissing < Minitest::Test
  def setup
    @sock   = FakeSocket.new
    @client = RaylibClient.new(@sock)
  end

  def test_keyword_args_dispatched_as_fire_and_forget
    @client.draw_circle(centerX: 400, centerY: 300, radius: 50, color: 'RED')
    msg = @sock.last_cmd
    assert_equal 'DrawCircle', msg['cmd']
    assert_equal 400,          msg.dig('args', 'centerX')
    assert_nil msg['id'], 'fire-and-forget must not carry an id'
  end

  def test_hash_arg_dispatched_as_fire_and_forget
    @client.clear_background({ color: 'BLACK' })
    msg = @sock.last_cmd
    assert_equal 'ClearBackground', msg['cmd']
    assert_equal 'BLACK', msg.dig('args', 'color')
  end

  def test_no_args_dispatches_correctly
    @client.begin_drawing
    msg = @sock.last_cmd
    assert_equal 'BeginDrawing', msg['cmd']
    assert_nil msg['args']
  end

  def test_sync_command_dispatches_via_sync
    resp   = ok_response({ 'width' => 800 })
    sock   = FakeSocket.new([resp])
    client = RaylibClient.new(sock)
    result = client.get_screen_width
    assert_equal({ 'width' => 800 }, result)
    msg = sock.last_cmd
    assert_equal 'GetScreenWidth', msg['cmd']
    assert msg['id'], 'sync command must carry an id'
  end

  def test_is_command_dispatches_via_sync
    resp   = ok_response({ 'focused' => true })
    sock   = FakeSocket.new([resp])
    client = RaylibClient.new(sock)
    result = client.is_window_focused
    assert_equal({ 'focused' => true }, result)
  end

  def test_fps_abbreviation_via_method_missing
    @client.draw_fps(posX: 10, posY: 10)
    msg = @sock.last_cmd
    assert_equal 'DrawFPS', msg['cmd']
  end

  def test_respond_to_missing_true
    assert @client.respond_to?(:draw_circle)
    assert @client.respond_to?(:get_screen_width)
    assert @client.respond_to?(:any_unknown_method)
  end
end

# ===========================================================================
class TestDisplayListDSL < Minitest::Test
  def setup
    @sock   = FakeSocket.new
    @client = RaylibClient.new(@sock)
  end

  def test_wraps_block_in_begin_and_end
    @client.display_list('main') do
      @client.clear_background(color: 'RAYWHITE')
      @client.draw_circle(centerX: 400, centerY: 300, radius: 50, color: 'RED')
    end
    cmds = @sock.all_cmds
    assert_equal 4, cmds.length
    assert_equal 'DisplayListBegin', cmds[0]['cmd']
    assert_equal 'main',             cmds[0].dig('args', 'name')
    assert_equal 'ClearBackground',  cmds[1]['cmd']
    assert_equal 'DrawCircle',       cmds[2]['cmd']
    assert_equal 'DisplayListEnd',   cmds[3]['cmd']
  end

  def test_display_list_end_sent_even_on_exception
    begin
      @client.display_list('test') do
        @client.draw_text(text: 'hi', posX: 0, posY: 0, fontSize: 12, color: 'WHITE')
        raise 'boom'
      end
    rescue RuntimeError
    end
    cmds = @sock.all_cmds
    assert_equal 'DisplayListEnd', cmds.last['cmd']
  end

  def test_empty_display_list
    @client.display_list('empty') {}
    cmds = @sock.all_cmds
    assert_equal 2, cmds.length
    assert_equal 'DisplayListBegin', cmds[0]['cmd']
    assert_equal 'DisplayListEnd',   cmds[1]['cmd']
  end
end

# ===========================================================================
class TestBatchMode < Minitest::Test
  def setup
    @sock   = FakeSocket.new
    @client = RaylibClient.new(@sock)
  end

  def test_flushes_all_commands_at_end_of_block
    @client.batch do
      @client.clear_background(color: 'BLACK')
      @client.draw_fps(posX: 10, posY: 10)
      @client.draw_text(text: 'hi', posX: 0, posY: 0, fontSize: 12, color: 'WHITE')
    end
    cmds = @sock.all_cmds
    assert_equal 3, cmds.length
    assert_equal 'ClearBackground', cmds[0]['cmd']
    assert_equal 'DrawFPS',         cmds[1]['cmd']
    assert_equal 'DrawText',        cmds[2]['cmd']
  end

  def test_flushes_on_exception
    begin
      @client.batch do
        @client.clear_background(color: 'BLACK')
        raise 'oops'
      end
    rescue RuntimeError
    end
    assert_equal 1, @sock.lines.length
    assert_equal 'ClearBackground', @sock.last_cmd['cmd']
  end

  def test_empty_batch_writes_nothing
    @client.batch {}
    assert_empty @sock.lines
  end

  def test_sync_inside_batch_sent_immediately
    resp   = ok_response({ 'width' => 800 })
    sock   = FakeSocket.new([resp])
    client = RaylibClient.new(sock)

    width_result = nil
    client.batch do
      client.clear_background(color: 'BLACK')
      width_result = client.get_screen_width   # bypasses buffer
      client.draw_fps(posX: 10, posY: 10)
    end

    assert_equal({ 'width' => 800 }, width_result)
    # Lines: GetScreenWidth (immediate puts) + ClearBackground + DrawFPS (batch write)
    assert_equal 3, sock.lines.length
  end

  def test_display_list_inside_batch
    @client.batch do
      @client.display_list('bg') do
        @client.clear_background(color: 'RAYWHITE')
      end
    end
    cmds = @sock.all_cmds
    assert_equal 3, cmds.length
    assert_equal 'DisplayListBegin', cmds[0]['cmd']
    assert_equal 'ClearBackground',  cmds[1]['cmd']
    assert_equal 'DisplayListEnd',   cmds[2]['cmd']
  end
end

# ===========================================================================
class TestEventHelpers < Minitest::Test
  def test_subscribe_sends_sync_command_with_events_array
    sock   = FakeSocket.new([ok_response(nil)])
    client = RaylibClient.new(sock)
    client.subscribe('KeyPressed', 'MouseMoved')
    msg = sock.last_cmd
    assert_equal 'Subscribe',                   msg['cmd']
    assert_equal %w[KeyPressed MouseMoved],      msg.dig('args', 'events')
    refute_nil msg['id']
  end

  def test_subscribe_accepts_array_argument
    sock   = FakeSocket.new([ok_response(nil)])
    client = RaylibClient.new(sock)
    client.subscribe(%w[KeyPressed MouseMoved])
    msg = sock.last_cmd
    assert_equal %w[KeyPressed MouseMoved], msg.dig('args', 'events')
  end

  def test_unsubscribe_sends_sync_command
    sock   = FakeSocket.new([ok_response(nil)])
    client = RaylibClient.new(sock)
    client.unsubscribe('KeyPressed')
    msg = sock.last_cmd
    assert_equal 'Unsubscribe', msg['cmd']
    refute_nil msg['id']
  end

  def test_drain_events_returns_all_ready_lines
    ev1    = JSON.generate({ event: 'KeyPressed',  key: 65,   frame: 100 })
    ev2    = JSON.generate({ event: 'MouseMoved',  x: 320.0, y: 240.0, frame: 101 })
    sock   = FakeSocket.new([ev1, ev2])
    client = RaylibClient.new(sock)
    events = client.drain_events
    assert_equal 2,            events.length
    assert_equal 'KeyPressed', events[0]['event']
    assert_equal 'MouseMoved', events[1]['event']
  end

  def test_drain_events_empty_when_none_ready
    client = RaylibClient.new(FakeSocket.new)
    assert_equal [], client.drain_events
  end

  def test_drain_events_skips_malformed_json
    good  = JSON.generate({ event: 'KeyPressed', key: 65, frame: 1 })
    sock  = FakeSocket.new([good])
    # Manually push a bad line into responses
    sock.instance_variable_get(:@responses).unshift('not json {{{')
    client = RaylibClient.new(sock)
    events = client.drain_events
    # malformed line is skipped; good line is included
    assert_equal 1, events.length
    assert_equal 'KeyPressed', events[0]['event']
  end
end

# ===========================================================================
class TestUploadHelpers < Minitest::Test
  def begin_resp  = JSON.generate({ id: 'r1', ok: true, result: { 'uploadId' => 'u-1' } })
  def chunk_resp  = JSON.generate({ id: 'r2', ok: true, result: nil })
  def commit_resp = JSON.generate({ id: 'r3', ok: true, result: { 'handle' => 7 } })

  def test_upload_data_single_chunk
    sock   = FakeSocket.new([begin_resp, chunk_resp, commit_resp])
    client = RaylibClient.new(sock)

    handle = client.upload_data('x' * 100, file_type: '.png', resource_type: 'texture',
                                name: 'test.png')
    assert_equal 7, handle

    cmds = sock.all_cmds
    assert_equal 3, cmds.length

    assert_equal 'BeginUpload',  cmds[0]['cmd']
    assert_equal 'test.png',     cmds[0].dig('args', 'name')
    assert_equal '.png',         cmds[0].dig('args', 'fileType')
    assert_equal 100,            cmds[0].dig('args', 'totalBytes')

    assert_equal 'UploadChunk',  cmds[1]['cmd']
    assert_equal 'u-1',          cmds[1].dig('args', 'uploadId')
    assert_equal 0,              cmds[1].dig('args', 'seq')
    refute_nil                   cmds[1].dig('args', 'data')

    assert_equal 'CommitUpload', cmds[2]['cmd']
    assert_equal 'u-1',          cmds[2].dig('args', 'uploadId')
    assert_equal 'texture',      cmds[2].dig('args', 'type')
  end

  def test_upload_data_multiple_chunks
    c2 = JSON.generate({ id: 'r4', ok: true, result: nil })
    c3 = JSON.generate({ id: 'r5', ok: true, result: nil })
    sock   = FakeSocket.new([begin_resp, chunk_resp, c2, c3, commit_resp])
    client = RaylibClient.new(sock)

    handle = client.upload_data('a' * 25, file_type: '.ogg', resource_type: 'music',
                                name: 'test.ogg', chunk_size: 10)
    assert_equal 7, handle

    cmds = sock.all_cmds
    # BeginUpload + 3 UploadChunk + CommitUpload
    assert_equal 5, cmds.length
    assert_equal 'BeginUpload', cmds[0]['cmd']
    [1, 2, 3].each_with_index { |seq, i| assert_equal seq - 1, cmds[i + 1].dig('args', 'seq') }
    assert_equal 'CommitUpload', cmds[4]['cmd']
  end

  def test_upload_data_base64_encodes_chunks
    sock   = FakeSocket.new([begin_resp, chunk_resp, commit_resp])
    client = RaylibClient.new(sock)

    data = "\xFF\xFE\xFD\xFC".b
    client.upload_data(data, file_type: '.png', resource_type: 'texture')
    b64 = sock.all_cmds[1].dig('args', 'data')
    require 'base64'
    assert_equal data, Base64.strict_decode64(b64)
  end

  def test_chunked_upload_reads_file(tmp_path = nil)
    # Write a temp file and verify chunked_upload reads it
    require 'tempfile'
    Tempfile.create(['test', '.png']) do |f|
      f.binmode
      f.write('PNGDATA' * 10)
      f.flush

      sock   = FakeSocket.new([begin_resp, chunk_resp, commit_resp])
      client = RaylibClient.new(sock)
      handle = client.chunked_upload(f.path, file_type: '.png', resource_type: 'texture')
      assert_equal 7, handle

      cmds = sock.all_cmds
      assert_equal File.basename(f.path), cmds[0].dig('args', 'name')
      assert_equal File.size(f.path),     cmds[0].dig('args', 'totalBytes')
    end
  end
end

# ===========================================================================
class TestConnectionManagement < Minitest::Test
  def test_connected_true_initially
    client = RaylibClient.new(FakeSocket.new)
    assert client.connected?
  end

  def test_connected_false_after_close
    sock   = FakeSocket.new
    client = RaylibClient.new(sock)
    client.close
    refute client.connected?
  end

  def test_close_is_idempotent
    client = RaylibClient.new(FakeSocket.new)
    client.close
    client.close  # must not raise
  end
end

# ===========================================================================
class TestSyncCmdsSet < Minitest::Test
  def test_includes_introspection_commands
    %w[GetScreenWidth GetScreenHeight GetFPS GetFrameTime GetTime
       IsWindowFocused IsKeyPressed IsMouseButtonDown
       GetMousePosition GetServerInfo].each do |cmd|
      assert RaylibClient::SYNC_CMDS.include?(cmd), "expected SYNC_CMDS to include #{cmd}"
    end
  end

  def test_includes_resource_commands
    %w[LoadTexture LoadFont LoadRenderTexture LoadSound LoadMusicStream LoadShader
       UnloadTexture UnloadFont UnloadRenderTexture UnloadSound UnloadMusicStream UnloadShader].each do |cmd|
      assert RaylibClient::SYNC_CMDS.include?(cmd), "expected SYNC_CMDS to include #{cmd}"
    end
  end

  def test_includes_upload_commands
    %w[UploadTexture UploadFont UploadSound UploadMusic UploadShader UploadTextureRaw
       BeginUpload UploadChunk CommitUpload AbortUpload ListUploads].each do |cmd|
      assert RaylibClient::SYNC_CMDS.include?(cmd), "expected SYNC_CMDS to include #{cmd}"
    end
  end

  def test_includes_event_commands
    assert RaylibClient::SYNC_CMDS.include?('Subscribe')
    assert RaylibClient::SYNC_CMDS.include?('Unsubscribe')
  end

  def test_excludes_fire_and_forget_commands
    %w[DrawCircle DrawText ClearBackground BeginDrawing EndDrawing
       DrawFPS DisplayListBegin DisplayListEnd SetWindowTitle].each do |cmd|
      refute RaylibClient::SYNC_CMDS.include?(cmd), "SYNC_CMDS should NOT include #{cmd}"
    end
  end
end
