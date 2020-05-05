#include <mbed.h>


DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);

InterruptIn sw2(SW2);

EventQueue queue(32 * EVENTS_EVENT_SIZE);

Thread t;

bool pause = false;

void Trig_pause() {
    // Safe to use 'printf' in context of thread 't', while IRQ is not.
    printf("paused!");
}


int main() {
    LED1 = 1;
    LED2 = 1;
    LED3 = 1;
    // t is a thread to process tasks in an EventQueue
    // t call queue.dispatch_forever() to start the scheduler of the EventQueue
    t.start(callback(&queue, &EventQueue::dispatch_forever));
    // 'Trig_led1' will execute in IRQ context
    // sw2.rise(Trig_led1);
    // 'Trig_led2' will execute in context of thread 't'
    // 'Trig_led2' is directly put into the queue
    sw2.rise(queue.event(Trig_pause));

}