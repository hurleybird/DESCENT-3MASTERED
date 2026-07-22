/*
Copyright (c) 2019 SaladBadger

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

//hogdir: takes a directory from the command line and packages it into a hog
//file for usage in <s>ICDP</s><s>Neptune</s>Piccu Engine. 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <wchar.h>
#include <vector>
#include <stdexcept>
#include <algorithm>

constexpr int FILENAME_LEN = 36;
constexpr int DIRENTRY_LEN = FILENAME_LEN + 12;
const char* sig = "HOG2";

FILE* hogfile;

struct fileinfo_data
{
	std::filesystem::path fullpath;
	char name[FILENAME_LEN];
	uint32_t size;
};

void write_uint32_t(FILE* fp, uint32_t value)
{
	uint8_t buffer[4];
	buffer[0] = value & 255;
	buffer[1] = (value >> 8) & 255;
	buffer[2] = (value >> 16) & 255;
	buffer[3] = (value >> 24) & 255;

	fwrite(buffer, 1, 4, fp);
}

uint32_t read_uint32_t(FILE* fp)
{
	uint8_t buffer[4];
	if (fread(buffer, 1, sizeof(buffer), fp) != sizeof(buffer))
		throw std::runtime_error("Unexpected end of HOG file");
	return uint32_t(buffer[0]) | (uint32_t(buffer[1]) << 8) |
		(uint32_t(buffer[2]) << 16) | (uint32_t(buffer[3]) << 24);
}

void extract_hog(const char* archive, const char* output_directory)
{
	FILE* input = fopen(archive, "rb");
	if (!input)
		throw std::runtime_error("Cannot open input HOG file");
	try
	{
		char signature[4];
		if (fread(signature, 1, sizeof(signature), input) != sizeof(signature) ||
			memcmp(signature, sig, sizeof(signature)) != 0)
			throw std::runtime_error("Input is not a HOG2 archive");
		const uint32_t count = read_uint32_t(input);
		const uint32_t data_offset = read_uint32_t(input);
		if (count > 100000 || data_offset < 68 + count * DIRENTRY_LEN)
			throw std::runtime_error("Invalid HOG2 directory");
		if (fseek(input, 56, SEEK_CUR) != 0)
			throw std::runtime_error("Invalid HOG2 header");

		struct archive_entry { std::string name; uint32_t size; };
		std::vector<archive_entry> entries;
		entries.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			char name[FILENAME_LEN + 1] = {};
			if (fread(name, 1, FILENAME_LEN, input) != FILENAME_LEN)
				throw std::runtime_error("Truncated HOG2 directory");
			read_uint32_t(input);
			const uint32_t size = read_uint32_t(input);
			read_uint32_t(input);
			name[FILENAME_LEN] = 0;
			if (!name[0] || strchr(name, '/') || strchr(name, '\\') || strstr(name, ".."))
				throw std::runtime_error("Unsafe filename in HOG2 directory");
			entries.push_back({name, size});
		}
		if (fseek(input, static_cast<long>(data_offset), SEEK_SET) != 0)
			throw std::runtime_error("Invalid HOG2 data offset");

		const std::filesystem::path destination(output_directory);
		std::filesystem::create_directories(destination);
		std::vector<uint8_t> buffer(1024 * 1024);
		for (const auto& entry : entries)
		{
			const auto output_path = destination / entry.name;
			FILE* output = fopen(output_path.u8string().c_str(), "wb");
			if (!output)
				throw std::runtime_error("Cannot create extracted file");
			uint32_t remaining = entry.size;
			while (remaining)
			{
				const size_t chunk = (std::min)(static_cast<size_t>(remaining), buffer.size());
				if (fread(buffer.data(), 1, chunk, input) != chunk ||
					fwrite(buffer.data(), 1, chunk, output) != chunk)
				{
					fclose(output);
					throw std::runtime_error("Failed while extracting HOG2 data");
				}
				remaining -= static_cast<uint32_t>(chunk);
			}
			fclose(output);
		}
		fclose(input);
	}
	catch (...)
	{
		fclose(input);
		throw;
	}
}

void generate_header(std::vector<fileinfo_data>& files)
{
	fwrite(sig, 1, strlen(sig), hogfile);
	write_uint32_t(hogfile, files.size());
	write_uint32_t(hogfile, files.size() * DIRENTRY_LEN + 68);

	uint8_t padding[56];
	memset(padding, 0xFF, sizeof(padding));
	fwrite(padding, 1, sizeof(padding), hogfile);

	for (fileinfo_data& file : files)
	{
		uint32_t placeholder = 0;
		fwrite(file.name, 1, FILENAME_LEN, hogfile);
		write_uint32_t(hogfile, placeholder); //flags
		write_uint32_t(hogfile, file.size); //file size
		write_uint32_t(hogfile, placeholder); //modified time but the build system don't care
	}

	//wait why not just write all the files now
	for (fileinfo_data& file : files)
	{
		std::string fullpath = file.fullpath.u8string();
		FILE* fp = fopen(fullpath.c_str(), "rb");
		if (!fp)
		{
			char errorstr[256];
			snprintf(errorstr, sizeof(errorstr), "generate_header: Cannot open file %s!", fullpath.c_str());
			throw std::runtime_error(errorstr);
		}
			
		uint8_t* buffer = (uint8_t*)malloc(file.size);
		if (!buffer)
		{
			char errorstr[256];
			snprintf(errorstr, sizeof(errorstr), "generate_header: Error allocating buffer for file %s!", fullpath.c_str());
			throw std::runtime_error(errorstr);
		}

		fread(buffer, 1, file.size, fp);
		fclose(fp);

		fwrite(buffer, 1, file.size, hogfile);

		free(buffer);
	}
}

void add_dir(const char* directory)
{
	if (strlen(directory) == 0)
	{
		fprintf(stderr, "Zero-length search string\n");
		return;
	}

	std::filesystem::path path(directory);
	std::filesystem::directory_iterator it = std::filesystem::directory_iterator(path);

	std::vector<fileinfo_data> pathlist;

	for (std::filesystem::directory_entry const& entry : it)
	{
		if (entry.is_regular_file())
		{
			//Eventually, Piccu should support UTF-8 proper. Eventually.
			std::string filename = entry.path().filename().u8string();

			if (filename.size() >= FILENAME_LEN)
				continue;

			uintmax_t filesize = entry.file_size();
			if (filesize > UINT32_MAX) //why not
				continue;

			fileinfo_data file = {};
			file.fullpath = entry.path();
			file.size = (uint32_t)filesize;
			strncpy(file.name, filename.c_str(), FILENAME_LEN);

			pathlist.push_back(file);
		}
	}

	//Generate the header
	generate_header(pathlist);
}

int main(int argc, char** argv)
{
	if (argc == 4 && strcmp(argv[1], "--extract") == 0)
	{
		try
		{
			extract_hog(argv[2], argv[3]);
			return 0;
		}
		catch (const std::runtime_error& err)
		{
			fprintf(stderr, "Error extracting %s:\n%s\n", argv[2], err.what());
			return 1;
		}
	}
	if (argc < 3)
	{
		printf("usage: hogdir [output file] [source dir]\n"
			"       hogdir --extract [input file] [output dir]\n");
		return 0;
	}

	hogfile = fopen(argv[1], "wb");
	if (!hogfile)
	{
		fprintf(stderr, "Failed to open output file %s.\n", argv[1]);
		return 1;
	}

	try
	{
		add_dir(argv[2]);
	}
	catch (const std::runtime_error& err)
	{
		fprintf(stderr, "Error creating hogfile %s:\n%s\n", argv[1], err.what());
	}

	fclose(hogfile);

	return 0;
}

