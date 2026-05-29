#!/usr/bin/env ruby
# Audio demo — Phase 3 audio commands.
#
# Demonstrates:
#   InitAudioDevice, UploadSound, UploadMusic, PlaySound, StopSound,
#   PlayMusicStream, PauseMusicStream, ResumeMusicStream,
#   SetSoundVolume, SetSoundPitch, SetMusicVolume,
#   UnloadSound, UnloadMusicStream, CloseAudioDevice
#
# Generates two WAV tones in Ruby — no external audio files required.
#
# Controls (click the window to give it keyboard focus):
#   SPACE      — replay the beep sound effect (pitch rises each press)
#   M          — pause / resume background music
#   Up / Down  — raise / lower music volume
#
# Usage:  ruby examples/audio_demo.rb
#         ruby examples/audio_demo.rb 7878   # custom port
#
# Requires: a running raylib_server on localhost:7878.

$LOAD_PATH.unshift File.expand_path('../lib', __dir__)
require 'raylib_client'
require 'base64'

PORT  = (ARGV[0] || 7878).to_i
FPS   = 30.0
FRAME = 1.0 / FPS
W     = 800
H     = 600

# Build a mono 16-bit PCM WAV from a mix of sine waves.
def generate_wav(freqs, duration, sample_rate: 44_100, amplitude: 0.45)
  n   = (duration * sample_rate).to_i
  amp = amplitude / [freqs.length, 1].max
  pcm = Array.new(n) do |i|
    t   = i.to_f / sample_rate
    val = freqs.sum { |f| Math.sin(2 * Math::PI * f * t) } * amp
    (val * 32_767).round.clamp(-32_768, 32_767)
  end.pack('s<*')  # signed 16-bit little-endian

  [
    'RIFF', [36 + pcm.bytesize].pack('V'), 'WAVE',
    'fmt ', [16].pack('V'),
    [1].pack('v'),                  # PCM
    [1].pack('v'),                  # mono
    [sample_rate].pack('V'),        # sample rate
    [sample_rate * 2].pack('V'),    # byte rate
    [2].pack('v'),                  # block align
    [16].pack('v'),                 # bits per sample
    'data', [pcm.bytesize].pack('V')
  ].join + pcm
end

rls = RaylibClient.connect(port: PORT)
rls.set_window_size(width: W, height: H)
rls.set_window_title(title: 'Audio Demo')

rls.init_audio_device

# 440 Hz beep (0.35 s) — uploaded inline as a Sound.
print 'Uploading SFX...'
sfx_wav    = generate_wav([440.0], 0.35)
sfx_handle = rls.upload_sound(fileType: '.wav', data: Base64.strict_encode64(sfx_wav))['handle']
puts " handle #{sfx_handle}"

# A-major chord drone (110 + 165 + 220 Hz, 3 s) — uploaded as a MusicStream.
print 'Uploading music stream...'
music_wav    = generate_wav([110.0, 165.0, 220.0], 3.0)
music_handle = rls.upload_music(fileType: '.wav', data: Base64.strict_encode64(music_wav))['handle']
puts " handle #{music_handle}"

music_vol    = 0.8
sfx_pitch    = 1.0
music_paused = false
bar_phase    = 0.0

KEY_SPACE = 32
KEY_M     = 77
KEY_UP    = 265
KEY_DOWN  = 264

rls.set_music_volume(handle: music_handle, volume: music_vol)
rls.play_sound(handle: sfx_handle)
rls.play_music_stream(handle: music_handle)
rls.subscribe('KeyPressed')

puts "Audio demo running. SPACE=replay SFX  M=pause/resume  Up/Down=volume  Ctrl+C=quit"

begin
  loop do
    t0 = Time.now

    rls.drain_events.each do |ev|
      next unless ev['event'] == 'KeyPressed'
      case ev['key']
      when KEY_SPACE
        sfx_pitch = ((sfx_pitch + 0.15 - 1.0) % 1.5 + 1.0).round(2)
        rls.set_sound_pitch(handle: sfx_handle, pitch: sfx_pitch)
        rls.stop_sound(handle: sfx_handle)
        rls.play_sound(handle: sfx_handle)
      when KEY_M
        if music_paused
          rls.resume_music_stream(handle: music_handle)
          music_paused = false
        else
          rls.pause_music_stream(handle: music_handle)
          music_paused = true
        end
      when KEY_UP
        music_vol = [music_vol + 0.1, 1.0].min.round(1)
        rls.set_music_volume(handle: music_handle, volume: music_vol)
      when KEY_DOWN
        music_vol = [music_vol - 0.1, 0.0].max.round(1)
        rls.set_music_volume(handle: music_handle, volume: music_vol)
      end
    end

    bar_phase += 0.08 unless music_paused

    rls.batch do
      rls.clear_background(color: [12, 14, 22, 255])

      # Title
      rls.draw_text(text: 'Audio Demo — Phase 3', posX: 20, posY: 14,
                    fontSize: 24, color: 'GOLD')
      rls.draw_text(text: 'Controls: SPACE=replay SFX   M=pause/resume   Up/Down=volume',
                    posX: 20, posY: 44, fontSize: 13, color: [160, 160, 200, 255])
      rls.draw_rectangle_lines_ex(rec: [6, 6, W - 12, H - 12],
                                  lineThick: 1.5, color: [60, 60, 100, 200])

      # Status panels
      s_col  = music_paused ? [200, 80, 80, 255] : [80, 200, 120, 255]
      s_text = music_paused ? 'MUSIC: PAUSED' : 'MUSIC: PLAYING'
      rls.draw_rectangle(posX: 20, posY: 76, width: 220, height: 36, color: [28, 28, 48, 255])
      rls.draw_rectangle_lines_ex(rec: [20, 76, 220, 36], lineThick: 1.5, color: s_col)
      rls.draw_text(text: s_text, posX: 34, posY: 86, fontSize: 16, color: s_col)

      rls.draw_text(text: format('Volume: %d%%', (music_vol * 100).round),
                    posX: 260, posY: 84, fontSize: 16, color: 'LIGHTGRAY')
      rls.draw_rectangle(posX: 380, posY: 88, width: 200, height: 10, color: [40, 40, 60, 255])
      rls.draw_rectangle(posX: 380, posY: 88, width: (music_vol * 200).round, height: 10,
                         color: [80, 180, 255, 255])

      # Animated waveform bars
      rls.draw_text(text: 'Waveform (animated)', posX: 20, posY: 132,
                    fontSize: 13, color: [100, 100, 140, 200])
      bars = 50
      bars.times do |i|
        amp   = music_paused ? 3 : (28 + 22 * Math.sin(bar_phase * 1.3 + i * 0.38)).round
        x     = 20 + i * (W - 40) / bars
        r     = (100 + 155 * i.to_f / bars).round
        g     = (160 + 80 * Math.sin(bar_phase * 0.5 + i * 0.2).abs).round
        rls.draw_rectangle(posX: x, posY: 164 - amp, width: (W - 40) / bars - 1, height: amp * 2,
                           color: [r, g, 255, 200])
      end

      # Sound effect info
      rls.draw_text(text: 'Sound Effect (UploadSound)',
                    posX: 20, posY: 224, fontSize: 15, color: 'SKYBLUE')
      rls.draw_text(text: "440 Hz sine, 0.35 s | current pitch: #{format('%.2f', sfx_pitch)}x",
                    posX: 20, posY: 244, fontSize: 13, color: [140, 180, 220, 255])
      rls.draw_text(text: 'Press SPACE to replay (pitch rises each press)',
                    posX: 20, posY: 262, fontSize: 13, color: [120, 120, 160, 200])

      # Music info
      rls.draw_text(text: 'Music Stream (UploadMusic)',
                    posX: 20, posY: 292, fontSize: 15, color: 'LIME')
      rls.draw_text(text: 'A-major chord: 110 + 165 + 220 Hz, 3 s WAV',
                    posX: 20, posY: 312, fontSize: 13, color: [140, 200, 140, 255])
      rls.draw_text(text: 'Press M to pause/resume',
                    posX: 20, posY: 330, fontSize: 13, color: [120, 160, 120, 200])

      # Command reference
      rls.draw_text(text: 'Commands exercised:',
                    posX: 20, posY: 380, fontSize: 14, color: [100, 100, 160, 255])
      [
        'InitAudioDevice    CloseAudioDevice',
        'UploadSound        UploadMusic',
        'PlaySound          StopSound          SetSoundVolume    SetSoundPitch',
        'PlayMusicStream    PauseMusicStream   ResumeMusicStream',
        'StopMusicStream    SetMusicVolume',
        'UnloadSound        UnloadMusicStream',
      ].each_with_index do |line, i|
        rls.draw_text(text: line, posX: 30, posY: 400 + i * 20,
                      fontSize: 12, color: [160, 160, 200, 220])
      end

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
  rls.unsubscribe('KeyPressed')            rescue nil
  rls.stop_sound(handle: sfx_handle)       rescue nil
  rls.stop_music_stream(handle: music_handle) rescue nil
  rls.unload_sound(handle: sfx_handle)     rescue nil
  rls.unload_music_stream(handle: music_handle) rescue nil
  rls.close_audio_device                   rescue nil
  rls.close
end
