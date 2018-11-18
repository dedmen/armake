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
#include <vector>
#include <algorithm>

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

    uint32_t original_size;
    uint32_t data_size;
    uint32_t startOffset;
    PboEntryPackingMethod method;


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
    uint32_t propertiesEnd;
    uint32_t headerEnd;
    std::istream& input;
public:
    PboReader(std::istream &input) : input(input) {}
    void readHeaders();
    const auto& getFiles() const noexcept { return files; }
    PboEntryBuffer getFileBuffer(const PboEntry& ent) const {
        return PboEntryBuffer(*this, ent);
    }


};



int cmd_inspect();

int cmd_unpack();

int cmd_cat();
