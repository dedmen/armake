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
#include "logger.h"
#include <iostream>
#include "rapify.h"


bool is_garbage(PboEntry entry) {
    int i;
    char c;

    if (entry.method != PboEntryPackingMethod::none)
        return true;

    bool garbageName = std::any_of(entry.name.begin(), entry.name.end(), [](char c) {
        return 
            c <= ' ' ||
            c == '"' ||
            c == '*' ||
            c == ':' ||
            c == '<' ||
            c == '>' ||
            c == '?' ||
            c == '/' ||
            c == '|';
    });

    return garbageName;
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
    in.read(reinterpret_cast<char*>(&header), sizeof(header));

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
    header.reserved = 0;// '3ECA'; //#TODO remove dis?
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

void PboEntryBuffer::setBufferSize(size_t newSize) {
    size_t dataLeft = egptr() - gptr();
    auto bufferOffset = gptr() - &buffer.front(); //Where we currently are inside the buffer
    auto bufferStartInFile = bufferEndFilePos - (dataLeft + bufferOffset); //at which offset in the PboEntry file our buffer starts
    bufferEndFilePos = bufferStartInFile; //Back to start.
    setg(&buffer.front(), &buffer.front(), &buffer.front()); //no data available

    buffer.clear(); //So we don't copy on realloc.
    buffer.resize(newSize);
}

int PboEntryBuffer::underflow() {
    if (gptr() < egptr()) // buffer not exhausted
        return traits_type::to_int_type(*gptr());

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

            setg(&buffer.front(), &buffer.front(), &buffer.front() + sizeToRead);

            dataLeft = std::min(sizeToRead, (size_t)_Count);
        } else
            dataLeft = std::min(dataLeft, (size_t)_Count);

        std::copy(gptr(), gptr() + dataLeft, _Ptr);
        _Ptr += dataLeft;
        _Count -= dataLeft;
        gbump(dataLeft);
    }

    return (_Start_count - _Count);
}

std::basic_streambuf<char>::pos_type PboEntryBuffer::seekoff(off_type offs, std::ios_base::seekdir dir, std::ios_base::openmode mode) {
    auto test = egptr();
    auto test2 = gptr();


    switch (dir) {
        case std::ios_base::beg: {
            //#TODO negative offs is error


            size_t dataLeft = egptr() - gptr();
            auto bufferOffset = gptr() - &buffer.front(); //Where we currently are inside the buffer
            auto bufferStartInFile = bufferEndFilePos - (dataLeft + bufferOffset); //at which offset in the PboEntry file our buffer starts



            //offset is still inside buffer
            if (bufferStartInFile <= offs && bufferEndFilePos > offs) { 
                auto curFilePos = (bufferEndFilePos - dataLeft);

                int64_t offsetToCurPos = offs - static_cast<int64_t>(curFilePos);
                gbump(offsetToCurPos); //Jump inside buffer till we find offs
                return offs;
            }

            //We are outside of buffer. Just reset and exit
            bufferEndFilePos = offs;
            setg(&buffer.front(), &buffer.front(), &buffer.front()); //no data available
            return bufferEndFilePos;

        }

        break;
        case std::ios_base::cur: {
                size_t dataLeft = egptr() - gptr();
                auto curFilePos = (bufferEndFilePos - dataLeft);

                if (offs == 0) return curFilePos;

                if (dataLeft == 0) {
                    bufferEndFilePos += offs;
                    return bufferEndFilePos;
                }


                if (offs > 0 && dataLeft > offs) { // offset is still inside buffer
                    gbump(offs);
                    return curFilePos + offs;
                }
                if (offs > 0) { //offset is outside of buffer
                    bufferEndFilePos = curFilePos + offs;
                    setg(&buffer.front(), &buffer.front(), &buffer.front()); //no data available
                    return bufferEndFilePos;
                }

                if (offs < 0) {

                    auto bufferOffset = gptr() - &buffer.front(); //Where we currently are inside the buffer
                    if (bufferOffset >= -offs) {//offset is still in buffer
                        gbump(offs);
                        return bufferOffset + offs;
                    }

                    bufferEndFilePos = curFilePos + offs;
                    setg(&buffer.front(), &buffer.front(), &buffer.front()); //no data available
                    return bufferEndFilePos;
                }
            }
        break;
        case std::ios_base::end:
            //#TODO positive offs is error
            bufferEndFilePos = file.data_size + offs;
            setg(&buffer.front(), &buffer.front(), &buffer.front()); //no data available
            return bufferEndFilePos;
        break;
    }
    return -1; //#TODO this is error
}

std::basic_streambuf<char>::pos_type PboEntryBuffer::seekpos(pos_type offs, std::ios_base::openmode mode) {
    return seekoff(offs, std::ios_base::beg, mode);
}

std::streamsize PboEntryBuffer::showmanyc() {
    //How many characters are left
    size_t dataLeft = egptr() - gptr();
    
    return (file.data_size - bufferEndFilePos) + dataLeft;
}


__itt_domain* unpackDomain = __itt_domain_create("armake.unpack");
__itt_string_handle* handle_readHeaders = __itt_string_handle_create("PboReader::readHeaders");

void PboReader::readHeaders() {
    __itt_task_begin(unpackDomain, __itt_null, __itt_null, handle_readHeaders);
    PboEntry intro;
    intro.read(input);

    //#TODO check stuff and throw if error
    //filename is empty
    //packing method is vers
    //time is 0
    //datasize is 0

    //header ignores startoffset and uncompressed size

    if (intro.method == PboEntryPackingMethod::none) {//Broken 3den exported pbo
        input.seekg(0, std::istream::beg); //Seek back to start
        badHeader = true;
    } else {   
        PboProperty prop;
        while (prop.read(input)) {
            properties.emplace_back(std::move(prop));
        }
        //When prop's last read "failed" we just finished reading the terminator of the properties
        propertiesEnd = input.tellg();
    }


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
    __itt_task_end(unpackDomain);
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
        //'amra','!!ek', 0, 0
        //'UHTC',' UHL', 0, 0
        0,0,0,0
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
        //'sihT','t si','b eh',' tse','gnat'
        0,0,0,0,0
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

int cmd_inspect(Logger& logger) {
    extern struct arguments args;
    struct header *headers;

    if (args.num_positionals != 2)
        return 128;

    // remove trailing slash in target
    if (args.positionals[1][strlen(args.positionals[1]) - 1] == PATHSEP)
        args.positionals[1][strlen(args.positionals[1]) - 1] = 0;

    std::ifstream input(args.positionals[1], std::ifstream::binary);
    if (!input.is_open()) {
        return 1;
    }

    PboReader reader(input);

    reader.readHeaders();
    if (reader.isBadHeader()) {
        printf("This PBO has no Header! 3DEN usually exports these pbos\n");
    }

    input.close(); //Don't need it anymore
    printf("Header extensions:\n");

    for (auto& prop : reader.getProperties())
        printf("- %s=%s\n", prop.key.c_str(), prop.value.c_str());
    printf("\n");

    auto& files = reader.getFiles();

    printf("# Files: %llu\n\n", files.size());

    printf("Path                                                Method   Original    Packed\n");
    printf("                                                                 Size      Size\n");
    printf("===============================================================================\n");
    for (auto& it : files) {
        std::string_view packMeth = "u";
        switch (it.method) {
            case PboEntryPackingMethod::none: packMeth = "n";  break;
            case PboEntryPackingMethod::version: packMeth = "v"; break;
            case PboEntryPackingMethod::compressed: packMeth = "c"; break;
            case PboEntryPackingMethod::encrypted: packMeth = "e"; break;
        }

        printf("%-50s %7s %9u %9u\n", it.name.c_str(), packMeth.data(), it.original_size, it.data_size);
    }
    return 0;
}


__itt_string_handle* handle_cmd_unpack = __itt_string_handle_create("cmd_unpack");
__itt_string_handle* handle_cmd_unpackF = __itt_string_handle_create("cmd_unpack_F");
int cmd_unpack(Logger& logger) {
    extern struct arguments args;
   
    if (args.num_positionals < 3)
        return 128;

    ScopeGuard ittS([]() {
        __itt_task_end(unpackDomain);
    });
    __itt_task_begin(unpackDomain, __itt_null, __itt_null, handle_cmd_unpack);




    // remove trailing slash in target
    if (args.positionals[1][strlen(args.positionals[1]) - 1] == PATHSEP)
        args.positionals[1][strlen(args.positionals[1]) - 1] = 0;

    std::ifstream input(args.positionals[1], std::ifstream::binary);
    if (!input.is_open()) {
        logger.error("Failed to open %s.\n", args.positionals[1]);
        return 1;
    }

    std::filesystem::path outputFolder(args.positionals[2]);

    PboReader reader(input);
    reader.readHeaders();
    if (reader.isBadHeader()) {
        printf("This PBO has no Header! 3DEN usually exports these pbos\n");
    }

    // create folder
    if (!std::filesystem::exists(outputFolder) && !create_folder(outputFolder)) {
        logger.error("Failed to create output folder %s.\n", args.positionals[2]);
        return 2;
    }

    if (!reader.getProperties().empty()) {
        // create header extensions file
        if (std::filesystem::exists(outputFolder / "$PBOPREFIX$") && !args.force) {
            logger.error("File %s already exists and --force was not set.\n", (outputFolder / "$PBOPREFIX$").string().c_str());
            return 3;
        }

        std::ofstream pboprefix(outputFolder / "$PBOPREFIX$", std::ofstream::binary);
        for (auto& it : reader.getProperties()) {
            pboprefix << it.key << "=" << it.value << "\n";
        }
    }

 
    std::vector<std::string_view> excludeFiles;

    for (int j = 0; j < args.num_excludefiles; j++) {
        excludeFiles.emplace_back(args.excludefiles[j]);
    }

    std::vector<std::string_view> includeFiles;

    for (int j = 0; j < args.num_includefolders; j++) {
        if (std::string_view(args.includefolders[j]) == ".") continue;
        includeFiles.emplace_back(args.includefolders[j]);
    }

    for (auto& file : reader.getFiles()) {
        if (is_garbage(file)) continue;

        bool excluded = std::any_of(excludeFiles.begin(), excludeFiles.end(), [&name = file.name](std::string_view& ex) {
            return matches_glob(name.c_str(), ex.data());
        });
        if (excluded) continue;

        if (!includeFiles.empty()) {
            bool included = std::any_of(includeFiles.begin(), includeFiles.end(), [&name = file.name](std::string_view& ex) {
                return matches_glob(name.c_str(), ex.data());
            });
            if (!included) continue;
        }

        std::filesystem::path outputPath = outputFolder / file.name;

        if (!std::filesystem::exists(outputPath.parent_path())) //Folder doesn't exist. Create it.
            if (!std::filesystem::create_directories(outputPath.parent_path()))
                logger.error("Failed to create folder %s.\n", outputPath.parent_path().string().c_str());

        // open target file
        if (std::filesystem::exists(outputPath) && !args.force) {
            logger.error("File %s already exists and --force was not set.\n", outputPath.string().c_str());
            return 7;
        }


        auto& fs = reader.getFileBuffer(file);
        std::istream source(&fs);

        if (file.name == "config.bin") {
            fs.setBufferSize(std::min(file.data_size, 1024*1024u));//increase buffer size to max 1MB. So that seeking around is fast while parsing the config

            std::ofstream target(outputPath.parent_path() / "config.cpp", std::ofstream::binary);

            auto config = Config::fromBinarized(source, logger, false);

            config.toPlainText(target, logger);
        } else if(outputPath.extension() == ".rvmat") { //We want to debinarize rvmat's on extract
            fs.setBufferSize(std::min(file.data_size, 1024 * 1024u));//increase buffer size to max 1MB. So that seeking around is fast while parsing the config

            std::ofstream target(outputPath, std::ofstream::binary);

            auto config = Config::fromBinarized(source, logger, false);

            config.toPlainText(target, logger);
        } else {
            __itt_task_begin(unpackDomain, __itt_null, __itt_null, handle_cmd_unpackF);

            std::ofstream target(outputPath, std::ofstream::binary);
            std::array<char, 4096> buf;
            do {
                source.read(buf.data(), buf.size());
                target.write(buf.data(), source.gcount());
            } while (source.gcount() == buf.size()); //if gcount is not full buffer, we reached EOF before filling the buffer till end
            __itt_task_end(unpackDomain);
        }

    }

    return 0;
}


int cmd_cat(Logger& logger) {
    extern struct arguments args;

    if (args.num_positionals < 3)
        return 128;

    // remove trailing slash in target
    if (args.positionals[1][strlen(args.positionals[1]) - 1] == PATHSEP)
        args.positionals[1][strlen(args.positionals[1]) - 1] = 0;

    std::ifstream input(args.positionals[1], std::ifstream::binary);
    if (!input.is_open()) {
        logger.error("Failed to open %s.\n", args.positionals[1]);
        return 1;
    }

    std::string findFile = args.positionals[2];

    PboReader reader(input);
    reader.readHeaders();
    auto& files = reader.getFiles();

    auto found = std::find_if(files.begin(), files.end(),[&findFile](const PboEntry& entry)
    {
        return iequals(entry.name,findFile);
    });


    if (found == files.end()) {
        logger.error("PBO does not contain the file %s.\n", args.positionals[2]);
        return 5;
    }

    auto& fs = reader.getFileBuffer(*found);
    std::istream source(&fs);

    std::array<char, 4096*2> buf;
    do {
        source.read(buf.data(), buf.size());
        std::cout.write(buf.data(), source.gcount());
    } while (source.gcount() == buf.size()); //if gcount is not full buffer, we reached EOF before filling the buffer till end

    return 0;
}
