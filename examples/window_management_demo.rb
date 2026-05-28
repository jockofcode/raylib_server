#!/usr/bin/env ruby
# Window management demo.
#
# Walks through all window management commands in a timed sequence.
# Each step labels what is about to happen on screen, then executes the
# command after a short pause so the effect is visible.
#
# Usage:  ruby examples/window_management_demo.rb
#         ruby examples/window_management_demo.rb 7878   # custom port
#
# Requires: a running raylib_server on localhost:7878.

require 'socket'
require 'json'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS
W     = 800
H     = 600

socket = TCPSocket.new('localhost', PORT)

def send_cmd(socket, cmd, args = nil)
  msg = { cmd: cmd }
  msg[:args] = args if args
  socket.puts JSON.generate(msg)
end

# Draw a labelled info screen for the current demo step.
def draw_info(socket, title, lines, accent = 'SKYBLUE')
  send_cmd(socket, 'ClearBackground', { color: [18, 18, 30, 255] })

  # Header bar
  send_cmd(socket, 'DrawRectangle', { posX: 0, posY: 0, width: 2000, height: 58, color: [28, 28, 48, 255] })
  send_cmd(socket, 'DrawRectangle', { posX: 0, posY: 56, width: 2000, height: 2, color: accent })
  send_cmd(socket, 'DrawText', { text: title, posX: 20, posY: 16, fontSize: 26, color: accent })

  # Body
  lines.each_with_index do |line, i|
    color = line.start_with?('  ') ? 'GOLD' : 'LIGHTGRAY'
    send_cmd(socket, 'DrawText', { text: line, posX: 20, posY: 78 + i * 30, fontSize: 18, color: color })
  end

  send_cmd(socket, 'DrawFPS', { posX: 10, posY: 10 })
end

def restore_defaults(socket)
  send_cmd(socket, 'SetWindowTitle',   { title: 'raylib_server' })
  send_cmd(socket, 'SetWindowSize',    { width: 800, height: 600 })
  send_cmd(socket, 'SetWindowOpacity', { opacity: 1.0 })
  send_cmd(socket, 'SetWindowState',   { flags: 4 })   # re-enable FLAG_WINDOW_RESIZABLE
  send_cmd(socket, 'RestoreWindow')
  send_cmd(socket, 'SetWindowFocused')
end

# ---------------------------------------------------------------------------
# Steps: each hash has :title, :lines, :wait (seconds), and an optional
# :action proc that runs after the wait expires.
# ---------------------------------------------------------------------------

STEPS = [
  {
    title:  'Window Management Demo',
    lines:  [
      'This demo walks through every window',
      'management command in sequence.',
      '',
      'Each step shows what will happen,',
      'then executes the command.',
    ],
    wait:   3.5,
  },

  # --- SetWindowTitle -------------------------------------------------------
  {
    title:  'SetWindowTitle',
    lines:  [
      'Changes the text in the OS title bar.',
      '',
      '  SetWindowTitle { title: "Hello from demo!" }',
    ],
    wait:   2.5,
    action: -> { send_cmd(socket, 'SetWindowTitle', { title: 'Hello from demo!' }) },
  },
  {
    title:  'SetWindowTitle (restore)',
    lines:  ['Restoring original title...'],
    wait:   1.5,
    action: -> { send_cmd(socket, 'SetWindowTitle', { title: 'Window Management Demo' }) },
  },

  # --- SetWindowSize --------------------------------------------------------
  {
    title:  'SetWindowSize',
    lines:  [
      'Resizes the window to given pixel dimensions.',
      '',
      '  SetWindowSize { width: 640, height: 400 }',
    ],
    wait:   2.5,
    action: -> { send_cmd(socket, 'SetWindowSize', { width: 640, height: 400 }) },
  },
  {
    title:  'SetWindowSize (restore)',
    lines:  ['Restoring to 800 x 600...'],
    wait:   1.5,
    action: -> { send_cmd(socket, 'SetWindowSize', { width: W, height: H }) },
  },

  # --- SetWindowPosition ----------------------------------------------------
  {
    title:  'SetWindowPosition',
    lines:  [
      'Moves the window to an absolute screen position.',
      '',
      '  SetWindowPosition { x: 80, y: 80 }',
    ],
    wait:   2.5,
    action: -> { send_cmd(socket, 'SetWindowPosition', { x: 80, y: 80 }) },
  },
  {
    title:  'SetWindowPosition (restore)',
    lines:  ['Moving back to (200, 150)...'],
    wait:   1.5,
    action: -> { send_cmd(socket, 'SetWindowPosition', { x: 200, y: 150 }) },
  },

  # --- SetWindowMinSize / SetWindowMaxSize ----------------------------------
  {
    title:  'SetWindowMinSize / SetWindowMaxSize',
    lines:  [
      'Clamps how small or large the user can drag',
      'the window.  Try resizing it by hand now.',
      '',
      '  SetWindowMinSize { width: 320, height: 240 }',
      '  SetWindowMaxSize { width: 1280, height: 960 }',
    ],
    wait:   4.0,
    action: -> {
      send_cmd(socket, 'SetWindowMinSize', { width: 320,  height: 240 })
      send_cmd(socket, 'SetWindowMaxSize', { width: 1280, height: 960 })
    },
  },

  # --- SetWindowState / ClearWindowState ------------------------------------
  {
    title:  'ClearWindowState — disable resizing',
    lines:  [
      'SetWindowState / ClearWindowState apply or',
      'remove ConfigFlags bits at runtime.',
      '',
      '  ClearWindowState { flags: 4 }   # FLAG_WINDOW_RESIZABLE',
      '',
      'The window border is now fixed-size.',
    ],
    wait:   3.0,
    action: -> { send_cmd(socket, 'ClearWindowState', { flags: 4 }) },
  },
  {
    title:  'SetWindowState — re-enable resizing',
    lines:  [
      '  SetWindowState { flags: 4 }   # FLAG_WINDOW_RESIZABLE',
      '',
      'Resizing is enabled again.',
    ],
    wait:   2.0,
    action: -> { send_cmd(socket, 'SetWindowState', { flags: 4 }) },
  },

  # --- SetWindowOpacity -----------------------------------------------------
  {
    title:  'SetWindowOpacity',
    lines:  [
      'Sets window transparency (0.0 = invisible,',
      '1.0 = fully opaque).',
      '',
      '  SetWindowOpacity { opacity: 0.35 }',
    ],
    wait:   2.5,
    action: -> { send_cmd(socket, 'SetWindowOpacity', { opacity: 0.35 }) },
  },
  {
    title:  'SetWindowOpacity (restore)',
    lines:  ['Restoring to fully opaque...'],
    wait:   2.0,
    action: -> { send_cmd(socket, 'SetWindowOpacity', { opacity: 1.0 }) },
  },

  # --- SetWindowFocused -----------------------------------------------------
  {
    title:  'SetWindowFocused',
    lines:  [
      'Brings the window to the foreground',
      'and gives it keyboard/mouse focus.',
      '',
      '  SetWindowFocused',
    ],
    wait:   2.0,
    action: -> { send_cmd(socket, 'SetWindowFocused') },
  },

  # --- MaximizeWindow / RestoreWindow ---------------------------------------
  {
    title:  'MaximizeWindow',
    lines:  [
      'Expands the window to fill the screen,',
      'like clicking the maximize button.',
      '',
      '  MaximizeWindow',
    ],
    wait:   2.5,
    action: -> { send_cmd(socket, 'MaximizeWindow') },
  },
  {
    title:  'RestoreWindow (from maximized)',
    lines:  [
      'Returns the window to its previous size',
      'and position.',
      '',
      '  RestoreWindow',
    ],
    wait:   2.0,
    action: -> { send_cmd(socket, 'RestoreWindow') },
  },

  # --- MinimizeWindow -------------------------------------------------------
  {
    title:  'MinimizeWindow',
    lines:  [
      'Hides the window to the dock / taskbar.',
      '',
      '  MinimizeWindow',
      '',
      'Restoring in 2 seconds...',
    ],
    wait:   2.0,
    action: -> { send_cmd(socket, 'MinimizeWindow') },
  },
  {
    title:  'RestoreWindow (from minimized)',
    lines:  [
      '  RestoreWindow',
      '  SetWindowFocused',
    ],
    wait:   2.0,
    action: -> {
      send_cmd(socket, 'RestoreWindow')
      send_cmd(socket, 'SetWindowFocused')
    },
  },

  # --- ToggleBorderlessWindowed ---------------------------------------------
  {
    title:  'ToggleBorderlessWindowed',
    lines:  [
      'Removes the OS title bar and chrome,',
      'making the window fill the screen edge-to-edge.',
      '',
      '  ToggleBorderlessWindowed',
    ],
    wait:   3.0,
    action: -> { send_cmd(socket, 'ToggleBorderlessWindowed') },
  },
  {
    title:  'ToggleBorderlessWindowed (off)',
    lines:  ['Toggling back to normal window...'],
    wait:   2.0,
    action: -> { send_cmd(socket, 'ToggleBorderlessWindowed') },
  },

  # --- ToggleFullscreen -----------------------------------------------------
  {
    title:  'ToggleFullscreen',
    lines:  [
      'Switches to exclusive fullscreen mode.',
      '',
      '  ToggleFullscreen',
      '',
      'Exiting fullscreen in 2 seconds...',
    ],
    wait:   2.0,
    action: -> { send_cmd(socket, 'ToggleFullscreen') },
  },
  {
    title:  'ToggleFullscreen (off)',
    lines:  ['Returning to windowed mode...'],
    wait:   2.0,
    action: -> { send_cmd(socket, 'ToggleFullscreen') },
  },

  # --- Done -----------------------------------------------------------------
  {
    title:  'Done!',
    lines:  [
      'All window management commands demonstrated.',
      '',
      'Restoring default window state...',
    ],
    wait:   2.5,
  },
].freeze

send_cmd(socket, 'SetWindowSize',  { width: W, height: H })
send_cmd(socket, 'SetWindowTitle', { title: 'Window Management Demo' })

puts "Window management demo — press Ctrl+C to skip to end."

begin
  STEPS.each do |step|
    deadline = Time.now + step[:wait]

    while Time.now < deadline
      t0 = Time.now
      draw_info(socket, step[:title], step[:lines])
      elapsed = Time.now - t0
      sleep([FRAME - elapsed, 0].max)
    end

    step[:action]&.call
  end

rescue Interrupt
  puts "\nSkipping to end..."
rescue Errno::EPIPE, Errno::ECONNRESET
  puts "Server disconnected."
ensure
  restore_defaults(socket) rescue nil
  send_cmd(socket, 'ClearBackground', { color: 'BLACK' }) rescue nil
  socket.close rescue nil
  puts "Done."
end
