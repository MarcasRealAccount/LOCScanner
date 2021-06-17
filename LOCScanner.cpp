#include <csignal>
#include <cstdint>
#include <cstdlib>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _MSC_VER
#define NOMINMAX
#include <Windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif

// Logging helper functions
struct RGBColor {
	constexpr RGBColor(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}

	uint8_t r, g, b;
};

std::ostream& operator<<(std::ostream& stream, RGBColor color) {
	return stream << "\033[38;2;" << static_cast<uint64_t>(color.r) << ";" << static_cast<uint64_t>(color.g) << ";" << static_cast<uint64_t>(color.b) << "m";
}

std::ostream& ResetColor(std::ostream& stream) {
	return stream << "\033[39m";
}

#ifdef _MSC_VER
static HANDLE stdoutHandle;
static DWORD defaultMode;
#endif

void setupAnsi() {
#ifdef _MSC_VER
	stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (stdoutHandle == INVALID_HANDLE_VALUE) return;
	DWORD mode;
	if (!GetConsoleMode(stdoutHandle, &mode)) return;
	defaultMode = mode;
	mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(stdoutHandle, mode);
#endif
}

void restoreAnsi() {
	std::cout << "\033[0m";
#ifdef _MSC_VER
	if (stdoutHandle == INVALID_HANDLE_VALUE) return;
	SetConsoleMode(stdoutHandle, defaultMode);
#endif
}

namespace Colors {
	static constexpr RGBColor White = { 255, 255, 255 };
	static constexpr RGBColor Red = { 255, 0, 0 };
	static constexpr RGBColor Yellow = { 255, 255, 0 };
	static constexpr RGBColor Green = { 0, 255, 0 };
	static constexpr RGBColor Cyan = { 0, 255, 255 };
	static constexpr RGBColor Blue = { 0, 0, 255 };
	static constexpr RGBColor Purple = { 255, 0, 255 };
	static constexpr RGBColor Black = { 0, 0, 0 };

	static constexpr RGBColor Info = { 127, 255, 255 };
	static constexpr RGBColor Warn = { 255, 127, 0 };
	static constexpr RGBColor Error = { 255, 30, 30 };
	static constexpr RGBColor Usage = { 127, 255, 63 };
	static constexpr RGBColor Help = { 127, 255, 255 };
	static constexpr RGBColor Arg = { 255, 255, 66 };
	static constexpr RGBColor Note = { 255, 255, 0 };
	static constexpr RGBColor NoteLabel = { 255, 63, 63 };
}

// LOCScanner
class LOCScanner {
public:
	using CallbackT = std::function<void(const LOCScanner&, const std::filesystem::path&) >;

public:
	void addInclusionFilter(std::string_view filter) {
		this->inclusionFilters.push_back(std::string(filter));
	}

	void addExclusionFilter(std::string_view filter) {
		this->exclusionFilters.push_back(std::string(filter));
	}

	void setStartPath(const std::filesystem::path& startPath) {
		this->startPath = startPath;
	}

	auto& getStartPath() const { return this->startPath; }

	void followLinks() {
		this->bFollowLinks = true;
	}

	void setMaxDepth(uint64_t maxDepth) {
		this->maxDepth = maxDepth;
	}

	void onPreDirectory(CallbackT callback) {
		this->preDirectoryCallback = callback;
	}

	void onPostDirectory(CallbackT callback) {
		this->postDirectoryCallback = callback;
	}

	void onFile(CallbackT callback) {
		this->fileCallback = callback;
	}

	size_t matchFiles(std::error_code* ec = nullptr) {
		if (this->startPath.empty())
			this->startPath = ".";
		std::filesystem::path currentDirectory = this->startPath;

		size_t matches = 0;

		std::filesystem::recursive_directory_iterator itr;
		if (ec)
			itr = std::filesystem::recursive_directory_iterator(currentDirectory, (this->bFollowLinks ? std::filesystem::directory_options::follow_directory_symlink : std::filesystem::directory_options::none) | std::filesystem::directory_options::skip_permission_denied, *ec);
		else
			itr = std::filesystem::recursive_directory_iterator(currentDirectory, (this->bFollowLinks ? std::filesystem::directory_options::follow_directory_symlink : std::filesystem::directory_options::none) | std::filesystem::directory_options::skip_permission_denied);

		if (ec && ec->operator bool())
			return 0;

		if (preDirectoryCallback)
			this->preDirectoryCallback(*this, this->startPath);

		for (auto end = std::filesystem::recursive_directory_iterator(); itr != end; itr++) {
			if (this->maxDepth > 0 && itr->is_directory() && itr.depth() >= this->maxDepth) {
				itr.disable_recursion_pending();
			}

			auto& path = itr->path();
			if (postDirectoryCallback) {
				std::filesystem::path dir;
				if (itr->is_directory())
					dir = path;
				else
					dir = path.parent_path();

				auto relative = std::filesystem::relative(dir, currentDirectory);
				std::vector<std::string> filenames;
				std::regex separatorReplace("\\\\+|\\/\\/+");
				std::string str = relative.string();
				str = std::regex_replace(str, separatorReplace, "/", std::regex_constants::match_any);
				size_t offset = 0;
				while (offset < str.size()) {
					size_t end = str.find_first_of('/', offset);
					filenames.push_back(str.substr(offset, end - offset));
					if (end < str.size())
						offset = end + 1;
					else
						offset = end;
				}

				for (auto& filename : filenames) {
					if (filename == "..") {
						postDirectoryCallback(*this, currentDirectory);
						currentDirectory = currentDirectory.parent_path();
					}
				}

				currentDirectory = dir;
			}

			if (itr->is_directory()) {
				if (preDirectoryCallback)
					this->preDirectoryCallback(*this, path);
			} else if (itr->is_regular_file() && fileCallback) {
				auto relative = std::filesystem::relative(path, this->startPath);
				std::regex separatorReplace("\\\\+|\\/\\/+");
				std::string str = relative.string();
				str = std::regex_replace(str, separatorReplace, "/");
				relative = std::filesystem::path(str);

				if (doesPathMatchFilters(relative, this->exclusionFilters)) {
					continue;
				} else if (this->inclusionFilters.size() == 0) {
					this->fileCallback(*this, relative);
					matches++;
				} else if (doesPathMatchFilters(relative, this->inclusionFilters)) {
					this->fileCallback(*this, relative);
					matches++;
				}
			}
		}

		if (postDirectoryCallback) {
			auto relative = std::filesystem::relative(this->startPath, currentDirectory);
			std::vector<std::string> filenames;
			std::regex separatorReplace("\\\\+|\\/\\/+");
			std::string str = relative.string();
			str = std::regex_replace(str, separatorReplace, "/");
			size_t offset = 0;
			while (offset < str.size()) {
				size_t end = str.find_first_of('/', offset);
				filenames.push_back(str.substr(offset, end - offset));
				if (end < str.size())
					offset = end + 1;
				else
					offset = end;
			}

			for (auto& filename : filenames) {
				if (filename == "..") {
					postDirectoryCallback(*this, currentDirectory);
					currentDirectory = currentDirectory.parent_path();
				}
			}

			postDirectoryCallback(*this, this->startPath);
		}

		return matches;
	}

private:
	bool doesPathMatchFilters(const std::filesystem::path& path, const std::vector<std::string>& filters) {
		std::regex separatorReplace("\\\\+|\\/\\/+");
		std::string str = path.string();
		str = std::regex_replace(str, separatorReplace, "/");

		for (auto& filter : filters)
			if (std::regex_match(str, std::regex(filter.begin(), filter.end())))
				return true;
		return false;
	}

private:
	std::filesystem::path startPath;
	std::vector<std::string> inclusionFilters;
	std::vector<std::string> exclusionFilters;
	bool bFollowLinks = false;
	uint64_t maxDepth = 0;
	CallbackT preDirectoryCallback;
	CallbackT postDirectoryCallback;
	CallbackT fileCallback;
};

size_t streamActualPos(std::istream& stream, size_t max) {
	return std::min(static_cast<size_t>(stream.tellg()), max);
}

size_t readFromStream(std::istream& stream, std::string& buffer, size_t numBytes, size_t filesize) {
	size_t startPosition = streamActualPos(stream, filesize);
	buffer.resize(numBytes);
	stream.read(buffer.data(), numBytes);
	return streamActualPos(stream, filesize) - startPosition;
}

void signalHandler(int s) {
	std::cout << "\n^C\n";
	restoreAnsi();
	exit(SIGINT);
}

int main(int argc, char** argv) {
	int returnCode = EXIT_SUCCESS;
	setupAnsi();
#ifndef _DEBUG
	try {
#endif
		signal(SIGINT, &signalHandler);

		LOCScanner scanner;

		bool printNumChars = false;
		bool printNumWords = false;
		bool printNumBytes = false;
		bool printWasteRate = false;
		bool printFiles = false;

		if (argc > 1) {
			for (int i = 1; i < argc; i++) {
				std::string_view key = argv[i];
				if (key.size() == 0)
					continue;

				if (key[0] == '-') {
					if (key == "-i") {
						if (++i < argc)
							scanner.addInclusionFilter(argv[i]);
					} else if (key == "-e") {
						if (++i < argc)
							scanner.addExclusionFilter(argv[i]);
					} else if (key == "-l") {
						scanner.followLinks();
					} else if (key == "-d") {
						if (++i < argc)
							scanner.setMaxDepth(std::strtoull(argv[i], nullptr, 10));
					} else if (key == "-print_num_chars") {
						printNumChars = true;
					} else if (key == "-print_num_words") {
						printNumWords = true;
					} else if (key == "-print_num_bytes") {
						printNumBytes = true;
					} else if (key == "-print_waste_rate") {
						printWasteRate = true;
					} else if (key == "-print_everything") {
						printNumChars = true;
						printNumWords = true;
						printNumBytes = true;
						printWasteRate = true;
					} else if (key == "-print_files") {
						printFiles = true;
					} else if (key == "-h") {
						std::ostringstream helpString;
						helpString << Colors::Usage << "LOCScanner Usage: '\"" << argv[0] << "\" {StartPath} {Args {value} ...}'\n" << ResetColor;
						helpString << Colors::Help << "LOCScanner Flags:\n" << ResetColor;
						helpString << Colors::Arg << "\t'-h'" << Colors::Info << " Show this help information\n" << ResetColor;
						helpString << Colors::Arg << "\t'-i <Regex>'" << Colors::Info << " Add include filter\n" << ResetColor;
						helpString << Colors::Arg << "\t'-e <Regex>'" << Colors::Info << " Add exclude filter\n" << ResetColor;
						helpString << Colors::Arg << "\t'-l'" << Colors::Info << " Follow links\n" << ResetColor;
						helpString << Colors::Arg << "\t'-d <MaxDepth>'" << Colors::Info << " Set max folder depth\n" << ResetColor;
						helpString << Colors::Arg << "\t'-print_num_chars'" << Colors::Info << " Print the number of characters\n" << ResetColor;
						helpString << Colors::Arg << "\t'-print_num_words'" << Colors::Info << " Print the number of words\n" << ResetColor;
						helpString << Colors::Arg << "\t'-print_num_bytes'" << Colors::Info << " Print the number of bytes\n" << ResetColor;
						helpString << Colors::Arg << "\t'-print_waste_rate'" << Colors::Info << " Print the waste rate (num chars / num bytes) in percent\n" << ResetColor;
						helpString << Colors::Arg << "\t'-print_everything'" << Colors::Info << " Print all data\n" << ResetColor;
						helpString << Colors::Arg << "\t'-print_files'" << Colors::Info << " Print on each file\n" << ResetColor;
						std::cout << helpString.str();
						restoreAnsi();
						exit(EXIT_SUCCESS);
					} else {
						std::cout << Colors::Warn << "Warning: Argument " << Colors::Arg << "'" << key << "' " << Colors::Warn << "Is not a recognized argument!\n";
					}
				} else {
					scanner.setStartPath(key);
				}
			}
		}

		std::map<std::filesystem::path, uint64_t> filesInDirectories;
		uint64_t totalFiles = 0;
		uint64_t totalBytes = 0;
		uint64_t totalLOC = 0;
		uint64_t totalWords = 0;
		uint64_t totalChars = 0;

		scanner.onFile([&](const LOCScanner& scn, const std::filesystem::path& filepath) {
			std::ifstream fileReader(scn.getStartPath() / filepath, std::ios::binary | std::ios::ate);
			if (fileReader) {
				size_t fileSize = fileReader.tellg();
				fileReader.seekg(0);
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

				fileReader.close();
				if (printFiles && loc > 0) {
					std::ostringstream fileInfoString;
					fileInfoString << Colors::Info << "LOC: " << Colors::Arg << "'" << loc << "'";
					if (printNumWords)
						fileInfoString << Colors::Info << ", Words: " << Colors::Arg << "'" << words << "'";
					if (printNumChars)
						fileInfoString << Colors::Info << ", Chars: " << Colors::Arg << "'" << chars << "'";
					if (printNumBytes)
						fileInfoString << Colors::Info << ", Bytes: " << Colors::Arg << "'" << fileSize << "'";
					if (printWasteRate)
						fileInfoString << Colors::Info << ", Waste rate: " << Colors::Arg << "'" << (100.0 - ((static_cast<double>(chars) / fileSize) * 100.0)) << "%'";
					fileInfoString << Colors::Info << ", In: " << Colors::Arg << filepath << "\n" << ResetColor;
					std::cout << fileInfoString.str();
				} else {
					std::ostringstream fileInfoString;
					fileInfoString << Colors::Info << "\rFiles: " << Colors::Arg << "'" << totalFiles << "'" << ResetColor;
					std::cout << fileInfoString.str();
				}
			}
			});

		std::filesystem::path startPath = scanner.getStartPath();
		if (startPath.empty())
			startPath = ".";
		startPath = std::filesystem::canonical(startPath);
		std::regex separatorReplace("\\\\+|\\/\\/+");
		std::string str = startPath.string();
		str = std::regex_replace(str, separatorReplace, "/");
		startPath = std::filesystem::path(str);
		std::ostringstream headerString;
		headerString << Colors::Info << "Scanning from: " << Colors::Arg << startPath << "\n";
		std::cout << headerString.str();
		std::error_code ec;
		if (scanner.matchFiles(&ec)) {
			std::ostringstream resultString;
			resultString << "\n";
			resultString << Colors::Info << "LOCScanner Result:\n" << ResetColor;
			resultString << Colors::Info << "\t" << Colors::Arg << "'" << filesInDirectories.size() << "'" << Colors::Info << " Directories\n" << ResetColor;
			resultString << Colors::Info << "\t" << Colors::Arg << "'" << totalFiles << "'" << Colors::Info << " Files\n" << ResetColor;
			if (printNumBytes)
				resultString << Colors::Info << "\t" << Colors::Arg << "'" << totalBytes << "'" << Colors::Info << " Bytes\n" << ResetColor;
			resultString << Colors::Info << "\t" << Colors::Arg << "'" << totalLOC << "'" << Colors::Info << " LOC\n" << ResetColor;
			if (printNumWords)
				resultString << Colors::Info << "\t" << Colors::Arg << "'" << totalWords << "'" << Colors::Info << " Words\n" << ResetColor;
			if (printNumChars)
				resultString << Colors::Info << "\t" << Colors::Arg << "'" << totalChars << "'" << Colors::Info << " Chars\n" << ResetColor;
			if (printWasteRate)
				resultString << Colors::Info << "\t" << Colors::Arg << "'" << (100.0 - ((static_cast<double>(totalChars) / totalBytes) * 100.0)) << "%'" << Colors::Info << " Waste rate\n" << ResetColor;
			std::cout << resultString.str();
		} else {
			std::cout << Colors::Error << "Error: Found no matching files!\n" << ResetColor;
			returnCode = EXIT_FAILURE;
		}

		if (ec) {
			std::cout << Colors::Error << "Error: std::error_code(" << ec << ") " << ec.message() << "\n" << ResetColor;
			returnCode = EXIT_FAILURE;
		}
#ifndef _DEBUG
	} catch (const std::exception& e) {
		std::cout << Colors::Error << "Error: std::exception " << e.what() << "\n" << ResetColor;
		returnCode = EXIT_FAILURE;
	}
#endif
	restoreAnsi();
	return returnCode;
}
