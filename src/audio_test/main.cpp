#include "mbed.h"
#include <cmath>
#include "DA7212.h"


DA7212 audio;
InterruptIn sw2(SW2);
InterruptIn sw3(SW3);

int16_t waveform[kAudioTxBufferSize];

EventQueue queue(32 * EVENTS_EVENT_SIZE);
EventQueue audio_master_queue(32 * EVENTS_EVENT_SIZE);
EventQueue audio_control_queue(32 * EVENTS_EVENT_SIZE);

Thread t(osPriorityLow);
Thread audio_master;
Thread audio_control;

int audio_clear = 0;

DigitalOut led1(LED1);
DigitalOut led2(LED2);

Serial pc( USBTX, USBRX );

int song[42] = {
  261, 261, 392, 392, 440, 440, 392,
  349, 349, 330, 330, 294, 294, 261,
  392, 392, 349, 349, 330, 330, 294,
  392, 392, 349, 349, 330, 330, 294,
  261, 261, 392, 392, 440, 440, 392,
  349, 349, 330, 330, 294, 294, 261};

int noteLength[42] = {
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2};

void playNote(int freq){
  for(int i = 0; i < kAudioTxBufferSize; i++){
    waveform[i] = (int16_t) (sin((double)i * 2. * M_PI/(double) (kAudioSampleFrequency / freq)) * ((1<<16) - 1));
  }
  audio.spk.play(waveform, kAudioTxBufferSize);
}

void play_song(){
  pc.printf("we are now in play_song function\n\r");
  led1 = 0;
  led2 = 1;
  audio_clear = 0;
  for(int i = 0; i < 42; i++){
    int length = noteLength[i];
    while(length--){
      // the loop below will play the note for the duration of 1s
      for(int j = 0; j < kAudioSampleFrequency / kAudioTxBufferSize; ++j){
        queue.call(playNote, song[i]);
        if(audio_clear == 1){
          pc.printf("we left play_song function\n\r");
          return;
        }
      }
      if(length < 1) wait(1.0);
    }
  }
}


void stop_song(){
  led1= 1;
  led2 = 0;
  audio.spk.pause();
  audio_clear = 1;
  
}

void reset_song(){
  pc.printf("In reset_song function\n\r");
  pc.printf("state:%d\n\r",audio_master.get_state());
  led1= 0;
  led2 = 1;
  audio_clear = 0;
  // audio.spk.play();
  // audio_master.start(play_song);
  audio_master_queue.call(play_song);
  pc.printf("we left reset_song function\n\r");
  return ;
}

int main(void){
  led1 = 1;
  led2 = 1;
  t.start(callback(&queue, &EventQueue::dispatch_forever));

  audio_control.start(callback(&audio_control_queue, &EventQueue::dispatch_forever));
  audio_master.start(callback(&audio_master_queue, &EventQueue::dispatch_forever));
  pc.printf("%d\n\r",audio_master.get_state());

  sw2.rise(audio_control_queue.event(stop_song));
  sw3.rise(audio_control_queue.event(reset_song));
}