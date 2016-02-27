//  Created by emilk on 2012-11-03.
//  Copyright (c) 2015 Emil Ernerfeldt. All rights reserved.
//

#include "file_system.hpp"

#include <dirent.h>
#include <errno.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>

#define LOGURU_WITH_STREAMS 1 // For logurur::strprintf
#include <loguru.hpp>

namespace fs {

using namespace std;

const char* getOSErrorString()
{
	return strerror(errno);
}

void throw_exception(const string& msg)
{
	throw runtime_error(msg);
}

inline bool starts_with(const std::string& str, const std::string& start)
{
	return str.size() >= start.size() && str.substr(0, start.size()) == start;
}

// ------------------------------------------------

FILEWrapper::FILEWrapper(const string& path, const char* mode) : _fp(0)
{
	if (!try_open(path, mode)) {
		throw_exception(loguru::strprintf("Failed to open file '%s' with mode '%s': %s", path.c_str(), mode, getOSErrorString()));
	}
}

FILEWrapper::~FILEWrapper()
{
	close();
}

void FILEWrapper::close()
{
	if (_fp) {
		fclose(_fp);
		_fp = nullptr;
	}
}

bool FILEWrapper::try_open(const string& path, const char* mode)
{
	close();
	_fp = fopen(path.c_str(), mode);
	return !!_fp;
}

bool FILEWrapper::error() const { return ferror(_fp) != 0; }

bool FILEWrapper::end_of_file() const { return feof(_fp) != 0; }

void FILEWrapper::read_or_die(void* dest, size_t nbytes)
{
	if (fread(dest, 1, nbytes, _fp) != nbytes) {
		throw_exception("Failed to read " + std::to_string(nbytes) + " bytes: " + getOSErrorString());
	}
}

// Returns the number of read bytes
size_t FILEWrapper::try_read(void* dest, size_t nbytes)
{
	size_t n = fread(dest, 1, nbytes, _fp);
	//if (n < nbytes)
	//	throw_exception("Failed to read " + str(nbytes) + " bytes: " + getOSErrorString());
	return n;
}

void FILEWrapper::write(const void* src, size_t nbytes)
{
	if (fwrite(src, 1, nbytes, _fp) != nbytes) {
		throw_exception("Failed to write " + std::to_string(nbytes) + " bytes: " + getOSErrorString());
	}
}

void FILEWrapper::flush()
{
	if (fflush(_fp) != 0) {
		LOG_F(WARNING, "fflush failed: %s", getOSErrorString());
	}
}

// Origin = SEEK_SET, SEEK_CUR or SEEK_END
void FILEWrapper::seek(int offset, int origin)
{
	if (fseek(_fp, offset, origin) != 0) {
		throw_exception(loguru::strprintf("Failed to seek in FILE: %s", getOSErrorString()));
	}
}

long FILEWrapper::tell() const { return ftell(_fp); }

// Returns true on success
bool FILEWrapper::read_line(char* dest, int nbytes)
{
	return fgets(dest, nbytes, _fp) != nullptr;
}

FILE* FILEWrapper::handle() { return _fp; }

// ------------------------------------------------

bool file_exists(const string& path)
{
	FILEWrapper fp;
	return fp.try_open(path, "rb");
}

size_t file_size(const string& path)
{
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		LOG_F(WARNING, "Failed to stat file %s", path.c_str());
		return 0;
	}
	return (size_t)info.st_size;
}

time_t modified_time(const string& path)
{
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		LOG_F(WARNING, "Failed to stat file %s", path.c_str());
		return 0;
	}
	return info.st_mtime;
}

bool is_file(const string& path)
{
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		LOG_F(WARNING, "Failed to stat file %s", path.c_str());
		return false;
	}
	return S_ISREG(info.st_mode);
}

bool is_directory(const string& path)
{
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		LOG_F(WARNING, "Failed to stat file %s", path.c_str());
		return false;
	}
	return S_ISDIR(info.st_mode);
}

vector<uint8_t> read_binary_file(const string& path)
{
	FILEWrapper fp(path, "rb");

	size_t size = file_size(path);
	if (size <= 0) {
		// Fail?
		size_t chunk_size = 1024*128; // Size of first chunk
		size_t nRead = 0;

		vector<uint8_t> bytes;

		while (!fp.end_of_file()) {
			bytes.resize(nRead + chunk_size);
			size_t n = fp.try_read(&bytes[nRead], chunk_size);
			nRead += n;
			bytes.resize(nRead);
			chunk_size *= 2; // Read progressively larger chunks
		}

		bytes.shrink_to_fit();

		return bytes;
	} else {
		vector<uint8_t> bytes(size);
		size_t n = fp.try_read(bytes.data(), size);
		if (n != size) {
			LOG_F(ERROR, "read_binary_file failed");
		}

		return bytes;
	}
}

string read_text_file(const string& path)
{
#if 1
	const auto vec = read_binary_file(path);
	return string(begin(vec), end(vec));
#else
	ifstream file;
	file.open(path.c_str());

	string content = "";
	string line;

	if (!file.is_open()) {
		ABORT_F("Failed to open '%s'", path.c_str());
	}

	while (!file.eof()) {
		getline(file, line);
		content += line + "\n";
	}

	file.close();
	return content;
#endif
}

void write_binary_file(const string& path, const void* data, size_t nbytes)
{
	FILEWrapper file(path, "wb");
	file.write(data, nbytes);
}

void write_text_file(const string& path, const char* text)
{
	FILEWrapper file(path.c_str(), "wb");
	file.write(text, strlen(text));
}

vector<string> files_in_directory(const string& path)
{
	vector<string> names;
	auto directory = opendir(path.c_str());
	if (!directory) {
		LOG_F(ERROR, "FILEWrapper: Failed to open directory '%s'", path.c_str());
		return names;
	}
	while (auto dentry = readdir(directory)) {
		names.push_back(dentry->d_name);
	}
	closedir(directory);
	return names;
}

void print_tree(const string& path, const string& indent)
{
	LOG_F(INFO, "%s%s", indent.c_str(), path.c_str());

	auto directory = opendir(path.c_str());
	if (!directory) {
		return; // Not a directory
	}
	while (auto dentry = readdir(directory)) {
		if (dentry->d_name[0] == '.') { continue; }
		string child_path = string(path) + "/" + dentry->d_name;
		print_tree(child_path, "    ");
	}
	closedir(directory);
}

// ----------------------------------------------------------------------------

std::string file_ending(const std::string& path)
{
	auto pos = path.find_last_of('.');
	if (pos == std::string::npos || pos == 0) {
		return "";
	} else {
		return path.substr(pos+1);
	}
}

// Returns whatever comes before the last . "foo.bar.png" -> "foo.bar"
std::string without_ending(const std::string& path)
{
	auto pos = path.find_last_of('.');
	if (pos == std::string::npos || pos == 0) {
		return path;
	} else {
		return path.substr(0, pos);
	}
}

// strip_path("foo/bar/", "foo/bar/baz/mushroom") -> "baz/mushroom"
std::string strip_path(const std::string& dir_path, const std::string& file_path)
{
	if (starts_with(file_path, dir_path)) {
		return file_path.substr(dir_path.size());
	} else {
		return file_path;
	}
}

// foo/bar/baz -> foo/bar/
std::string file_dir(const std::string& path)
{
	auto pos = path.find_last_of('/');
	if (pos == std::string::npos) {
		return "";
	} else {
		return path.substr(0, pos+1);
	}
}

// foo/bar/baz.png -> baz.png
std::string file_name(const std::string& path)
{
	auto pos = path.find_last_of('/');
	if (pos == std::string::npos) {
		return path;
	} else {
		return path.substr(pos+1);
	}
}

const char* file_name(const char* path)
{
    const char* result = path;
    for (; *path; ++path) {
        if (*path == '/' || *path == '\\') {
            result = path + 1;
        }
    }
    return result;
}

} // namespace fs