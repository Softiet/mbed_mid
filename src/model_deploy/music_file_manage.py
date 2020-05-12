import numpy as np
import serial
import time
import re


# send the waveform table to K66F
serdev = '/dev/ttyACM5'
s = serial.Serial(serdev)

s.reset_input_buffer()
s.reset_output_buffer()
print("All system go, pending")

print("Copy Cat mode on")
song_list = []

class Music_file:
    def __init__(self,name,sheet,note_length,speed,beat1,beat2):
        self.name = name
        self.sheet = sheet
        self.note_length = note_length
        self.speed = speed
        self.beat1 = beat1
        self.beat2 = beat2
    def get_name(self):
        return self.name
    def get_note(self):
        return self.sheet
    def get_length(self):
        return self.note_length
    def get_speed(self):
        return self.speed
    def get_beat1(self):
        return self.beat1
    def get_beat2(self):
        return self.beat2


songs = []
songs.append(Music_file("USSR Anthem",
   [392, 523, 392, 440, 494, 329, 329,
    440, 392, 349, 392, 261, 261, 293,
    293, 329, 349, 349, 392, 440, 494,
    523, 587, 392, 659, 587, 523, 587,
    494, 392, 523, 494, 440, 494, 329,
    329, 440, 392, 349, 392, 261, 261,
    523, 494, 440, 392],
   [2, 4, 3, 1, 4, 2, 2,
    4, 3, 1, 4, 2, 2, 4,
    3, 1, 4, 3, 1, 4, 2,
    2, 6, 2, 4, 3, 1, 4,
    2, 2, 4, 3, 1, 4, 2,
    2, 4, 3, 1, 4, 2, 2,
    4, 3, 1, 8],
    0.5,
    [3,11,19,27,35,43,51],
    [53,55,57]
))

songs.append(Music_file("Astronomia",
   [494, 440, 415, 329, 370, 370, 554,
    494, 440, 415, 415, 415, 494, 440,
    415, 370, 370, 880, 830, 880, 830,
    880, 370, 370, 880, 830, 880, 830,
    880, 370, 370, 554, 494, 440, 415,
    415, 415, 494, 440, 415, 370, 370,
    880, 830, 880, 830, 880, 370, 370,
    880, 830, 880, 830, 880],
   [1, 1, 1, 1, 2, 1, 1,
    2, 2, 2, 1, 1, 2, 1,
    1, 2, 1, 1, 1, 1, 1,
    1, 2, 1, 1, 1, 1, 1,
    1, 2, 1, 1, 2, 2, 2, 
    1, 1, 2, 1, 1, 2, 1, 
    1, 1, 1, 1, 1, 2, 1, 
    1, 1, 1, 1, 1],
    0.06,
    [],
    []
))

songs.append(Music_file("I wanna hold your hand",
   [587, 659, 587, 523, 494, 494, 587,
    523, 494, 440,   1, 494, 494, 494,
    494, 494, 370, 659, 587, 523, 494, 
    494, 587, 523, 494, 440,   1, 494, 
    494, 494, 494, 494, 988, 784, 740, 
    659, 587, 523, 587, 523, 494, 440, 
    494, 440, 392,   1, 784, 740, 659, 
    587, 440, 392],
   [2, 2, 3, 1, 8, 2, 2,
    3, 1, 4,10, 2, 2, 2,
    4, 4,12, 4, 3, 1, 8, 
    2, 2, 3, 1, 4,10, 2,
    2, 2, 4, 2,20, 2, 2, 
    2, 4, 4, 3, 1, 3, 1, 
    1, 1, 8, 2, 2, 2, 2, 
    4, 4, 16],
    0.1,
    [],
    []
))

for i in songs:
    song_list.append(i.get_name())

"""print(songs[0].get_name())"""

a= ""
log = []
while(1):
    try:
        new_word = s.read(1).decode("utf-8")
    except:
        new_word = ''
    a = a + new_word
    if(a[-1] == "\r"):
        log.append(a)
        print(a)
        if("op_1\n\r" in a):
            print("op1 recieved!\ntransmit song list!")
            s.reset_output_buffer()
            s.write((str(len(song_list))+"\n").encode("utf-8"))
            time.sleep(0.1)
            for i in song_list:
                print("Name send: "+i)
                time.sleep(0.1)
                s.write((i+'\n').encode("utf-8"))
            s.write('\n'.encode("utf-8"))
        if("op_2" in a):
            print("op2 recieved!")
            print(a.split())
            s.reset_output_buffer()
            index = int(a.split()[1])
            print("song " +str(index) + " selected")
            print("length:" + str(len(songs[index].get_note())))
            s.write((str(len(songs[index].get_note()))+'\n').encode('utf-8'))
            time.sleep(0.1)

            for i in songs[index].get_note():
                s.write((str(i)+'\n').encode('utf-8'))
                time.sleep(0.05)
            time.sleep(0.1)
            for i in songs[index].get_length():
                s.write((str(i)+'\n').encode('utf-8'))
                time.sleep(0.05)
            s.write((str(songs[index].get_speed())+'\n').encode('utf-8'))
            time.sleep(0.05)
            s.write((str(len(songs[index].get_beat1()))+'\n').encode('utf-8'))
            time.sleep(0.05)
            for i in songs[index].get_beat1():
                s.write((str(i)+'\n').encode('utf-8'))
                time.sleep(0.05)
            s.write((str(len(songs[index].get_beat2()))+'\n').encode('utf-8'))
            time.sleep(0.05)
            for i in songs[index].get_beat2():
                s.write((str(i)+'\n').encode('utf-8'))
                time.sleep(0.05)


    if(a[-1]=='\r'):
        a = ""
    
        
