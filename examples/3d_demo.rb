#!/usr/bin/env ruby
# 3D Demo — Phase 8: 3D shapes and camera.
#
# Demonstrates:
#   BeginMode3D / EndMode3D
#   DrawSphere, DrawCube, DrawCylinder, DrawCapsule, DrawPlane, DrawGrid
#   DrawSphereWires, DrawCubeWires, DrawLine3D, DrawCircle3D, DrawRay
#
# Controls:
#   Up / Down  — pitch camera
#   Left / Right — orbit camera
#   Ctrl+C     — quit
#
# Usage:  ruby examples/3d_demo.rb
#         ruby examples/3d_demo.rb 7878   # custom port

$LOAD_PATH.unshift File.expand_path('../lib', __dir__)
require 'raylib_client'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 60.0
FRAME = 1.0 / FPS
W     = 800
H     = 600

rls = RaylibClient.connect(port: PORT)
rls.set_window_size(width: W, height: H)
rls.set_window_title(title: '3D Demo — Phase 8')
rls.set_target_fps(fps: 60)

rls.subscribe('KeyPressed')

angle   = 0.0   # camera orbit angle in radians
pitch   = 0.3   # vertical tilt
radius  = 16.0

KEY_UP    = 265
KEY_DOWN  = 264
KEY_LEFT  = 263
KEY_RIGHT = 262

puts "3D demo running.  Arrow keys = orbit camera.  Ctrl+C = quit."

begin
  loop do
    t0 = Time.now

    rls.drain_events.each do |ev|
      next unless ev['event'] == 'KeyPressed'
      case ev['key']
      when KEY_LEFT  then angle -= 0.12
      when KEY_RIGHT then angle += 0.12
      when KEY_UP    then pitch = [pitch + 0.08,  1.4].min
      when KEY_DOWN  then pitch = [pitch - 0.08, -1.4].max
      end
    end

    angle += 0.008  # slow auto-rotation

    cam_x = radius * Math.cos(angle) * Math.cos(pitch)
    cam_y = radius * Math.sin(pitch)
    cam_z = radius * Math.sin(angle) * Math.cos(pitch)

    camera = {
      position:   [cam_x, cam_y, cam_z],
      target:     [0.0, 1.0, 0.0],
      up:         [0.0, 1.0, 0.0],
      fovy:       45.0,
      projection: 0,   # CAMERA_PERSPECTIVE
    }

    rls.display_list('main') do
      rls.clear_background(color: [18, 18, 28, 255])

      rls.begin_mode3_d(camera: camera)

      # Ground grid
      rls.draw_grid(slices: 20, spacing: 1.0)

      # Floor plane
      rls.draw_plane(centerPos: [0, 0, 0], size: [20, 20], color: [40, 50, 60, 200])

      # Central sphere
      rls.draw_sphere(centerPos: [0, 1, 0], radius: 1.0, color: 'SKYBLUE')
      rls.draw_sphere_wires(centerPos: [0, 1, 0], radius: 1.02, rings: 10, slices: 10,
                            color: [80, 160, 255, 120])

      # Cubes around the sphere
      4.times do |i|
        a = i * Math::PI / 2.0
        x = 4.0 * Math.cos(a)
        z = 4.0 * Math.sin(a)
        colors = ['RED', 'GREEN', 'GOLD', 'PURPLE']
        rls.draw_cube(position: [x, 0.5, z], width: 1.0, height: 1.0, length: 1.0,
                      color: colors[i])
        rls.draw_cube_wires(position: [x, 0.5, z], width: 1.0, height: 1.0, length: 1.0,
                            color: 'WHITE')
      end

      # Cylinder
      rls.draw_cylinder(position: [6, 0, 0], radiusTop: 0.5, radiusBottom: 0.8,
                        height: 2.0, slices: 12, color: 'LIME')
      rls.draw_cylinder_wires(position: [6, 0, 0], radiusTop: 0.5, radiusBottom: 0.8,
                              height: 2.0, slices: 12, color: 'DARKGREEN')

      # Capsule
      rls.draw_capsule(startPos: [-6, 0, 0], endPos: [-6, 2, 0], radius: 0.5,
                       slices: 8, rings: 4, color: 'ORANGE')
      rls.draw_capsule_wires(startPos: [-6, 0, 0], endPos: [-6, 2, 0], radius: 0.5,
                             slices: 8, rings: 4, color: 'DARKBROWN')

      # Circle3D (a halo around the sphere)
      rls.draw_circle3_d(center: [0, 1, 0], radius: 1.5, rotationAxis: [1, 0, 0],
                         rotationAngle: 90.0, color: 'YELLOW')

      # A ray pointing up
      rls.draw_ray(position: [0, 0, 0], direction: [0, 1, 0], color: 'WHITE')

      rls.end_mode3_d

      # 2D HUD overlay
      rls.draw_fps(posX: 10, posY: 10)
      rls.draw_text(text: '3D Demo — Phase 8', posX: W / 2 - 90, posY: 14,
                    fontSize: 20, color: 'RAYWHITE')
      rls.draw_text(text: 'Arrow keys: orbit camera', posX: 10, posY: H - 30,
                    fontSize: 14, color: [160, 160, 180, 200])
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
  rls.unsubscribe('KeyPressed') rescue nil
  rls.close
end
