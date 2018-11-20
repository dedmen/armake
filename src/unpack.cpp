/*
 * Copyright (C)  2016  Felix "KoffeinFlummi" Wiegand
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <filesystem>
//#include <unistd.h>

#include "args.h"
#include "filesystem.h"
#include "utils.h"
#include "unpack.h"
#include <numeric>


bool is_garbage(struct header *header) {
    int i;
    char c;

    if (header->packing_method != 0)
        return true;

    for (i = 0; i < strlen(header->name); i++) {
        c = header->name[i];
        if (c <= 31)
            return true;
        if (c == '"' ||
                c == '*' ||
                c == ':' ||
                c == '<' ||
                c == '>' ||
                c == '?' ||
                c == '/' ||
                c == '|')
            return true;
    }

    return false;
}

bool PboProperty::read(std::istream& in) {
    std::getline(in, key, '\0');
    if (key.empty()) return false; //We tried to read the end element of the property list
    std::getline(in, value, '\0');
    return true;
}

void PboProperty::write(std::ostream& out) const {
    out.write(key.c_str(), key.length() + 1);
    out.write(value.c_str(), value.length() + 1);
}

void PboEntry::read(std::istream& in) {
    struct {
        uint32_t method;
        uint32_t originalsize;
        uint32_t reserved;
        uint32_t timestamp;
        uint32_t datasize;
    } header {};

    std::getline(in, name, '\0');
    //#TODO read in one go
    in.read(reinterpret_cast<char*>(&header.method), 4);
    in.read(reinterpret_cast<char*>(&header.originalsize), 4);
    in.read(reinterpret_cast<char*>(&header.reserved), 4);
    in.read(reinterpret_cast<char*>(&header.timestamp), 4);
    in.read(reinterpret_cast<char*>(&header.datasize), 4);
    method = PboEntryPackingMethod::none;

    if (header.method == 'Encr') { //encrypted
        method = PboEntryPackingMethod::encrypted;
    }
    if (header.method == 'Cprs') { //compressed
        method = PboEntryPackingMethod::compressed;
    }
    if (header.method == 'Vers') { //Version
        method = PboEntryPackingMethod::version;
    }

    data_size = header.datasize;
    original_size = header.originalsize;

}

void PboEntry::write(std::ostream& out, bool noDate) const {
    struct {
        uint32_t method;
        uint32_t originalsize;
        uint32_t reserved;
        uint32_t timestamp;
        uint32_t datasize;
    } header {};

    switch (method) {
        case PboEntryPackingMethod::none: header.method = 0; break;
        case PboEntryPackingMethod::version: header.method = 'Vers'; break;
        case PboEntryPackingMethod::compressed: header.method = 'Cprs'; break;
        case PboEntryPackingMethod::encrypted: header.method = 'Encr'; break;
    }
    header.originalsize = original_size;
    header.reserved = '3ECA'; //#TODO remove dis?
    //time is unused by Arma. We could write more misc stuff into there for a total of 8 bytes
    header.timestamp = 0;// noDate ? 0 : std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    header.datasize = data_size;

    out.write(name.c_str(), name.length()+1);
    out.write(reinterpret_cast<char*>(&header.method), 4);
    out.write(reinterpret_cast<char*>(&header.originalsize), 4);
    out.write(reinterpret_cast<char*>(&header.reserved), 4);
    out.write(reinterpret_cast<char*>(&header.timestamp), 4);
    out.write(reinterpret_cast<char*>(&header.datasize), 4);
}

int PboEntryBuffer::underflow() {
    if (gptr() < egptr()) // buffer not exhausted
        return traits_type::to_int_type(*gptr());

    //gptr Pointer to current position of input sequence
    //egptr End of the buffered part of the input sequence
    //Beginning of the buffered part of the input sequence

    reader.input.seekg(file.startOffset + bufferEndFilePos);
    size_t sizeLeft = file.data_size - bufferEndFilePos;
    if (sizeLeft == 0) return std::char_traits<char>::eof();

    auto sizeToRead = std::min(sizeLeft, buffer.size());
    reader.input.read(buffer.data(), sizeToRead);
    bufferEndFilePos += sizeToRead;

    setg(&buffer.front(), &buffer.front(), &buffer.front() + sizeToRead);

    return std::char_traits<char>::to_int_type(*this->gptr());
}

int64_t PboEntryBuffer::xsgetn(char* _Ptr, int64_t _Count) {
    // get _Count characters from stream
    const int64_t _Start_count = _Count;

    while (_Count) {
        size_t dataLeft = egptr() - gptr();
        if (dataLeft == 0) {
            reader.input.seekg(file.startOffset + bufferEndFilePos);
            size_t sizeLeft = file.data_size - bufferEndFilePos;
            if (sizeLeft == 0) break; //EOF
            auto sizeToRead = std::min(sizeLeft, buffer.size());
            reader.input.read(buffer.data(), sizeToRead);
            bufferEndFilePos += sizeToRead;
            dataLeft = sizeToRead;
        }

        std::copy(gptr(), gptr()+dataLeft, _Ptr);
        _Ptr += dataLeft;
        gbump(dataLeft);
    }

    return (_Start_count - _Count);
}

void PboReader::readHeaders() {
    PboEntry intro;
    intro.read(input);

    //#TODO check stuff and throw if error
    //filename is empty
    //packing method is vers
    //time is 0
    //datasize is 0

    //header ignores startoffset and uncompressed size

    PboProperty prop;
    while (prop.read(input)) {
        properties.emplace_back(std::move(prop));
    }
    //When prop's last read "failed" we just finished reading the terminator of the properties
    propertiesEnd = input.tellg();

    PboEntry entry;

    while (entry.read(input), !entry.name.empty()) {
        files.emplace_back(std::move(entry));
    }
    //We just read the last terminating entry header too.
    headerEnd = input.tellg();

    size_t curPos = headerEnd;
    for (auto& it : files) {
        it.startOffset = curPos;
        curPos += it.data_size;
    }
    auto fileEnd = curPos;
    //After end there is checksum 20 bytes. Grab that too. cmd_inspect might want to display that or check it

}


extern "C" {
#include "sha1.h"
}
#include <array>

struct hashing_ostreambuf : public std::streambuf
{
    hashing_ostreambuf(std::ostream &finalOut) : finalOut(finalOut) {
        SHA1Reset(&context);
    }

protected:
    std::streamsize xsputn(const char_type* s, std::streamsize n) override {
        SHA1Input(&context, reinterpret_cast<const unsigned char*>(s), n);
        finalOut.write(s, n);

        //Yes return result is fake, but meh.
        return n; // returns the number of characters successfully written.
    }

    int_type overflow(int_type ch) override {
        auto outc = static_cast<char>(ch);
        SHA1Input(&context, reinterpret_cast<const unsigned char*>(&outc), 1);
        finalOut.put(outc);

        return 1;
    }

public:

    std::array<char, 20> getResult() {

        if (!SHA1Result(&context))
            throw std::logic_error("SHA1 hash corrupted");

        for (int i = 0; i < 5; i++) {
            unsigned temp = context.Message_Digest[i];
            context.Message_Digest[i] = ((temp >> 24) & 0xff) |
                ((temp << 8) & 0xff0000) | ((temp >> 8) & 0xff00) | ((temp << 24) & 0xff000000);
        }
        std::array<char, 20> res;

        memcpy(res.data(), context.Message_Digest, 20);

        return res;
    }


private:
    std::ostream &finalOut;
    SHA1Context context;
};

void PboFTW_CopyFromFile::writeDataTo(std::ostream& output) {
    std::ifstream inp(file, std::ifstream::binary);
    std::array<char, 4096> buf;
    do {
        inp.read(buf.data(), buf.size());
        output.write(buf.data(), inp.gcount());
    } while (inp.gcount() > 0);
}

void PboWriter::writePbo(std::ostream& output) {

    hashing_ostreambuf hashOutbuf(output);

    std::ostream out(&hashOutbuf);


    struct pboEntryHeader {
        uint32_t method;
        uint32_t originalsize;
        uint32_t reserved;
        uint32_t timestamp;
        uint32_t datasize;
        void write(std::ostream& output) const {
            output.write(reinterpret_cast<const char*>(this), sizeof(pboEntryHeader));
        }
    };

    pboEntryHeader versHeader {
        'Vers',
        'amra','!!ek', 0, 0
        //'UHTC',' UHL', 0, 0
        //0,0,0,0
    };

    //Write a "dummy" Entry which is used as header.
    //#TODO this should use pboEntry class to write. But I want a special "reserved" value here
    out.put(0); //name
    versHeader.write(out);

    for (auto& it : properties)
        it.write(out);
    out.put(0); //properties endmarker

    //const size_t curOffs = out.tellp();
    //
    //const size_t headerStringsSize = std::transform_reduce(filesToWrite.begin(),filesToWrite.end(), 0u, std::plus<>(), [](auto& it) {
    //    return it->getEntryInformation().name.length() + 1;
    //});
    //
    //const size_t headerSize = (filesToWrite.size() + 1) * sizeof(pboEntryHeader) + headerStringsSize;
    //
    //auto curFileStartOffset = curOffs + headerSize;
    for (auto& it : filesToWrite) {
        auto& prop = it->getEntryInformation();
        //prop.startOffset = curFileStartOffset;
        //
        //curFileStartOffset += prop.name.length() + 1 + sizeof(pboEntryHeader);
        prop.write(out);
    }

    //End is indicated by empty name
    out.put(0);
    //Rest of the header after that is ignored, so why not have fun?
    pboEntryHeader endHeader {
        'sihT','t si','b eh',' tse','gnat'
        //0,0,0,0,0
    };
    endHeader.write(out);


    for (auto& it : filesToWrite) {
        it->writeDataTo(out);
    }

    
    auto hash = hashOutbuf.getResult();

    //no need to write to hashing buf anymore
    output.put(0); //file hash requires leading zero which doesn't contribute to hash data
    output.write(hash.data(), hash.size());

}

int cmd_inspect() {
    extern struct arguments args;
    extern const char *current_target;
    FILE *f_target;
    int num_files;
    long i;
    long fp_tmp;
    char buffer[2048];
    struct header *headers;

    if (args.num_positionals != 2)
        return 128;

    headers = (struct header *)safe_malloc(sizeof(struct header) * MAXFILES);

    current_target = args.positionals[1];

    // remove trailing slash in target
    if (args.positionals[1][strlen(args.positionals[1]) - 1] == PATHSEP)
        args.positionals[1][strlen(args.positionals[1]) - 1] = 0;

    // open file
    f_target = fopen(args.positionals[1], "rb");
    if (!f_target) {
        errorf("Failed to open %s.\n", args.positionals[1]);
        free(headers);
        return 1;
    }

    // read header extensions
    fseek(f_target, 1, SEEK_SET);
    fgets(buffer, 5, f_target);
    if (strncmp(buffer, "sreV", 4) == 0) {
        fseek(f_target, 21, SEEK_SET);
        printf("Header extensions:\n");
        while (true) {
            fp_tmp = ftell(f_target);
            fgets(buffer, 2048, f_target);
            if (strnlen(buffer, 2048) == 2048) {
                errorf("Header extension exceeds maximum size.");
                return 4;
            }
            fseek(f_target, fp_tmp + strlen(buffer) + 1, SEEK_SET);

            if (strlen(buffer) == 0)
                break;

            printf("- %s=", buffer);

            fp_tmp = ftell(f_target);
            fgets(buffer, 2048, f_target);
            if (strnlen(buffer, 2048) == 2048) {
                errorf("Header extension exceeds maximum size.");
                return 4;
            }
            fseek(f_target, fp_tmp + strlen(buffer) + 1, SEEK_SET);

            printf("%s\n", buffer);
        }
        printf("\n");
    } else {
        fseek(f_target, 0, SEEK_SET);
    }

    // read headers
    for (num_files = 0; num_files <= MAXFILES; num_files++) {
        fp_tmp = ftell(f_target);
        fgets(buffer, sizeof(buffer), f_target);
        fseek(f_target, fp_tmp + strlen(buffer) + 1, SEEK_SET);
        if (strlen(buffer) == 0) {
            fseek(f_target, sizeof(uint32_t) * 5, SEEK_CUR);
            break;
        }

        strcpy(headers[num_files].name, buffer);
        fread(&headers[num_files].packing_method, sizeof(uint32_t), 1, f_target);
        fread(&headers[num_files].original_size, sizeof(uint32_t), 1, f_target);
        fseek(f_target, sizeof(uint32_t) * 2, SEEK_CUR);
        fread(&headers[num_files].data_size, sizeof(uint32_t), 1, f_target);
    }
    if (num_files > MAXFILES) {
        errorf("Maximum number of files (%i) exceeded.\n", MAXFILES);
        fclose(f_target);
        free(headers);
        return 4;
    }

    printf("# Files: %i\n\n", num_files);

    printf("Path                                                  Method  Original    Packed\n");
    printf("                                                                  Size      Size\n");
    printf("================================================================================\n");
    for (i = 0; i < num_files; i++) {
        if (headers[i].original_size == 0)
            headers[i].original_size = headers[i].data_size;
        printf("%-50s %9u %9u %9u\n", headers[i].name, headers[i].packing_method, headers[i].original_size, headers[i].data_size);
    }

    // clean up
    fclose(f_target);
    free(headers);

    return 0;
}


int cmd_unpack() {
    extern struct arguments args;
    extern const char *current_target;
    FILE *f_source;
    FILE *f_target;
    int num_files;
    long i;
    long j;
    long fp_tmp;
    char full_path[2048];
    char buffer[2048];
    struct header *headers;

    if (args.num_positionals < 3)
        return 128;

    headers = (struct header *)safe_malloc(sizeof(struct header) * MAXFILES);

    current_target = args.positionals[1];

    // open file
    f_source = fopen(args.positionals[1], "rb");
    if (!f_source) {
        errorf("Failed to open %s.\n", args.positionals[1]);
        free(headers);
        return 1;
    }

    // create folder
    if (!create_folder(args.positionals[2])) {
        errorf("Failed to create output folder %s.\n", args.positionals[2]);
        fclose(f_source);
        free(headers);
        return 2;
    }

    // create header extensions file
    strcpy(full_path, args.positionals[2]);
    strcat(full_path, PATHSEP_STR);
    strcat(full_path, "$PBOPREFIX$");
    if (std::filesystem::exists(full_path) && !args.force) {
        errorf("File %s already exists and --force was not set.\n", full_path);
        fclose(f_source);
        free(headers);
        return 3;
    }

    // read header extensions
    fseek(f_source, 1, SEEK_SET);
    fgets(buffer, 5, f_source);
    if (strncmp(buffer, "sreV", 4) == 0) {
        fseek(f_source, 21, SEEK_SET);

        // open header extensions file
        f_target = fopen(full_path, "wb");
        if (!f_target) {
            errorf("Failed to open file %s.\n", full_path);
            fclose(f_source);
            free(headers);
            return 4;
        }

        // read all header extensions
        while (true) {
            fp_tmp = ftell(f_source);
            fgets(buffer, 2048, f_source);
            if (strnlen(buffer, 2048) == 2048) {
                errorf("Header extension exceeds maximum size.");
                return 4;
            }
            fseek(f_source, fp_tmp + strlen(buffer) + 1, SEEK_SET);

            if (strlen(buffer) == 0)
                break;

            fprintf(f_target, "%s=", buffer);

            fp_tmp = ftell(f_source);
            fgets(buffer, 2048, f_source);
            if (strnlen(buffer, 2048) == 2048) {
                errorf("Header extension exceeds maximum size.");
                return 4;
            }
            fseek(f_source, fp_tmp + strlen(buffer) + 1, SEEK_SET);

            fprintf(f_target, "%s\n", buffer);
        }
    } else {
        fseek(f_source, 0, SEEK_SET);
    }

    // read headers
    for (num_files = 0; num_files <= MAXFILES; num_files++) {
        fp_tmp = ftell(f_source);
        fgets(buffer, sizeof(buffer), f_source);
        fseek(f_source, fp_tmp + strlen(buffer) + 1, SEEK_SET);
        if (strlen(buffer) == 0) {
            fseek(f_source, sizeof(uint32_t) * 5, SEEK_CUR);
            break;
        }

        strcpy(headers[num_files].name, buffer);
        fread(&headers[num_files].packing_method, sizeof(uint32_t), 1, f_source);
        fread(&headers[num_files].original_size, sizeof(uint32_t), 1, f_source);
        fseek(f_source, sizeof(uint32_t) * 2, SEEK_CUR);
        fread(&headers[num_files].data_size, sizeof(uint32_t), 1, f_source);
    }
    if (num_files > MAXFILES) {
        errorf("Maximum number of files (%i) exceeded.\n", MAXFILES);
        fclose(f_source);
        free(headers);
        return 5;
    }

    // read files
    for (i = 0; i < num_files; i++) {
        // check for garbage
        if (is_garbage(&headers[i])) {
            fseek(f_source, headers[i].data_size, SEEK_CUR);
            continue;
        }

        // check if file is excluded
        for (j = 0; j < args.num_excludefiles; j++) {
            if (matches_glob(headers[i].name, args.excludefiles[j]))
                break;
        }
        if (j < args.num_excludefiles) {
            fseek(f_source, headers[i].data_size, SEEK_CUR);
            continue;
        }

        // check if file is included
        for (j = 1; j < args.num_includefolders; j++) {
            if (matches_glob(headers[i].name, args.includefolders[j]))
                break;
        }
        if (args.num_includefolders > 1 && j == args.num_includefolders) {
            fseek(f_source, headers[i].data_size, SEEK_CUR);
            continue;
        }

        // replace pathseps on linux
#ifndef _WIN32
        for (j = 0; j < strlen(headers[i].name); j++) {
            if (headers[i].name[j] == '\\')
                headers[i].name[j] = PATHSEP;
        }
#endif

        // get full path
        strcpy(full_path, args.positionals[2]);
        strcat(full_path, PATHSEP_STR);
        strcat(full_path, headers[i].name);

        // create containing folder
        strcpy(buffer, full_path);
        if (strrchr(buffer, PATHSEP) != NULL) {
            *strrchr(buffer, PATHSEP) = 0;
            if (!create_folder(buffer)) {
                errorf("Failed to create folder %s.\n", buffer);
                fclose(f_source);
                return 6;
            }
        }

        // open target file
        if (std::filesystem::exists(full_path) && !args.force) {
            errorf("File %s already exists and --force was not set.\n", full_path);
            fclose(f_source);
            return 7;
        }
        f_target = fopen(full_path, "wb");
        if (!f_target) {
            errorf("Failed to open file %s.\n", full_path);
            fclose(f_source);
            return 8;
        }

        // write to file
        for (j = 0; headers[i].data_size - j >= sizeof(buffer); j += sizeof(buffer)) {
            fread(buffer, sizeof(buffer), 1, f_source);
            fwrite(buffer, sizeof(buffer), 1, f_target);
        }
        fread(buffer, headers[i].data_size - j, 1, f_source);
        fwrite(buffer, headers[i].data_size - j, 1, f_target);

        // clean up
        fclose(f_target);
    }

    // clean up
    fclose(f_source);
    free(headers);

    return 0;
}


int cmd_cat() {
    extern struct arguments args;
    extern const char *current_target;
    FILE *f_source;
    int num_files;
    int file_index;
    long i;
    long j;
    long fp_tmp;
    char buffer[2048];
    struct header *headers;

    if (args.num_positionals < 3)
        return 128;

    headers = (struct header *)safe_malloc(sizeof(struct header) * MAXFILES);

    current_target = args.positionals[1];

    // open file
    f_source = fopen(args.positionals[1], "rb");
    if (!f_source) {
        errorf("Failed to open %s.\n", args.positionals[1]);
        free(headers);
        return 1;
    }

    // read header extensions
    fseek(f_source, 1, SEEK_SET);
    fgets(buffer, 5, f_source);
    if (strncmp(buffer, "sreV", 4) == 0) {
        fseek(f_source, 21, SEEK_SET);
        i = 0;
        while (true) {
            fp_tmp = ftell(f_source);
            buffer[i++] = fgetc(f_source);
            buffer[i] = '\0';
            if (buffer[i - 1] == '\0' && buffer[i - 2] == '\0')
                break;
        }
    } else {
        fseek(f_source, 0, SEEK_SET);
    }

    // read headers
    file_index = -1;
    for (num_files = 0; num_files <= MAXFILES; num_files++) {
        fp_tmp = ftell(f_source);
        fgets(buffer, sizeof(buffer), f_source);
        fseek(f_source, fp_tmp + strlen(buffer) + 1, SEEK_SET);
        if (strlen(buffer) == 0) {
            fseek(f_source, sizeof(uint32_t) * 5, SEEK_CUR);
            break;
        }

        if (stricmp(args.positionals[2], buffer) == 0)
            file_index = num_files;

        strcpy(headers[num_files].name, buffer);
        fread(&headers[num_files].packing_method, sizeof(uint32_t), 1, f_source);
        fread(&headers[num_files].original_size, sizeof(uint32_t), 1, f_source);
        fseek(f_source, sizeof(uint32_t) * 2, SEEK_CUR);
        fread(&headers[num_files].data_size, sizeof(uint32_t), 1, f_source);
    }

    if (num_files > MAXFILES) {
        errorf("Maximum number of files (%i) exceeded.\n", MAXFILES);
        fclose(f_source);
        free(headers);
        return 4;
    }

    if (file_index == -1) {
        errorf("PBO does not contain the file %s.\n", args.positionals[2]);
        fclose(f_source);
        free(headers);
        return 5;
    }

    // read files
    for (i = 0; i < num_files; i++) {
        if (i != file_index) {
            fseek(f_source, headers[i].data_size, SEEK_CUR);
            continue;
        }

        // write to file
        for (j = 0; headers[i].data_size - j >= sizeof(buffer); j += sizeof(buffer)) {
            fread(buffer, sizeof(buffer), 1, f_source);
            fwrite(buffer, sizeof(buffer), 1, stdout);
        }
        fread(buffer, headers[i].data_size - j, 1, f_source);
        fwrite(buffer, headers[i].data_size - j, 1, stdout);
    }

    // clean up
    fclose(f_source);
    free(headers);

    return 0;
}
