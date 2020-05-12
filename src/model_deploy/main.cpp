#include "accelerometer_handler.h"
#include "config.h"
#include "magic_wand_model_data.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"


#include <mbed.h>
#include "uLCD_4DGL.h"

#include <cmath>
#include "DA7212.h"

#include <string.h>
#include <vector>

#define bufferLength (32)
#define signalLength (1024)

DA7212 audio;
uLCD_4DGL uLCD(D1, D0, D2);


// Return the result of the last prediction
int PredictGesture(float* output) {
  // How many times the most recent gesture has been matched in a row
  static int continuous_count = 0;
  // The result of the last prediction
  static int last_predict = -1;
  // Find whichever output has a probability > 0.8 (they sum to 1)
  int this_predict = -1;

  for (int i = 0; i < label_num; i++) {
    if (output[i] > 0.8) this_predict = i;
  }


  // No gesture was detected above the threshold

  if (this_predict == -1) {
    continuous_count = 0;
    last_predict = label_num;
    return label_num;
  }

  if (last_predict == this_predict) {
    continuous_count += 1;
  } else {
    continuous_count = 0;
  }
  last_predict = this_predict;

  // If we haven't yet had enough consecutive matches for this gesture,
  // report a negative result

  if (continuous_count < config.consecutiveInferenceThresholds[this_predict]) {
    return label_num;
  }
  // Otherwise, we've seen a positive result, so clear all our variables
  // and report it
  continuous_count = 0;
  last_predict = -1;

  return this_predict;
}

int detected_gesture;



DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);

InterruptIn sw2(SW2);
InterruptIn sw3(SW3);

Serial pc(USBTX, USBRX);

bool pause_state = false;
int mode_selected = 0;
// 0: forward
// 1: backward
// 2: change song
// 3: Taiko
// 4: Song selection menu
bool pause = false;
int current_mode = 0;

int current_song = 0;
int song_selected = 0;

int audio_clear = 1;
int number_of_songs;
std::vector<string> song_list{"hi","It's me","Mario","You shall not pass"};
std::vector<string> mode_names{
  "Forward Mode",
  "Backward Mode",
  "Song Selection(Unseenable)",
  "Taiko Mode",
  "Song Menu(Unseenable)"
};

int taiko_score = 0;
int hit1 = 0;
int hit2 = 0;

Thread uLCD_thread(osPriorityHigh,0x8000);
Thread gesture_thread(osPriorityNormal,0x10000);
Thread button_thread(osPriorityNormal,0x1000);
Thread audio_sub_thread(osPriorityNormal,0x2000);
Thread audio_thread(osPriorityHigh,0x8000);


EventQueue queue_audio(32 * EVENTS_EVENT_SIZE);
EventQueue queue_sub_audio(32 * EVENTS_EVENT_SIZE);
EventQueue queue_gesture(32 * EVENTS_EVENT_SIZE);
EventQueue queue_button(32 * EVENTS_EVENT_SIZE);
EventQueue queue_uLCD(32 * EVENTS_EVENT_SIZE);

void uLCD_update();
void uLCD_taiko_score_update();
void uLCD_taiko_indicator_update();
void gesture_procedure();
void play_NOTE();
void play_song();

void request_song_sheet();

void flush_serial_buffer(){ 
  char char1 = 0;
  while (pc.readable()){
    char1 = pc.getc(); 
  } 
  return; 
} 



void Trig_pause() {
    // Safe to use 'printf' in context of thread 't', while IRQ is not.
    pc.printf("paused!\n\r");
    led1 = 0;
    led2 = 0;
    led3 = 0;
    pause_state = true;
    queue_uLCD.call(uLCD_update);
    audio.spk.pause();
    audio_clear = 1;
    // queue_uLCD.dispatch();
}

void Trig_confirm() {
    // Safe to use 'printf' in context of thread 't', while IRQ is not.
    pc.printf("confirmed!\n\r");
    led1 = 1;
    led2 = 1;
    led3 = 1;
    if(mode_selected == 2){
      mode_selected =4;
      current_mode =4;
      uLCD_update();
      return ;
    }
    if(mode_selected == 4){
      mode_selected = 0;
      current_mode = 0;
      if(current_song != song_selected){
        current_song = song_selected;
        request_song_sheet();
      }
    }
    
    audio_clear = 0;
    queue_audio.call(play_song);
    pause_state = false;
    queue_uLCD.call(uLCD_update);
    if(mode_selected == 3){
      taiko_score = 0;
      queue_uLCD.call(uLCD_taiko_score_update);
    }
    pc.printf("audio played \n\r");
    current_mode = mode_selected;
    // queue_uLCD.dispatch();
}

int16_t waveform[kAudioTxBufferSize];

std::vector<int> song;
  /*
  {
  261, 261, 392, 392, 440, 440, 392,
  349, 349, 330, 330, 294, 294, 261,
  392, 392, 349, 349, 330, 330, 294,
  392, 392, 349, 349, 330, 330, 294,
  261, 261, 392, 392, 440, 440, 392,
  349, 349, 330, 330, 294, 294, 261};
*/
std::vector<int> noteLength;
/*[42] = {
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2,
  1, 1, 1, 1, 1, 1, 2
};
*/
std::vector<int> beat1;
std::vector<int> beat2;
float speed = 1.0;
void playNote(int freq){
  for(int i = 0; i < kAudioTxBufferSize; i++){
    waveform[i] = (int16_t) (sin((double)i * 2. * M_PI/(double) (kAudioSampleFrequency / freq)) * ((1<<16) - 1));
  }
  if(audio_clear != 1){
    audio.spk.play(waveform, kAudioTxBufferSize);
  }
  
}

void play_song(){
  pc.printf("we are now in play_song function\n\r");
  int current_bars = 1;
  led1 = 0;
  led2 = 1;
  audio_clear = 0;
  int k1 = 0;
  int k2 = 0;
  for(int i = 0; i < int(song.size()); i++){
    int length = noteLength[i];
    while(length > 0){
      // the loop below will play the note for the duration of 1s
      if(current_mode == 3 && audio_clear!= 1){
        current_bars++;
        if(beat1[k1] == current_bars){
          hit1 = 1;
          k1++;
          led2 = 0;
        }
        else{
          hit1 = 0;
          led2 = 1;
        }
        if(beat2[k2] == current_bars){
          hit2 = 1;
          k2++;
          led3 = 0;
        }
        else{
          hit2 = 0;
          led3 = 1;
        }
        queue_uLCD.call(uLCD_taiko_indicator_update);
      }
      for(int j = 0; j < kAudioSampleFrequency / kAudioTxBufferSize; ++j){
        queue_sub_audio.call(playNote, song[i]);
      }
      if(audio_clear == 1){
        pc.printf("we left play_song function\n\r");
        return;
      }
      if(length > 0){
        wait(speed);
      } 
      length--;
    }
    audio.spk.pause();
    wait(speed/10);
  }
  audio_clear = 1;
  audio.spk.pause();
  return;
}

void stop_song(){
  audio.spk.pause();
  audio_clear = 1;
}

void reset_song(){
  audio_clear = 0;
  queue_audio.call(play_song);
  return ;
}

char serialInBuffer[bufferLength];
int serialCount = 0;

void pc_initialize(){
  pc.printf("hello!\n\r");
  pc.printf("op_1\n\r");
  char temp_c = 0;
  int i =0;
  // load song list
  while(temp_c!='\n'){
    if(pc.readable()){
      temp_c = pc.getc();
      serialInBuffer[i] = temp_c;
      i++;
    }
    serialInBuffer[i] = '\0';
  }
  number_of_songs = int(atoi(serialInBuffer));
  pc.printf("numbers of song set: %d\n\r",number_of_songs);
  
  song_list.clear();
  
  for(int j = 0;j<number_of_songs;j++){
    i = 0;
    temp_c = 0;
    memset(serialInBuffer,0,bufferLength);
    while(temp_c!='\n'){
      if(pc.readable()){
        temp_c = pc.getc();
        serialInBuffer[i] = temp_c;
        i++;
      }
    }
    int k =0;
    pc.printf("song %d :",j);
    while(serialInBuffer[k]!=0){
      pc.printf("%c",serialInBuffer[k]);
      k++;
    }
    song_list.push_back(serialInBuffer);
  }
  pc.printf("\n\r");
}

void request_song_sheet(){
  flush_serial_buffer();
  pc.printf("op_2 %d\n\r",current_song);
  audio_clear = 1;
  audio.spk.pause();
  // load song sheet
  char temp_c = 0;
  int i =0;
  song.clear();
  noteLength.clear();
  memset(serialInBuffer,0,bufferLength);
  while(temp_c!='\n'){
    if(pc.readable()){
      temp_c = pc.getc();
      serialInBuffer[i] = temp_c;
      i++;
    }
  }

  int total_note_number = atoi(serialInBuffer);
  pc.printf("total note number: %d\n\r",total_note_number);
  
  for(int j = 0;j<total_note_number;j++){
    i = 0;
    temp_c = 0;
    memset(serialInBuffer,0,bufferLength);
    while(temp_c!='\n'){
      if(pc.readable()){
        temp_c = pc.getc();
        serialInBuffer[i] = temp_c;
        i++;
        led2 = !led2;
      }
    }
    song.push_back(atoi(serialInBuffer));
  }
  for(int j = 0;j<total_note_number;j++){
    i = 0;
    temp_c = 0;
    memset(serialInBuffer,0,bufferLength);
    while(temp_c!='\n'){
      if(pc.readable()){
        temp_c = pc.getc();
        serialInBuffer[i] = temp_c;
        i++;
        led1 = !led1;
      }
    }
    noteLength.push_back(atoi(serialInBuffer));
  }
    i = 0;
    temp_c = 0;
    memset(serialInBuffer,0,bufferLength);
    while(temp_c!='\n'){
      if(pc.readable()){
        temp_c = pc.getc();
        serialInBuffer[i] = temp_c;
        i++;
        led3 = !led3;
      }
    }
    speed = atof(serialInBuffer);
  // beat1 
  beat1.clear();
  temp_c = 0;
  i =0;
  memset(serialInBuffer,0,bufferLength);
  while(temp_c!='\n'){
    if(pc.readable()){
      temp_c = pc.getc();
      serialInBuffer[i] = temp_c;
      i++;
    }
  }
  int beat1_counts = atoi(serialInBuffer);
  for(int j = 0;j<beat1_counts;j++){
    i = 0;
    temp_c = 0;
    memset(serialInBuffer,0,bufferLength);
    while(temp_c!='\n'){
      if(pc.readable()){
        temp_c = pc.getc();
        serialInBuffer[i] = temp_c;
        i++;
        led1 = !led1;
      }
    }
    beat1.push_back(atoi(serialInBuffer));
  }
  // beat2 
  beat2.clear();
  temp_c = 0;
  i =0;
  memset(serialInBuffer,0,bufferLength);
  while(temp_c!='\n'){
    if(pc.readable()){
      temp_c = pc.getc();
      serialInBuffer[i] = temp_c;
      i++;
    }
  }
  int beat2_counts = atoi(serialInBuffer);
  for(int j = 0;j<beat2_counts;j++){
    i = 0;
    temp_c = 0;
    memset(serialInBuffer,0,bufferLength);
    while(temp_c!='\n'){
      if(pc.readable()){
        temp_c = pc.getc();
        serialInBuffer[i] = temp_c;
        i++;
        led2 = !led2;
      }
    }
    beat2.push_back(atoi(serialInBuffer));
  }
  pc.printf("transmission complete!\n\r");
}


int main(int argc, char* argv[]) {
  pc.printf("Main Entered\n\r");
  led1 = 1;
  led2 = 1;
  led3 = 1;
  pc_initialize();
  request_song_sheet();
  queue_audio.call(play_song);

  // uLCD.cls();
  // uLCD.printf("Initialize");
  
  button_thread.start(callback(&queue_button, &EventQueue::dispatch_forever));
  
  sw2.rise(queue_button.event(Trig_pause));
  sw3.rise(queue_button.event(Trig_confirm));
  

  uLCD_thread.start(callback(&queue_uLCD, &EventQueue::dispatch_forever));

  gesture_thread.start(gesture_procedure);

  // test_thread.start(test_func);
  
  audio_sub_thread.start(callback(&queue_sub_audio, &EventQueue::dispatch_forever));
  audio_thread.start(callback(&queue_audio, &EventQueue::dispatch_forever));  

}


void uLCD_update(){
  uLCD.cls();
  if(pause_state && current_mode <= 3){
    uLCD.color(GREEN);
    uLCD.printf("mode Selection\n");
    uLCD.color(WHITE);
    if(mode_selected == 0){
      uLCD.textbackground_color(WHITE);
      uLCD.color(BLACK);
    }
    uLCD.printf("Forward\n");
    uLCD.textbackground_color(BLACK);
    uLCD.color(WHITE);
    if(mode_selected == 1){
      uLCD.textbackground_color(WHITE);
      uLCD.color(BLACK);
    }
    uLCD.printf("Backward\n");
    uLCD.textbackground_color(BLACK);
    uLCD.color(WHITE);
    if(mode_selected == 2){
      uLCD.textbackground_color(WHITE);
      uLCD.color(BLACK);
    }
    uLCD.printf("Song selection\n");
    uLCD.textbackground_color(BLACK);
    uLCD.color(WHITE);
    if(mode_selected == 3){
      uLCD.textbackground_color(WHITE);
      uLCD.color(BLACK);
    }
    uLCD.printf("Taiko\n");
    uLCD.textbackground_color(BLACK);
    uLCD.color(WHITE);
  }
  else if(pause_state && current_mode ==4){
    uLCD.color(GREEN);
    uLCD.printf("Song Selection \nMenu");
    uLCD.color(WHITE);
    for(int i =0;i<int(song_list.size());i++){
      if(song_selected == i){
        uLCD.textbackground_color(WHITE);
      }
      uLCD.text_string(song_list[i].c_str(),0,i+4,FONT_7X8,i==song_selected?BLACK:WHITE);
      if(song_selected == i){
        uLCD.textbackground_color(BLACK);
      }
    }
    uLCD.color(GREEN);
  }
  else{
    uLCD.color(GREEN);
    uLCD.text_string("current mode:",0,1,FONT_7X8,GREEN);
    uLCD.text_string(mode_names[current_mode].c_str(),0,2,FONT_7X8,WHITE);
    uLCD.text_string("currently playing:",0,3,FONT_7X8,GREEN);
    uLCD.text_string(song_list[current_song].c_str(),0,4,FONT_7X8,WHITE);
    if(current_mode == 3){
      uLCD.text_string("Score:",0,5,FONT_7X8,GREEN);
    }
  }
}

void uLCD_taiko_score_update(){
  if(current_mode == 3){
    uLCD.text_string("Score:",0,5,FONT_7X8,GREEN);
    uLCD.text_string((std::to_string(taiko_score)).c_str(),0,6,FONT_7X8,WHITE);
  }
}

void uLCD_taiko_indicator_update(){
  if(current_mode == 3){
    uLCD.filled_rectangle(0, 70, 120, 120,BLACK);
    uLCD.text_string((std::to_string(taiko_score)).c_str(),0,6,FONT_7X8,WHITE);
    if(hit1 == 1){
      uLCD.triangle(30,80,10,110,50,110,GREEN);
      pc.printf("indicator triangle\n\r");
    }
    if(hit2 == 1){
      uLCD.line(90,80,90,110,BLUE);
      uLCD.line(80,100,90,110,BLUE);
      uLCD.line(100,100,90,110,BLUE);
      pc.printf("indicator arrow\n\r");
    }
  }
}

void gesture_procedure(){
  pc.printf("Gesture Procedure called\n\r");
  
  uLCD.cls();
  
  // uLCD.printf("Gesture Procedure\n\r");
  // pc.printf("uLCD function called\n\r");
  uLCD_update();
  // Create an area of memory to use for input, output, and intermediate arrays.
  // The size of this will depend on the model you're using, and may need to be
  // determined by experimentation.
  constexpr int kTensorArenaSize = 60 * 1024;
  uint8_t tensor_arena[kTensorArenaSize];
  // Whether we should clear the buffer next time we fetch data
  bool should_clear_buffer = false;
  bool got_data = false;
  // The gesture index of the prediction
  int gesture_index;
  // Set up logging.
  static tflite::MicroErrorReporter micro_error_reporter;
  tflite::ErrorReporter* error_reporter = &micro_error_reporter;
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  const tflite::Model* model = tflite::GetModel(g_magic_wand_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    error_reporter->Report(
        "Model provided is schema version %d not equal "
        "to supported version %d.",
        model->version(), TFLITE_SCHEMA_VERSION);
    return -1;
  }


  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.

  static tflite::MicroOpResolver<6> micro_op_resolver;

  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_MAX_POOL_2D,
                               tflite::ops::micro::Register_MAX_POOL_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                               tflite::ops::micro::Register_FULLY_CONNECTED());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                               tflite::ops::micro::Register_SOFTMAX());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,
                               tflite::ops::micro::Register_RESHAPE(),1);

  // Build an interpreter to run the model with

  //**********  this stuff here has some problem: 
  // *********  solution: allocate a large thread size for it
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);

  tflite::MicroInterpreter* interpreter = &static_interpreter;
  //***********  here ends this error

  // Allocate memory from the tensor_arena for the model's tensors
  interpreter->AllocateTensors();
  // Obtain pointer to the model's input tensor
  TfLiteTensor* model_input = interpreter->input(0);
  if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] != config.seq_length) ||
      (model_input->dims->data[2] != kChannelNumber) ||
      (model_input->type != kTfLiteFloat32)) {
    error_reporter->Report("Bad input tensor parameters in model");
    return -1;
  }

  int input_length = model_input->bytes / sizeof(float);
  TfLiteStatus setup_status = SetupAccelerometer(error_reporter);
  if (setup_status != kTfLiteOk) {
    error_reporter->Report("Set up failed\n");
    return -1;
  }

  error_reporter->Report("Set up successful...\n");
  
  
  while (true) {
    // Attempt to read new data from the accelerometer
    got_data = ReadAccelerometer(error_reporter, model_input->data.f,
                                 input_length, should_clear_buffer);
    // If there was no new data,
    // don't try to clear the buffer again and wait until next time
    if (!got_data) {
      should_clear_buffer = false;
      continue;
    }

    // Run inference, and report any error
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
      error_reporter->Report("Invoke failed on index: %d\n", begin_index);
      continue;
    }
    // Analyze the results to obtain a prediction
    gesture_index = PredictGesture(interpreter->output(0)->data.f);
    // Clear the buffer next time we read data
    should_clear_buffer = gesture_index < label_num;
    // Produce an output
    if (gesture_index < label_num) {
      error_reporter->Report(config.output_message[gesture_index]);

      detected_gesture = gesture_index;
      
      if(pause_state == true){
        if(gesture_index == 0){
          led1 = 0;
          led2 = 1;
          led3 = 1;

        }
        if(gesture_index == 1){
          led1 = 1;
          led2 = 0;
          led3 = 1;
        }
        if(gesture_index == 2){
          led1 = 1;
          led2 = 1;
          led3 = 0;
          if(current_mode <= 3){
            mode_selected = mode_selected==3?0:mode_selected+1;
          }
          if(current_mode == 4){
            song_selected = song_selected == int(song_list.size()-1)?0:song_selected+1;

          }
          uLCD_update();
        }  
      }
      else{
        if(gesture_index == 0){
          if(current_mode == 0){

            current_song = current_song==int(song_list.size()-1)?0:current_song+1;
            request_song_sheet();
            audio.spk.pause();
            audio_clear = 1;
            queue_audio.call(play_song);
            uLCD_update();
          }
          else if(current_mode == 1){
            current_song = current_song == 0?int(song_list.size()-1):current_song-1;
            request_song_sheet();
            audio.spk.pause();
            audio_clear = 1;
            queue_audio.call(play_song);
            uLCD_update();
          }
        }
        if(gesture_index == 1){
          if(current_mode == 3){
            if(hit1 == 1){
              hit1 = -1;
              taiko_score +=10;
            }
          }
        }
        if(gesture_index == 2){
          if(current_mode == 3){
            if(hit2 == 1){
              hit2 = -1;
              taiko_score +=10;
            }
          }
        }
        
      }
    }

  }
}
