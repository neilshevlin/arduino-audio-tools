#pragma once

#include "AudioCodecs/AudioEncoded.h"
#include "AudioCodecs/CodecOpus.h"
#include "AudioTools/Buffers.h"
#include "oggz/oggz.h"

#define OGG_READ_SIZE (1024)
#define OGG_DEFAULT_BUFFER_SIZE (OGG_READ_SIZE)
//#define OGG_DEFAULT_BUFFER_SIZE (246)
//#define OGG_READ_SIZE (512)

namespace audio_tools {

/**
 * @brief OggContainerDecoder - Ogg Container. Decodes a packet from an Ogg
 * container. The Ogg begin segment contains the AudioInfo structure. You can
 * subclass and overwrite the beginOfSegment() method to implement your own
 * headers
 * @ingroup codecs
 * @ingroup decoder
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class OggContainerDecoder : public AudioDecoder {
 public:
  /**
   * @brief Construct a new OggContainerDecoder object
   */

  OggContainerDecoder() {
    p_codec = &dec_copy;
    out.setDecoder(p_codec);
  }

  OggContainerDecoder(AudioDecoder *decoder) {
    p_codec = decoder;
    out.setDecoder(p_codec);
  }

  OggContainerDecoder(AudioDecoder &decoder) {
    p_codec = &decoder;
    out.setDecoder(p_codec);
  }

  /// Defines the output Stream
  void setOutputStream(Print &print) override { out.setOutput(&print); }

  void setNotifyAudioChange(AudioInfoSupport &bi) override {
    out.setNotifyAudioChange(bi);
  }

  AudioInfo audioInfo() override { return out.audioInfo(); }

  void begin(AudioInfo info) {
    TRACED();
    this->info = info;
    out.setAudioInfo(info);
    begin();
  }

  void begin() override {
    TRACED();
    if (p_oggz == nullptr) {
      p_oggz = oggz_new(OGGZ_READ | OGGZ_AUTO);  // OGGZ_NONSTRICT
      is_open = true;
      // Callback to Replace standard IO
      if (oggz_io_set_read(p_oggz, ogg_io_read, this) != 0) {
        LOGE("oggz_io_set_read");
        is_open = false;
      }
      // Callback
      if (oggz_set_read_callback(p_oggz, -1, read_packet, this) != 0) {
        LOGE("oggz_set_read_callback");
        is_open = false;
      }

      if (oggz_set_read_page(p_oggz, -1, read_page, this) != 0) {
        LOGE("oggz_set_read_page");
        is_open = false;
      }
    }
  }

  void end() override {
    TRACED();
    flush();
    out.end();
    is_open = false;
    oggz_close(p_oggz);
    p_oggz = nullptr;
  }

  void flush() {
    LOGD("oggz_read...");
    while ((oggz_read(p_oggz, OGG_READ_SIZE)) > 0)
      ;
  }

  virtual size_t write(const void *in_ptr, size_t in_size) override {
    LOGD("write: %d", (int)in_size);

    // fill buffer
    size_t size_consumed = buffer.writeArray((uint8_t *)in_ptr, in_size);
    if (buffer.availableForWrite() == 0) {
      // Read all bytes into oggz, calling any read callbacks on the fly.
      flush();
    }
    // write remaining bytes
    if (size_consumed < in_size) {
      size_consumed += buffer.writeArray((uint8_t *)in_ptr + size_consumed,
                                         in_size - size_consumed);
      flush();
    }
    return size_consumed;
  }

  virtual operator bool() override { return is_open; }

 protected:
  EncodedAudioOutput out;
  CopyDecoder dec_copy;
  AudioDecoder *p_codec = nullptr;
  RingBuffer<uint8_t> buffer{OGG_DEFAULT_BUFFER_SIZE};
  OGGZ *p_oggz = nullptr;
  bool is_open = false;
  long pos = 0;

  // Final Stream Callback -> provide data to ogg
  static size_t ogg_io_read(void *user_handle, void *buf, size_t n) {
    LOGD("ogg_io_read: %d", (int)n);
    size_t result = 0;
    OggContainerDecoder *self = (OggContainerDecoder *)user_handle;
    if (self->buffer.available() >= n) {
      OggContainerDecoder *self = (OggContainerDecoder *)user_handle;
      result = self->buffer.readArray((uint8_t *)buf, n);
      self->pos += result;

    } else {
      result = 0;
    }
    return result;
  }

  // Process full packet
  static int read_packet(OGGZ *oggz, oggz_packet *zp, long serialno,
                         void *user_data) {
    LOGD("read_packet: %d", (int)zp->op.bytes);
    OggContainerDecoder *self = (OggContainerDecoder *)user_data;
    ogg_packet *op = &zp->op;
    int result = op->bytes;
    if (op->b_o_s) {
      self->beginOfSegment(op);
    } else if (op->e_o_s) {
      self->endOfSegment(op);
    } else {
      LOGD("process audio packet");
      int eff = self->out.write(op->packet, op->bytes);
      if (eff != result) {
        LOGE("Incomplere write");
      }
    }
    // 0 = success
    return 0;
  }

  static int read_page(OGGZ *oggz, const ogg_page *og, long serialno,
                       void *user_data) {
    LOGD("read_page: %d", (int)og->body_len);
    // 0 = success
    return 0;
  }

  virtual void beginOfSegment(ogg_packet *op) {
    LOGD("bos");
    if (op->bytes == sizeof(AudioInfo)) {
      AudioInfo cfg;
      memcpy(&cfg, op->packet, op->bytes);
      cfg.logInfo();
      if (cfg.bits_per_sample == 16 || cfg.bits_per_sample == 24 ||
          cfg.bits_per_sample == 32) {
        setAudioInfo(cfg);
      } else {
        LOGE("Invalid AudioInfo")
      }
    } else {
      LOGE("Invalid Header")
    }
  }

  virtual void endOfSegment(ogg_packet *op) {
    // end segment not supported
    LOGW("e_o_s");
  }
};

/**
 * @brief OggContainerEncoder - Ogg Container. Encodes a packet for an Ogg
 * container. The Ogg begin segment contains the AudioInfo structure. You can
 * subclass ond overwrite the writeHeader() method to implement your own header
 * logic.
 * @ingroup codecs
 * @ingroup encoder
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class OggContainerEncoder : public AudioEncoder {
 public:
  // Empty Constructor - the output stream must be provided with begin()
  OggContainerEncoder() {
    p_codec = &copy_enc;
    out.setEncoder(p_codec);
  }

  OggContainerEncoder(AudioEncoder *encoder) {
    p_codec = encoder;
    out.setEncoder(p_codec);
  }

  OggContainerEncoder(AudioEncoder &encoder) {
    p_codec = &encoder;
    out.setEncoder(p_codec);
  }

  /// Defines the output Stream
  void setOutputStream(Print &print) override { out.setOutput(&print); }

  /// Provides "audio/pcm"
  const char *mime() override { return mime_pcm; }

  /// We actually do nothing with this
  virtual void setAudioInfo(AudioInfo info) override {
    out.setAudioInfo(info);
    channels = info.channels;
  }

  virtual void begin(AudioInfo from) {
    setAudioInfo(from);
    begin();
  }

  /// starts the processing using the actual AudioInfo
  virtual void begin() override {
    TRACED();
    AudioInfo cfg = out.audioInfo();
    assert(cfg.channels != 0);
    assert(cfg.sample_rate != 0);
    is_open = true;
    out.begin();
    if (p_oggz == nullptr) {
      p_oggz = oggz_new(OGGZ_WRITE | OGGZ_NONSTRICT | OGGZ_AUTO);
      serialno = oggz_serialno_new(p_oggz);
      oggz_io_set_write(p_oggz, ogg_io_write, this);
      packetno = 0;
      granulepos = 0;

      if (!writeHeader()) {
        is_open = false;
      }
    }
  }

  /// stops the processing
  void end() override {
    TRACED();

    writeFooter();

    is_open = false;
    oggz_close(p_oggz);
    p_oggz = nullptr;
    out.end();
  }

  /// Writes raw data to be encoded and packaged
  virtual size_t write(const void *in_ptr, size_t in_size) override {
    if (!is_open || in_ptr == nullptr) return 0;
    LOGD("OggContainerEncoder::write: %d", (int)in_size);

    // encode the data
    uint8_t *data = (uint8_t *)in_ptr;
    op.packet = (uint8_t *)data;
    op.bytes = in_size;
    if (op.bytes > 0) {
      op.granulepos = granulepos +=
          op.bytes / sizeof(int16_t) / channels;  // sample
      op.b_o_s = false;
      op.e_o_s = false;
      op.packetno = packetno++;
      is_audio = true;
      if (!writePacket(op, OGGZ_FLUSH_AFTER)) {
        return 0;
      }
    }
    // trigger pysical write
    while ((oggz_write(p_oggz, in_size)) > 0)
      ;

    return in_size;
  }

  operator bool() override { return is_open; }

  bool isOpen() { return is_open; }

 protected:
  EncodedAudioOutput out;
  AudioEncoder *p_codec = nullptr;
  bool is_open;
  OGGZ *p_oggz = nullptr;
  ogg_packet op;
  ogg_packet oh;
  size_t granulepos = 0;
  size_t packetno = 0;
  long serialno = -1;
  bool is_audio = false;
  int channels = 2;
  CopyEncoder copy_enc;

  virtual bool writePacket(ogg_packet &op, int flag = 0) {
    LOGD("writePacket: %d", (int)op.bytes);
    long result = oggz_write_feed(p_oggz, &op, serialno, flag, NULL);
    if (result < 0 && result != OGGZ_ERR_OUT_OF_MEMORY) {
      LOGE("oggz_write_feed: %d", (int)result);
      return false;
    }
    return true;
  }

  virtual bool writeHeader() {
    TRACED();
    AudioInfo cfg = out.audioInfo();
    oh.packet = (uint8_t *)&cfg;
    oh.bytes = sizeof(AudioInfo);
    oh.granulepos = 0;
    oh.packetno = packetno++;
    oh.b_o_s = true;
    oh.e_o_s = false;
    is_audio = false;
    return writePacket(oh);
  }

  virtual bool writeFooter() {
    TRACED();
    op.packet = (uint8_t *)nullptr;
    op.bytes = 0;
    op.granulepos = granulepos;
    op.packetno = packetno++;
    op.b_o_s = false;
    op.e_o_s = true;
    is_audio = false;
    return writePacket(op, OGGZ_FLUSH_AFTER);
  }

  // Final Stream Callback
  static size_t ogg_io_write(void *user_handle, void *buf, size_t n) {
    LOGD("ogg_io_write: %d", (int)n);
    OggContainerEncoder *self = (OggContainerEncoder *)user_handle;
    if (self == nullptr) {
      LOGE("self is null");
      return 0;
    }
    // self->out.write((uint8_t *)buf, n);
    writeSamples<uint8_t>(&(self->out), (uint8_t *)buf, n);
    // 0 = continue
    return 0;
  }
};

}  // namespace audio_tools
