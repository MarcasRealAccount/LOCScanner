# LOCScanner
A program that scans for the number of lines in files using regex filters for inclusions and exclusions

## How to build?
First get yourself a compiler, as long as it supports C++17.<br>
Then compile the file as you would a normal cpp file :)<br>
Now run it :>

### Windows with Visual Studio
Make a visual studio project, add the only cpp file and hit `Ctrl+B` or Build, whatever you prefer

### GCC
`g++ -O3 -std=c++17 LOCScanner.cpp -o LOCScanner`

### Clang
`clang++ -O3 -std=c++17 LOCScanner.cpp -o LOCScanner`

## How to use?
If you have not built it already go do that.<br>
You can now run it with the `LOCScanner` command.<br>
To see all arguments that can be passed to `LOCScanner` do `LOCScanner -h`, that will give something like this:<br>
```
LOCScanner Usage: '"LOCScanner.exe" {StartPath} {Args {value} ...}'
LOCScanner Flags:
  '-h' Show this help information
  '-i <regex>' Add include filter
  '-e <regex>' Add exclude filter
  '-l' Follow links
  '-d <MaxDepth>' Set max folder depth
  '-print_num_chars' Print the number of characters
  '-print_num_words' Print the number of words
  '-print_num_bytes' Print the number of bytes
  '-print_waste_rate' Print the waste rate (num chars / num bytes) in percent
  '-print_everything' Print all data
  '-print_files' Print on each file
```
`-print_num_bytes` prints the file size, and the total size of all files.
`-print_waste_rate` this shouldn't tell you to remove all whitespace characters as they are nice to have, just not too many around `30%` is good.
`-print_everything` is just a collection of `-print_num_chars`, `-print_num_words`, `-print_num_bytes` and `-print_waste_rate`.

### Examples:
* `LOCScanner -i ".*\.(cpp|cc|c|hpp|h|inl)"` will look for all files ending with `.cpp`, `.cc`, `.c`, `.hpp`, `.h` and `.inl` from where the command is called.
* `LOCScanner -i ".*\/Folder\/.* -e ".*\/Folder\/InFolder\/.*` will match all files inside a directory called `Folder/` but excludes files in the sub directory `InFolder/`.
* `LOCScanner` will match every file from where the command is called.
* `LOCScanner Source` will match every file from the `Source/` directory from where the command is called.
* `LOCScanner -print_everything` will match every file and print all data.

## How does it work?
The way this program works is that it recursively looks at all files.<br>
Gets the filename and checks if that filename is excluded (using `-e <regex>`), or included (using `-i <regex>`).<br>
If the filename was excluded, the file is skipped and nothing else happens.<br>
If the filename wasn't excluded and was included, the file is further processed.<br>

When the file is being processed it reads every character.<br>
It first checks if the character is a `\n` (newline) if so it checks how many characters there was in that line and if that number was above zero it adds 1 to how many words there are in that line, then it checks how many words there was in that line and if that number was above zero it adds 1 to the number of lines in that file.<br>
If the character is not a `\n` (newline) and is not a whitespace then it adds 1 to the number of characters there is in the current line.<br>
If the character is not a `\n` (newline) and is a whitespace then it checks how many characters it was since last time and if that number was above zero it adds 1 to how many words there are in that line.<br>
And then at the very end of the file to make sure all characters, words and lines are accounted for it checks how many characters there was and if that number is above zero it adds 1 to the number of words, then checks how many words there was and if that number is above zero it adds 1 to the number of lines.

The source code of what I tried to describe:
```cpp
uint64_t loc = 0;
uint64_t words = 0;
uint64_t chars = 0;
uint64_t wordCount = 0;
uint64_t charCount = 0;

std::string buffer;
size_t readAmount;
while ((readAmount = readFromStream(fileReader, buffer, 16384, fileSize)) > 0) {
  for (size_t i = 0; i < readAmount; i++) {
    char c = buffer[i];
    if (c == '\n') {
      if (charCount > 0)
        wordCount++;
      if (wordCount > 0)
        loc++;
      words += wordCount;
      wordCount = 0;
      charCount = 0;
    } else {
      if (std::isspace(static_cast<uint8_t>(c))) {
        if (charCount > 0)
          wordCount++;
        chars += charCount;
        charCount = 0;
      } else {
        charCount++;
      }
    }
  }
}
if (charCount > 0)
  wordCount++;
chars += charCount;
if (wordCount > 0)
  loc++;
words += wordCount;
filesInDirectories[filepath.parent_path()]++;
totalFiles++;
totalBytes += fileSize;
totalLOC += loc;
totalWords += words;
totalChars += chars;
```
