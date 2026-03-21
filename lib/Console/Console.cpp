#include "Console.h"

Console::Console(unsigned long baud) : _baud(baud), _prompt("> "), _numCommands(0), _inputPos(0) {}

void Console::begin() {
  Serial.begin(_baud);
  while (!Serial) {
    ;
  }

  Serial.println("=== Serial Console ===");
  printHelp();
  Serial.print(_prompt);
}

void Console::setPrompt(const char* prompt) {
  _prompt = prompt;
}

bool Console::addCommand(const char* verb, const char* noun, int minArgs, CommandHandler handler) {
  if (_numCommands >= MAX_COMMANDS)
    return false;

  _commands[_numCommands].verb = verb;
  _commands[_numCommands].noun = noun;
  _commands[_numCommands].minArgs = minArgs;
  _commands[_numCommands].handler = handler;
  _numCommands++;
  return true;
}

void Console::update() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      _inputBuffer[_inputPos] = '\0';
      Serial.println();

      String input(_inputBuffer);
      input.trim();
      processCommand(input);

      _inputPos = 0;
      Serial.print(_prompt);
    } else if (c == '\b' || c == 127) {
      if (_inputPos > 0) {
        _inputPos--;
        Serial.print("\b \b");
      }
    } else if (_inputPos < MAX_INPUT_LENGTH - 1) {
      _inputBuffer[_inputPos++] = c;
      Serial.print(c);
    }
  }
}

int Console::parseArgs(const String& input, String args[], int maxArgs) {
  int count = 0;
  int start = 0;
  int len = input.length();

  while (start < len && count < maxArgs) {
    while (start < len && input[start] == ' ') start++;
    if (start >= len)
      break;

    int end = start;
    while (end < len && input[end] != ' ') end++;

    args[count++] = input.substring(start, end);
    start = end;
  }
  return count;
}

void Console::processCommand(const String& input) {
  String args[MAX_ARGS];
  int argc = parseArgs(input, args, MAX_ARGS);

  if (argc == 0)
    return;

  String verb = args[0];
  verb.toLowerCase();

  if (verb == "help") {
    printHelp();
    return;
  }

  String noun = (argc > 1) ? args[1] : "";
  noun.toLowerCase();

  for (int i = 0; i < _numCommands; i++) {
    if (verb == _commands[i].verb && noun == _commands[i].noun) {
      if (argc < _commands[i].minArgs) {
        Serial.print("Error: not enough arguments for '");
        Serial.print(verb);
        Serial.print(" ");
        Serial.print(noun);
        Serial.println("'");
        return;
      }
      _commands[i].handler(argc, args);
      return;
    }
  }

  Serial.print("Unknown command: ");
  Serial.println(input);
  printHelp();
}

void Console::printHelp() {
  Serial.println("Available commands:");
  for (int i = 0; i < _numCommands; i++) {
    Serial.print("  ");
    Serial.print(_commands[i].verb);
    Serial.print(" ");
    Serial.print(_commands[i].noun);
    if (_commands[i].minArgs > 2) {
      Serial.print(" <value>");
    }
    Serial.println();
  }
  Serial.println("  help");
}
