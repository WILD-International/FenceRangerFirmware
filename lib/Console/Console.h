#ifndef SERIAL_CONSOLE_H
#define SERIAL_CONSOLE_H

#include <Arduino.h>

#define MAX_INPUT_LENGTH 128
#define MAX_ARGS 4
#define MAX_COMMANDS 16

typedef void (*CommandHandler)(int argc, const String args[]);

struct Command {
  const char* verb;  // "get", "set", "reset"
  const char* noun;  // "app_key"
  int minArgs;       // minimum token count (verb + noun + params)
  CommandHandler handler;
};

class Console {
 public:
  Console(unsigned long baud = 115200);

  void begin();
  void update();  // Call in loop()

  // Register a command: e.g. addCommand("get", "app_key", 2, cmdGetAppKey);
  bool addCommand(const char* verb, const char* noun, int minArgs, CommandHandler handler);

  // Set a custom prompt (default "> ")
  void setPrompt(const char* prompt);

 private:
  unsigned long _baud;
  const char* _prompt;

  Command _commands[MAX_COMMANDS];
  int _numCommands;

  char _inputBuffer[MAX_INPUT_LENGTH];
  int _inputPos;

  int parseArgs(const String& input, String args[], int maxArgs);
  void processCommand(const String& input);
  void printHelp();
};

#endif
