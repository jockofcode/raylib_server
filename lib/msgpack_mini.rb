# frozen_string_literal: true
# msgpack_mini.rb — minimal pure-Ruby MessagePack encoder and decoder.
#
# Supports the subset of MessagePack types used by the raylib_server protocol:
#   Maps (fixmap / map16)
#   Arrays (fixarray / array16)
#   Strings (fixstr / str8 / str16)
#   Integers (fixint / int8..int32 / uint8..uint32)
#   Float64
#   Boolean, Nil
#
# No external dependencies.

module MsgpackMini
  # ---------------------------------------------------------------------------
  # Encoder
  # ---------------------------------------------------------------------------

  def self.encode(val)
    case val
    when NilClass   then "\xC0".b
    when TrueClass  then "\xC3".b
    when FalseClass then "\xC2".b
    when Symbol     then encode_str(val.to_s)
    when String     then encode_str(val)
    when Integer    then encode_int(val)
    when Float      then "\xCB".b + [val].pack('G')
    when Array      then encode_array(val)
    when Hash       then encode_map(val)
    else raise ArgumentError, "MsgpackMini: unsupported type #{val.class}"
    end
  end

  def self.encode_str(s)
    b = s.b
    n = b.bytesize
    if n <= 31
      [0xA0 | n].pack('C') + b
    elsif n <= 255
      "\xD9".b + [n].pack('C') + b
    elsif n <= 65_535
      "\xDA".b + [n].pack('n') + b
    else
      "\xDB".b + [n].pack('N') + b
    end
  end

  def self.encode_int(v)
    if v >= 0
      if    v <= 127            then [v].pack('C')
      elsif v <= 0xFF           then "\xCC".b + [v].pack('C')
      elsif v <= 0xFFFF         then "\xCD".b + [v].pack('n')
      elsif v <= 0xFFFF_FFFF    then "\xCE".b + [v].pack('N')
      else                           "\xCF".b + [v].pack('Q>')
      end
    else
      if    v >= -32            then [v & 0xFF].pack('C')
      elsif v >= -128           then "\xD0".b + [v].pack('c')
      elsif v >= -32_768        then "\xD1".b + [v].pack('s>')
      elsif v >= -2_147_483_648 then "\xD2".b + [v].pack('l>')
      else                           "\xD3".b + [v].pack('q>')
      end
    end
  end

  def self.encode_array(arr)
    n = arr.length
    hdr = n <= 15 ? [0x90 | n].pack('C') : "\xDC".b + [n].pack('n')
    hdr.b + arr.map { |v| encode(v) }.join.b
  end

  def self.encode_map(hash)
    n = hash.length
    hdr = n <= 15 ? [0x80 | n].pack('C') : "\xDE".b + [n].pack('n')
    (hdr.b + hash.map { |k, v| encode(k.to_s) + encode(v) }.join).b
  end

  # ---------------------------------------------------------------------------
  # Decoder
  # ---------------------------------------------------------------------------

  # Decodes one value from a binary String starting at offset pos.
  # Returns [decoded_value, new_pos].
  def self.decode_one(data, pos)
    b = data.getbyte(pos)
    pos += 1

    # Positive fixint
    return [b, pos] if b <= 0x7F

    # Fixmap
    if (b & 0xF0) == 0x80
      decode_map_entries(data, pos, b & 0x0F)
    # Fixarray
    elsif (b & 0xF0) == 0x90
      decode_array_entries(data, pos, b & 0x0F)
    # Fixstr
    elsif (b & 0xE0) == 0xA0
      n = b & 0x1F
      [data.byteslice(pos, n).force_encoding('UTF-8'), pos + n]
    # Negative fixint
    elsif (b & 0xE0) == 0xE0
      [b - 256, pos]
    else
      case b
      when 0xC0 then [nil,   pos]
      when 0xC2 then [false, pos]
      when 0xC3 then [true,  pos]
      when 0xCA
        f32 = data.byteslice(pos, 4).unpack1('g')
        [f32.to_f, pos + 4]
      when 0xCB
        f64 = data.byteslice(pos, 8).unpack1('G')
        [f64, pos + 8]
      when 0xCC then [data.getbyte(pos), pos + 1]
      when 0xCD then [data.byteslice(pos, 2).unpack1('n'), pos + 2]
      when 0xCE then [data.byteslice(pos, 4).unpack1('N'), pos + 4]
      when 0xCF then [data.byteslice(pos, 8).unpack1('Q>'), pos + 8]
      when 0xD0 then [data.byteslice(pos, 1).unpack1('c'), pos + 1]
      when 0xD1 then [data.byteslice(pos, 2).unpack1('s>'), pos + 2]
      when 0xD2 then [data.byteslice(pos, 4).unpack1('l>'), pos + 4]
      when 0xD3 then [data.byteslice(pos, 8).unpack1('q>'), pos + 8]
      when 0xD9
        n = data.getbyte(pos)
        [data.byteslice(pos + 1, n).force_encoding('UTF-8'), pos + 1 + n]
      when 0xDA
        n = data.byteslice(pos, 2).unpack1('n')
        [data.byteslice(pos + 2, n).force_encoding('UTF-8'), pos + 2 + n]
      when 0xDB
        n = data.byteslice(pos, 4).unpack1('N')
        [data.byteslice(pos + 4, n).force_encoding('UTF-8'), pos + 4 + n]
      when 0xDC
        n = data.byteslice(pos, 2).unpack1('n')
        decode_array_entries(data, pos + 2, n)
      when 0xDD
        n = data.byteslice(pos, 4).unpack1('N')
        decode_array_entries(data, pos + 4, n)
      when 0xDE
        n = data.byteslice(pos, 2).unpack1('n')
        decode_map_entries(data, pos + 2, n)
      when 0xDF
        n = data.byteslice(pos, 4).unpack1('N')
        decode_map_entries(data, pos + 4, n)
      else
        raise "MsgpackMini: unsupported byte 0x#{b.to_s(16)}"
      end
    end
  end

  def self.decode_map_entries(data, pos, count)
    h = {}
    count.times do
      k, pos = decode_one(data, pos)
      v, pos = decode_one(data, pos)
      h[k] = v
    end
    [h, pos]
  end

  def self.decode_array_entries(data, pos, count)
    arr = []
    count.times { v, pos = decode_one(data, pos); arr << v }
    [arr, pos]
  end

  # Decode a MessagePack-encoded binary string, returning the Ruby value.
  def self.decode(data)
    val, = decode_one(data.b, 0)
    val
  end
end
