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

Thread uLCD_thread(osPriorityNormal,0x10000);
Thread gesture_thread(osPriorityNormal,0x10000);
Thread button_thread(osPriorityNormal,0x1000);
Thread test_thread;
Thread audio_thread;


EventQueue queue_audio(32 * EVENTS_EVENT_SIZE);
EventQueue queue_gesture(32 * EVENTS_EVENT_SIZE);
EventQueue queue_button(32 * EVENTS_EVENT_SIZE);
EventQueue queue_uLCD(32 * EVENTS_EVENT_SIZE);

void uLCD_update();

void gesture_procedure();

void test_func(){
  while(true){
    led1 = !led1;
    pc.printf("hello from LED1\n");
    wait(0.1);
  }
}

bool pause = false;
int current_mode = 0;
// 0: play

void Trig_pause() {
    // Safe to use 'printf' in context of thread 't', while IRQ is not.
    pc.printf("paused!");
    led1 = 0;
    led2 = 0;
    led3 = 0;
    pause_state = true;
    queue_uLCD.call(uLCD_update);
    // queue_uLCD.dispatch();
}

void Trig_confirm() {
    // Safe to use 'printf' in context of thread 't', while IRQ is not.
    pc.printf("confirmed!");
    led1 = 1;
    led2 = 1;
    led3 = 1;
    pause_state = false;
    queue_uLCD.call(uLCD_update);
    // queue_uLCD.dispatch();
}


DA7212 audio;

//int16_t waveform[kAudioTxBufferSize];

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

/*
void playNote(int freq){
  for(int i = 0; i < kAudioTxBufferSize; i++){
    waveform[i] = (int16_t) (sin((double)i * 2. * M_PI/(double) (kAudioSampleFrequency / freq)) * ((1<<16) - 1));
  }
  audio.spk.play(waveform, kAudioTxBufferSize);
}
*/



int main(int argc, char* argv[]) {

  led1 = 1;
  led2 = 1;
  led3 = 1;

  // uLCD.cls();
  // uLCD.printf("Initialize");
  

  button_thread.start(callback(&queue_button, &EventQueue::dispatch_forever));
  
  sw2.rise(queue_button.event(Trig_pause));
  sw3.rise(queue_button.event(Trig_confirm));
  

  uLCD_thread.start(callback(&queue_uLCD, &EventQueue::dispatch_forever));

  pc.printf("Main Entered\n");

  gesture_thread.start(gesture_procedure);

  // test_thread.start(test_func);
  /*
  audio_thread.start(callback(&queue_audio, &EventQueue::dispatch_forever));

  for(int i = 0; i < 42; i++){
    int length = noteLength[i];
    while(length--){
      // the loop below will play the note for the duration of 1s
      for(int j = 0; j < kAudioSampleFrequency / kAudioTxBufferSize; ++j){
        queue_audio.call(playNote, song[i]);
      }
      if(length < 1) wait(1.0);
    }
  }
  */
  pc.printf("hello?\n");
  // queue_gesture.call()
  

}


void uLCD_update(){
  uLCD.cls();
  if(pause_state){
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
  else{
    uLCD.color(GREEN);
    uLCD.printf("currently playing\n");

  }

}

void gesture_procedure(){
  pc.printf("Gesture Procedure called\n");
  
  uLCD.cls();
  
  uLCD.printf("Gesture Procedure\n\r");
  pc.printf("uLCD function called\n\r");
  
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
          mode_selected = mode_selected==3?0:mode_selected+1;
          uLCD_update();
        }  
      }
    }

  }
}
