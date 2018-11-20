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

#pragma once


#define MAXFILES 4096

#include <fstream>
#include <utility>
#include <vector>
#include <algorithm>
#include <sstream>
#include <filesystem>


struct header {
    char name[512];
    uint32_t packing_method;
    uint32_t original_size;
    uint32_t data_size;
};

class PboProperty {
public:
    std::string key;
    std::string value;

    bool read(std::istream& in);
    void write(std::ostream& out) const;
};

enum class PboEntryPackingMethod {
    none,
    version,
    compressed,
    encrypted
};

class PboEntry {
public:
    std::string name;

    uint32_t original_size = 0;
    uint32_t data_size = 0;
    uint32_t startOffset = 0;
    PboEntryPackingMethod method = PboEntryPackingMethod::none;


    void read(std::istream& in);
    void write(std::ostream& out, bool noDate = false) const;
};

class PboReader;
class PboEntryBuffer : public std::streambuf {
    std::vector<char> buffer;
    const PboEntry& file;
    const PboReader& reader;
    //What position the character after the last character that's currently in our buffer, corresponds to in the pbofile
    //Meeaning on next read, the first character read is that pos
    size_t bufferEndFilePos{0}; 
    // context for the compression
public:
    PboEntryBuffer(const PboReader& rd, const PboEntry& ent, uint32_t bufferSize = 4096u) : buffer(std::min(ent.data_size, bufferSize)), file(ent), reader(rd) {
        char *end = &buffer.front() + buffer.size();
        setg(end, end, end);
    }

    int underflow() override;
    int64_t __CLR_OR_THIS_CALL xsgetn(char* _Ptr, int64_t _Count) override;

    void setBuffersize(size_t newSize) {
        buffer.resize(newSize);
    }
};

class PboReader {
    friend class PboEntryBuffer;

    std::vector<PboEntry> files;
    std::vector<PboProperty> properties;
    uint32_t propertiesEnd{0};
    uint32_t headerEnd{0};
    std::istream& input;
public:
    PboReader(std::istream &input) : input(input) {}
    void readHeaders();
    const auto& getFiles() const noexcept { return files; }
    PboEntryBuffer getFileBuffer(const PboEntry& ent) const {
        return PboEntryBuffer(*this, ent);
    }


};

class PboFileToWrite : std::enable_shared_from_this<PboFileToWrite> { //#TODO need a better name
protected:
    PboEntry entryInfo;
public:
    virtual ~PboFileToWrite() = default;
    //return info like name and filesize (packing method not supported yet, startOffset/originalSize are ignored)
    //Might be called by multiple threads!
    virtual PboEntry& getEntryInformation() {
        return entryInfo;
    }
    virtual void writeDataTo(std::ostream &output) = 0;

};

class PboFTW_CopyFromFile : public PboFileToWrite {
    std::filesystem::path file;
public:
    PboFTW_CopyFromFile(std::string filename, std::filesystem::path inputFile) : file(std::move(inputFile)) {
        entryInfo.name = std::move(filename);
        //only backslash in pbo
        std::replace(entryInfo.name.begin(), entryInfo.name.end(), '/', '\\');
        entryInfo.data_size = entryInfo.original_size = std::filesystem::file_size(file); //I expect file size not to change.
    }
    //PboEntry& getEntryInformation() override {
    //    entryInfo.data_size = entryInfo.original_size = std::filesystem::file_size(file);
    //    return entryInfo;
    //}
    void writeDataTo(std::ostream& output) override;
};

class PboFTW_FromString : public PboFileToWrite {
    std::string data;
public:
    PboFTW_FromString(std::string filename, std::string input) : data(std::move(input)) {
        entryInfo.name = std::move(filename);
        //only backslash in pbo
        std::replace(entryInfo.name.begin(), entryInfo.name.end(), '/', '\\');
        entryInfo.data_size = entryInfo.original_size = data.length();
    }
    void writeDataTo(std::ostream& output) override;
};



class PboWriter {
    std::vector<PboProperty> properties;
    std::vector<std::shared_ptr<PboFileToWrite>> filesToWrite;
public:


    void addProperty(PboProperty prop) { properties.emplace_back(std::move(prop)); }
    void addFile(std::shared_ptr<PboFileToWrite> file) { filesToWrite.emplace_back(std::move(file)); }
    void writePbo(std::ostream &output);
};



int cmd_inspect();

int cmd_unpack();

int cmd_cat();
