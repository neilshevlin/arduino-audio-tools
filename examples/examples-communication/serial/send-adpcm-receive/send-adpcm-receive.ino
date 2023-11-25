/**
 * @file send-adpcm-receive.ino
 * @author Phil Schatzmann
 * @brief Sending and receiving audio via Serial. You need to connect the RX pin
 * with the TX pin!
 * 
 * We send encoded ADPCM audio over the serial wire.
 *
 * @version 0.1
 * @date 2022-03-09
 *
 * @copyright Copyright (c) 2022
 */

#include "AudioTools.h"
#include "AudioCodecs/CodecADPCM.h" // https://github.com/pschatzmann/adpcm
//#include "AudioLibs/AudioKit.h"

AudioInfo info(8000, 1, 16);
I2SStream out; // or AudioKitStream
SineWaveGenerator<int16_t> sineWave(32000);
GeneratedSoundStream<int16_t> sound(sineWave);

auto &serial = Serial2;
ADPCMEncoder enc(AV_CODEC_ID_ADPCM_IMA_WAV);
ADPCMDecoder dec(AV_CODEC_ID_ADPCM_IMA_WAV);
EncodedAudioStream enc_stream(&serial, &enc);
EncodedAudioStream dec_stream(&out, &dec);
Throttle throttle(enc_stream);
static int frame_size = 498;
StreamCopy copierOut(throttle, sound, frame_size);  // copies sound into Serial
StreamCopy copierIn(dec_stream, serial, frame_size);     // copies sound from Serial

void setup() {
  Serial.begin(115200);
  AudioLogger::instance().begin(Serial, AudioLogger::Info);

  // Note the format for setting a serial port is as follows:
  // Serial.begin(baud-rate, protocol, RX pin, TX pin);
  Serial2.begin(1000000, SERIAL_8N1);

  sineWave.begin(info, N_B4);
  throttle.begin(info);
  enc_stream.begin(info);
  dec_stream.begin(info);

  // start I2S
  auto config = out.defaultConfig(TX_MODE);
  config.copyFrom(info);
  out.begin(config);

  // better visibility in logging
  copierOut.setLogName("out");
  copierIn.setLogName("in");

}

void loop() {
  // copy to serial
  copierOut.copy();
  // copy from serial
  copierIn.copy();
}