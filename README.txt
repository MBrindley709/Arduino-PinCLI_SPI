PinCLI
 
Exercises basic pin functions using typed commands (CLI) over the serial port, allowing you to 
play with the hardware without writing any code.  Not all board/hardware functions are implemented;
if you need them, implementation is on you!

I went looking for a CLI (Command Line Interface) for arduino and I only found one.  
I had trouble believing that after all this time, there wasn't an arduino CLI program
which would exercise basic functions like setting pins and such.  So, I created this.

This code was written and tested on an Arduino Nano (compatible).  

*** You need to know the hardware you are using. ***
*** You are responsible for anything you do with this code. ***

This is a major reworking of
 https://www.norwegiancreations.com/2018/02/creating-a-command-line-interface-in-arduinos-serial-monitor/

to more closely follow with some previous things I've done and to greatly reduce RAM usage (and to 
do something useful).

The code in the Norwegian Creations link above has issues. 
   1.  It depends on the serial port timeout. If you are typing and pause too long 
       (default is 1 second), then it tries to parse the partial command.  It has 
       no way of knowing if an _actual_ end of command has been received.

   2.  The serial input uses 'readStringUntil("\n")', saying return a string when the
       end of line is encountered.  In the comments, the author says to set the Arduino
       IDE terminal to use 'no line ending'.  So, every command waits for the timeout.

   3.  If you don't use 'no line ending', the commands all fail with an error 
       (possibly because of extra characters?) I didn't fully investigate this situation.

   4.  To simplify things, the code does not have editing for the command line; it depends
       on the Arduino IDE terminal batch mode text transmission to allow you to do the editing.  
       Most terminal programs don't have a batch mode transmit, so this makes use with
       them more difficult.

   5. The code has rather heavy RAM usage which is a scarce resource on small microcontrollers.

Here is the result of compiling the Norwegian Creations code with the Arduino IDE 2.3.6 for a Nano board:
   Sketch uses 6912 bytes (22%) of program storage space. Maximum is 30720 bytes.
   Global variables use 1519 bytes (74%) of dynamic memory, leaving 529 bytes for local variables. Maximum is 2048 bytes.

... doesn't leave much RAM left on a Nano.

Here is the result of compiling PinCLI_SPI with the Arduino IDE 2.3.6 for a Nano board:
   Sketch uses 5758 bytes (18%) of program storage space. Maximum is 30720 bytes.
   Global variables use 406 bytes (19%) of dynamic memory, leaving 1642 bytes for local variables. Maximum is 2048 bytes.
   
406 bytes of Ram vs. 1519 bytes of RAM!  Much better!  Even the program space is significantly less.

Scripts directory
-----------------
I connected up the Nano's SPI bus to a couple of 74HC595 8-bit shift registers.  I then connected the 74HC595's
outputs to 16 LEDs which were in 2, 8-LED bargraphs.  The 595_test.txt file contains the sequence of commands 
I copied and pasted (in mass) into Tera Term to test the software out.  It executed to quickly to do a good 
job of testing, so I added the delay commands.  That meant I had to add delays in Tera Term to keep from 
over-running the Nano's serial port buffer.

The 299_test.txt file has the commands for testing with 2 74HC299 8-bit shift registers instead of 74HC595.

Both types of shift registers worked in 2 of the 4 SPI modes.
