// PinCLI
// Mike Brindley
//
// **********
//
// A major reworking of
// https://www.norwegiancreations.com/2018/02/creating-a-command-line-interface-in-arduinos-serial-monitor/
// to more closely follow with some previous things I've done and to greatly reduce RAM usage (and to 
// do something useful).
//
// The code in the Norwegian Creations link above has issues. 
//    1.  It depends on the serial port timeout. If you are typing and pause too long 
//        (default is 1 second), then it tries to parse the partial command.  It has 
//        no way of knowing if an _actual_ end of command has been received.
//
//    2.  The serial input uses 'readStringUntil("\n")', saying return a string when the
//        end of line is encountered.  In the comments, the author says to set the Arduino
//        IDE terminal to use 'no line ending'.  So, every command waits for the timeout.
//
//    3.  If you don't use 'no line ending', the commands all fail with an error 
//        (possibly because of extra characters?) I didn't fully investigate this situation.
//
//    4.  To simplify things, the code does not have editing for the command line; it depends
//        on the Arduino IDE terminal batch mode text transmission to allow you to do the editing.  
//        Most terminal programs don't have a batch mode transmit, so this makes use with
//        them more difficult.
//
//    5. The code has rather heavy RAM usage which is a scarce resource on small microcontrollers.
//
// **********
//
// Exercises basic pin functions using typed commands (CLI) over the serial port.
//
// I went looking for a CLI (Command Line Interface) for arduino and I only found one.  
// I had trouble believing that after all this time, there wasn't an arduino CLI program
// which would exercise basic functions like setting pins and such.  So, I created this.
//
// This code was written and tested on an Arduino Nano (compatible).  You need to know 
// the hardware you are using.
//
// Mike Brindley
// 4 January 2021
//
// Included basic editing using backspace.  More complicated editing requires more code 
// and either a fixed terminal type or something like the Unix curses package and 
// terminal services.
//
// To make the code easier to translate for general embedded use, I tried to stick to 
// plain C code, minimizing C++ facilities, where possible.  Arduino forces a bit of C++
// type usuage on you.
//
// The Arduino F() macro forces data to be stored in program (FLASH) memory.  I use it 
// to take string literals out of precious RAM.  There is a small execution speed penalty
// for this, but when you only have 2K RAM, the speed penalty is probably worth paying.
// The F() macro is only valid in some situations - the compiler will complain.
//
// ***Note:  Terminal program should be set to use either CR or CR/LF line endings (which  
// are the most common defaults for such programs).

// 1 August 2021 Mike Brindley
// SPI is working with 74HC595

// 20 August 2021 Mike Brindley
// Moved initial memset to clear receive buffer before printing.
// This seems to fix the issue with initial garbage characters being transmitted
// after power up.
//
// 10 December 2021 Mike Brindley
// Now reuses a single args buffer instead of an array.
// Updated debug printing macros to accept variable arguments.
//
// 22 August 2025 Mike Brindley
// v0.36
// Added #ifdef to show how to account for board differences.

#include <SPI.h>

#define PINCLI_VER (F("x0.36"))

// If defined, a bunch of debug output is included.  
//#define PCLI_DEBUG (1)

#ifdef PCLI_DEBUG
   #define pdbg_print(...) Serial.print(__VA_ARGS__)
   #define pdbg_println(...) Serial.println(__VA_ARGS__)
   #define pdbg_Vprintln(v) Serial.print("< "#v " = "); Serial.print(v); Serial.println(F(" >"));
   #define pdbg_Fprint() Serial.print(__func__);
#else
   #define pdbg_print(x)   
   #define pdbg_println(x) 
   #define pdbg_Vprintln(v)
   #define pdbg_Fprint()

#endif

#define LINE_BUF_SIZE 64   //Maximum input string length
#define ARG_BUF_SIZE 16     //Maximum argument/token string length
#define MAX_NUM_ARGS 4      //Maximum number of arguments/tokens

#define CR_CHAR     ((char)13)
#define LF_CHAR     ((char)10)
#define BKSP_CHAR   ((char)8)

#define CLI_PROMPT  ("> ")

// SPI Master default parameters - adjust as needed
// Note: almost every SPI peripheral can do 1 MHz
#define SPI_SPEED (1000000)
#define SPI_ORDER (MSBFIRST)
#define SPI_MODE (SPI_MODE0)

// Check https://docs.arduino.cc/language-reference/en/functions/communication/SPI/
// for table of which Arduino boards use which pins for SPI
//
// This discussion:  https://arduino.stackexchange.com/questions/21137/arduino-how-to-get-the-board-type-in-code
// talks about the board identifiers available at compile time.
#ifdef ARDUINO_AVR_MEGA2560
   #define SPI_SSPIN (53)
#else
// Most AVR Arduinos use pin 10 for Slave Select
   #define SPI_SSPIN (10)
#endif

bool error_flag = false;
 
char line_buf[LINE_BUF_SIZE];
char args[ARG_BUF_SIZE];
static char *cp = line_buf;
 
//Function declarations
int cmd_delay(); // Delay for specified milliseconds
int cmd_help();
int cmd_pmode(); // Set pin mode
int cmd_pr();    // pin read 
int cmd_pw();    // pin write
int cmd_spiend();  // End SPI transaction
int cmd_spirw();   // Send/read one byte over SPI
int cmd_spirw16();  // Send/read two bytes over SPI
int cmd_spistart(); // Start SPI transaction

// *** TODO: put command stuff in structures for ease of access, instead of seperate arrays.

// List of functions pointers corresponding to each command
// Because of not using structures, the separate arrays for command information
// need to be in same command order.
int (*commands_func[])() =
{
    &cmd_delay,
    &cmd_help,
    &cmd_pmode,
    &cmd_pr,
    &cmd_pw,
    &cmd_spiend,
    &cmd_spirw,
    &cmd_spirw16,
    &cmd_spistart
        
};
 
// List of command names
// Commands should be in alphabetical order to allow early drop out of search.
// (More important as list gets longer.)
//
// Make sure the listing order for command names matches the order in commands_func[].
// Using structures to tie together all needed info for a single command would make 
// this order matching unnecessary.
//
// To avoid case confusion, all commands are listed here as lowercase and the characters
// coming in are forced to lowercase.
static const char *commands_str[] = 
{
    "delay",
    "help",
    "pmode",
    "pr",
    "pw",
    "spiend",
    "spirw",
    "spirw16",
    "spistart"
};

// List of pmode sub commands
static const char *pmode_args[] =
{
    "i",  // input
    "ip", // input pullup
    "o"   // output
};

// spistart arg2 (Data Order) values
static const char *spistart_arg1[] =
{
    "lsbfirst",
    "msbfirst"
};

// spistart arg3 (spi mode) values
static const char *spistart_arg2[]=
{
    "mode0",
    "mode1",
    "mode2",
    "mode3"
};

 
static const int num_commands = sizeof(commands_str) / sizeof(char *);
 
void setup() {

    Serial.begin(9600);  // 57600 bit rate is what Arduino programming uses
                          // -> Should be safe

    PinCLI_init();
}
 
void loop()
{
    PinCLI();

    // Note:  You may put other tasks here, if needed.  Try not to have them take too much
    //    time or the serial buffer can overflow, dropping characters.
    
}

void PinCLI_init()
{
    memset(line_buf, 0, LINE_BUF_SIZE);  // Clear line buffer

    Serial.print(F("PinCLI v"));
    Serial.println(PINCLI_VER);
    pdbg_println(F("Debug output enabled."));
    
    send_prompt();
}

// Command Line prompt
void send_prompt()
{
   Serial.print(CLI_PROMPT);
}
 
void PinCLI()
{
    int ret;  
    
    ret = read_input();

    if( ret > 0)
    {
       pdbg_print(F("CMD: "));
       pdbg_print(line_buf);
       pdbg_print(F("  "));
       pdbg_print(F("("));
       pdbg_print( strlen(line_buf));
       pdbg_print(F(")"));
       pdbg_print(CR_CHAR);
       pdbg_print(LF_CHAR);

       parse_cmd();

       if(!error_flag)
          {
          execute();
          }

       // Initialize for next command
       memset(line_buf, 0, LINE_BUF_SIZE);  // Clear line buffer
       cp = line_buf;

       memset(args, 0, ARG_BUF_SIZE);  // Clear arguments
          
       send_prompt();
    }
   
    error_flag = false;
}

  
// read_input()
//     
//    Terminal should treat a received CR as a CR/LF pair
//
// return:
//   1 when CR (end of command) detected 
//   0 otherwise
int read_input()
{
    //String line_string;
    byte c;
 
    while(Serial.available())
    {
       c = (byte) Serial.read();

       if(LF_CHAR == c)
       {
          // Throw away LF
          continue;
       }
       
       // Handle end of command
       if(CR_CHAR == c)
       {
          *cp == '\0'; // Ensure string is terminated
          Serial.write(CR_CHAR);
          Serial.write(LF_CHAR);
          return 1;
       }

       // Handle backspace editing
       if(BKSP_CHAR == c)
       {
          if((cp - line_buf) > 0)
          {
             *(--cp) = '\0';  // Terminate string, erasing last character

             // Patch up terminal line
             Serial.write(BKSP_CHAR); 
             Serial.write(" ");  // Erase character, can't use F() with Serial.write()
             Serial.write(BKSP_CHAR);  // Reposition character
          }
          
          continue;  // already processed character so next while loop
       }

       // Need to use the character, so make it lowercase
       c = tolower(c); // Convert character to lowercase.
       
       if(cp - line_buf < (LINE_BUF_SIZE - 1))
       {
          *cp++ = c;  // Store character in buffer
                      // Don't need to terminate string as memset() already did that
          Serial.write(c);
       }
       else
       {
          // Command line too long, throw it away and start over
          // Current (overflowing) character is saved.
          
          cp = line_buf;
          memset( line_buf, '\0', LINE_BUF_SIZE);

          //Serial.write("\a" + CR_CHAR);  // Requires C++ operator overloading
          Serial.write('\a'); // Alert or bell
          Serial.write(CR_CHAR);
          Serial.write(LF_CHAR);
          send_prompt();

          *cp++ = c;  // Store character in buffer

          Serial.write(c);
          
       }
       
    }  // -- end while() loop

    // If we get here, we don't have a complete command line
    return 0;
}
 
// Note: uses strtok() internally, so for the first call, input string is the buffer to parse,
//    and subsequent calls use NULL for the input string.
//
//     instr (char *) - buffer to be parsed for first token
//     outstr (char*) - buffer to copy token into
//                    - first character (outstr[0]) == 0 if no token found.
void get_token(char *instr, char *outstr)
{
    char *token;

pdbg_print(F("\t")); pdbg_Fprint(); pdbg_println();

    outstr[0] = 0;  // Make sure output string is empty
    
    token = strtok(instr, " ");  

    if(NULL == token)
    {
        pdbg_println("   token is NULL");
        return;  // End of string - early exit
    }
    
    if(strlen(token) < ARG_BUF_SIZE)
    {
        strcpy(outstr, token);
        
        pdbg_print(F("\t"));
        pdbg_print(F("token: "));
        pdbg_println(outstr);
    }
    else
    {
        Serial.println(F("Token too long."));
        error_flag = true;
        return;
    }
}

void parse_cmd()
{
    get_token(line_buf, args);
}

// Probably should return a pointer
int execute()
{  
  // Check for no command specified
  if(NULL == args[0])
  {
    Serial.println(F("(Empty Command)"));
    return 0;
  }
  
  // Search for valid command
  for(int i=0; i<num_commands; i++)
  {
      if(strcmp(args, commands_str[i]) == 0)
      {
          return(*commands_func[i])();
      }
  }
 
  Serial.println(F("Invalid command. Type \"help\" for more."));
  
  return 0;
}

// *** Warning *** Arduino Nano does not use hardware flow control, so using this function 
//    may cause serial buffer overflow - use sparingly!
//
// Could try XON/XOFF here, but that prevents binary transmission from the nano - is that a problem?

int cmd_delay()
{
   long val = 0;

   pdbg_Fprint();
   pdbg_println("");

   // Only argument is delay time in milliseconds
   get_token(NULL, args);
   val = atoi(args);

   pdbg_print(F("\t"));
   pdbg_Vprintln(val);

   delay(val);

   return 0;
}

int cmd_help()
{
    get_token(NULL, args);
    
    if(args[0] == NULL)
    {
        help_help();
    }
    else if(strcmp(args, commands_str[0]) == 0)
    {
        help_delay();
    }
    else if(strcmp(args, commands_str[1]) == 0)
    {
        help_help();
    }
    else if(strcmp(args, commands_str[2]) == 0)
    {
        help_pmode();
    }
    else if(strcmp(args, commands_str[3]) == 0)
    {
        help_pr();
    }
    else if(strcmp(args, commands_str[4]) == 0)
    {
        help_pw();
    }
    else if(strcmp(args, commands_str[5]) == 0)
    {
        help_spiend();
    }
    else if(strcmp(args, commands_str[6]) == 0)
    {
        help_spirw();
    }
    else if(strcmp(args, commands_str[7]) == 0)
    {
        help_spirw16();
    }
    else if(strcmp(args, commands_str[8]) == 0)
    {
        help_spistart();
    }
    else
    {
        help_help();
    }
}
 
void help_help(){
    Serial.println(F("The following commands are available:"));
 
    for(int i=0; i<num_commands; i++)
    {
        Serial.print(F("  "));
        Serial.println(commands_str[i]);
    }
    
    Serial.println(F(""));
    Serial.println(F("You can for instance type \"help pmode\" for more info on the pmode command."));
}
 
void help_delay()
{
   Serial.println(F("  Pause execution for a time.  *** Serial buffer overflow danger! ***"));   
   Serial.println(F("    delay <milliseconds>"));
   Serial.println(F("      <milliseconds> -> number of milliseconds to delay"));
}

//void help_exit()
//{
//    Serial.println(F("This will exit the CLI. To restart the CLI, restart the program."));
//}

void help_pmode()
{
   Serial.println(F("  Set digital pin mode"));
   Serial.println(F("    pmode <pin> <mode>"));
   Serial.println(F("      <pin> -> digital pin number"));
   Serial.println(F("      <mode> -> mode indicator: I, IP, or O"));
}

void help_pr()
{
   Serial.println(F("  Digital pin read"));
   Serial.println(F("    pr <pin>"));
   Serial.println(F("      <pin> -> digital pin number"));
   Serial.println(F("  Returns value (0 or 1) on the specified pin."));
}

void help_pw()
{
   Serial.println(F("  Write value to digital pin"));
   Serial.println(F("    pw <pin> <value>"));
   Serial.println(F("      <pin> -> digital pin number"));
   Serial.println(F("      <value> -> value to write to pin (0 or 1)"));
}

void help_spiend()
{
   Serial.println(F("  End SPI transaction."));  
}

void help_spirw()
{
   Serial.println(F("  Read and Write one byte on SPI"));
   Serial.println(F("    spirw <byte>"));
   Serial.println(F("      <byte> -> number to write, 0 to 255"));
   Serial.println(F("  Returns value read from SPI bus."));
}

void help_spirw16()
{
   Serial.println(F("  Read and Write a 16-bit (2 byte) value on SPI"));
   Serial.println(F("    spirw <value>"));
   Serial.println(F("      <value> -> number to write, 0 to 65535"));
   Serial.println(F("  Returns value read from SPI bus."));
}

void help_spistart()
{
   Serial.println(F("  Start SPI transaction."));  
   Serial.println(F("    spistart <order> <mode>")); 
   Serial.println(F("      <order> -> bit order: lsbfirst or msbfirst")); 
   Serial.println(F("      <mode> -> SPI bus mode:  mode0, mode1, mode2, or mode3")); 
   Serial.println(F("  If arguments are missing or invalid, then defaults are used."));   
}

int cmd_pmode()
{
   int pinnum = 0;

// Error checking?
   // 1st argument is pin number
   get_token(NULL, args);
   pinnum = atoi(args);

   pdbg_print("\t");
   pdbg_println(pinnum);

   // 2nd argument is mode, O, I, or IP
   get_token(NULL, args);

   if(strcmp(args, pmode_args[0]) == 0)
   {
      pdbg_println(F("Input"));
      pinMode(pinnum, INPUT);
      
   }
   else if(strcmp(args, pmode_args[1]) == 0)
   {
      pdbg_println(F("Input-Pullup"));
      pinMode(pinnum, INPUT_PULLUP);
       
   }
   else if(strcmp(args, pmode_args[2]) == 0)
   {
      pdbg_println(F("Output"));
      pinMode(pinnum, OUTPUT);
       
   }
   else 
   {
      pdbg_println(F("No valid type"));
   }

   return 0;
}

int cmd_pr()
{
   int pinnum = 0;
   int val = -1;

   // 1st argument is pin number
   get_token(NULL, args);
   pinnum = atoi(args[1]);

   pdbg_print(F("\t"));
   pdbg_println(pinnum);

   val = digitalRead(pinnum);

   if(LOW == val)
   {
      Serial.println(0);
    
      return 0;
   }

   if(HIGH == val) 
   {
      Serial.println(1);
      
      return 1;
   }
   
   return -1;
}

int cmd_pw()
{
   int pinnum = 0;
   int val = -1;  

   // 1st argument is pin number
   get_token(NULL, args);
   pinnum = atoi(args);

   pdbg_print("\t");
   pdbg_println(pinnum);

   // 2nd argument is digital value, 0 or 1
   get_token(NULL, args);
   val = atoi(args);

   pdbg_print("\t");
   pdbg_println(val);
   
   if(0 == val)
   {
      pdbg_println(F("Write 0"));
      digitalWrite(pinnum, LOW);
      
   }
   else if(1 == val)
   {
      pdbg_println(F("Write 1"));
      digitalWrite(pinnum, HIGH);
       
   }
   else   
   {
      Serial.println(F("Digital pin write value error"));  
   }

   // memset() for args[]?
   return 0;
}

// *********************
//  SPI commands
// *********************

// End SPI transaction
int cmd_spiend()
{
  digitalWrite(SPI_SSPIN, HIGH);  // de-assert Slave Select
  
  SPI.endTransaction();

  return 0;
}

// Read and Write one byte on SPI
int cmd_spirw()
{
  byte write_val;
  byte rx_val;

  get_token(NULL, args);
  write_val = atoi(args);

  pdbg_print("\t");
  pdbg_println(write_val);
  
  rx_val = SPI.transfer(write_val);

  Serial.println(rx_val);
  
  return rx_val;
}

// Read and Write two bytes on SPI
int cmd_spirw16()
{
  int write_val;
  int rx_val;

  get_token(NULL, args);
  write_val = atoi(args);

  pdbg_print("\t");
  pdbg_println(write_val);
  
  rx_val = SPI.transfer16(write_val);

  Serial.println(rx_val);
  
  return rx_val;
}

//
// Note:  if both arguments are missing or invalid, then defaults are used.
int cmd_spistart()
{
  int order, mode;

  // Default argument values
  order = SPI_ORDER;
  mode = SPI_MODE;

// First argument, order
   get_token(NULL, args);

  if(strcmp(args, spistart_arg1[0]) == 0)
  {
    order = LSBFIRST;
    pdbg_println(F("\t -> order = LSBFIRST"));
  }
  else if (strcmp(args, spistart_arg1[1]) == 0)
  {
    order = MSBFIRST;
    pdbg_println(F("\t -> order = MSBFIRST"));
  }
  else
  {
    // Put error message here, if desired
  }

// Second argument, mode
   get_token(NULL, args);
  if(strcmp(args, spistart_arg2[0]) == 0)
  {
    mode = SPI_MODE0;
    pdbg_println(F("\t -> mode -> SPI_MODE0"));
  }
  else if (strcmp(args, spistart_arg2[1]) == 0)
  {
    mode = SPI_MODE1;
    pdbg_println(F("\t -> mode -> SPI_MODE1"));
  }
  else if (strcmp(args, spistart_arg2[2]) == 0)
  {
    mode = SPI_MODE2;
    pdbg_println(F("\t -> mode -> SPI_MODE2"));
  }
  else if (strcmp(args, spistart_arg2[3]) == 0)
  {
    mode = SPI_MODE3;
    pdbg_println(F("\t -> mode -> SPI_MODE3"));
  }
  else
  {
    // Put error message here, if desired
  }
  
  pinMode(SPI_SSPIN, OUTPUT); // SPI Slave Select pin needs to be an output for SPI Master

  SPI.begin();  // Initialize SPI port
                // Note: For a real application, this would probably be done once only,
                //       not at the start of each transaction.

  SPI.beginTransaction(SPISettings(SPI_SPEED, order, mode));

  digitalWrite(SPI_SSPIN, LOW); // assert Slave Select
  
  return 0;
}

// Probably not a very useful command ...
//int cmd_exit()
//{
//    Serial.println(F("Exiting CLI."));
// 
//    while(1);
//}
