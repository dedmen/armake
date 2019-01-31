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

#include <filesystem>
#include "logger.h"
#include "unpack.h"
extern "C" {
#include "sha1.h"
}
#include "args.h"
#include "filesystem.h"
#include "utils.h"
#include "keygen.h"
#include "sign.h"

#include <wolfssl/openssl/bn.h>

//Written by Dedmen for Intercept
template <class T, auto D>
class ManagedObject {
public:
    T obj = nullptr;

    ManagedObject() = default;
    ManagedObject(T s) : obj(s) {}

    ~ManagedObject() {
        if (!obj) return;

        //Special case for CertCloseStore
        if constexpr (std::is_invocable_v<decltype(D), T, DWORD>)
            D(obj, 0);
        else
            D(obj);
    }

    T operator->() { return obj; }
    T* operator&() { return &obj; }
    T& operator*() { return *obj; }
    operator T() { return obj; }

    ManagedObject& operator=(T* s) {
        obj = s;
        return *this;
    }
};

//#define BISIGN_V2
void pad_hash(const std::array<unsigned char,20>& hash, std::vector<char>& buffer, size_t buffsize) {
    int i;

    buffer[0] = 0;
    buffer[1] = 1;

    for (i = 0; i < (buffsize - 38); i++)
        buffer[2 + i] = 255;



    memcpy(buffer.data() + buffsize - 36,
        "\x00\x30\x21\x30\x09\x06\x05\x2b\x0e\x03\x02\x1a\x05\x00\x04\x14", 16);
    memcpy(buffer.data() + buffsize - 20, hash.data(), 20);
}

int name_hash_sort(const void *av, const void *bv) {
    const char *a = *((const char **)av);
    const char *b = *((const char **)bv);
    int i;

    for (i = 0; a[i] != 0 && b[i] != 0; i++) {
        if (a[i] < b[i])
            return -1;
        if (a[i] > b[i])
            return 1;
    }

    return 0;
}

#define REVERSE_DIGEST_ENDIANNESS(x) for (unsigned int& i : x.Message_Digest) reverse_endianness(&i, sizeof(i));

//#TODO first part of signature is the pbo checksum that the pbowriter already wrote to the end, find a way to reuse that
int sign_pbo(std::filesystem::path path_pbo, std::filesystem::path path_privatekey, std::filesystem::path path_signature) {
    SHA1Context sha;
    std::string prefix;

    std::ifstream pboInput(path_pbo, std::ifstream::binary);
    PboReader reader(pboInput);
    reader.readHeaders();

    auto& pboProperties = reader.getProperties();
    if (auto found = std::find_if(pboProperties.begin(), pboProperties.end(), [](const PboProperty& prop) {
        return prop.key == "prefix";
    }); found != pboProperties.end()) {
        prefix = found->value+'\\'; //the \ at end is important
    }

    std::vector<std::string> filenames;
    for (auto& it : reader.getFiles())
        filenames.emplace_back(it.name);

    for (auto& it : filenames)
        std::transform(it.begin(), it.end(), it.begin(), ::tolower);
    
    std::sort(filenames.begin(), filenames.end());

    // calculate name hash
    SHA1Reset(&sha);
    for (auto& file : filenames)
        SHA1Input(&sha, reinterpret_cast<const unsigned char*>(file.c_str()), file.length());

    if (!SHA1Result(&sha))
        return 1;

    for (unsigned int& i : sha.Message_Digest) reverse_endianness(&i, sizeof(i));

    std::array<unsigned char,20> namehash;
    memcpy(namehash.data(), &sha.Message_Digest[0], 20);

    // calculate file hash
    SHA1Reset(&sha);

    bool foundFiles = false;
    for (auto& entry : reader.getFiles()) {
        auto ext = std::filesystem::path(entry.name).extension().string();
#ifdef BISIGN_V2
        //blacklist
        if (
            ext == ".paa" ||
            ext == ".jpg" ||
            ext == ".p3d" ||
            ext == ".tga" ||
            ext == ".rvmat" ||
            ext == ".lip" ||
            ext == ".ogg" ||
            ext == ".wss" ||
            ext == ".png" ||
            ext == ".rtm" ||
            ext == ".pac" ||
            ext == ".fxy" ||
            ext == ".wrp")
            continue;
#else
        //whitelist
        if (
            ext != ".sqf" &&
            ext != ".inc" &&
            ext != ".bikb" &&
            ext != ".ext" &&
            ext != ".fsm" &&
            ext != ".sqm" &&
            ext != ".hpp" &&
            ext != ".cfg" &&
            ext != ".sqs" &&
            ext != ".h")
            continue;
#endif
        foundFiles = true;
        auto fileBuf = reader.getFileBuffer(entry);
        std::istream source(&fileBuf);
        std::array<char, 4096> buf;
        do {
            source.read(buf.data(), buf.size());
            SHA1Input(&sha, reinterpret_cast<const unsigned char*>(buf.data()), source.gcount());
        } while (source.gcount() == buf.size()); //if gcount is not full buffer, we reached EOF before filling the buffer till end
    }

    if (!foundFiles)
    #ifdef BISIGN_V2
        SHA1Input(&sha, reinterpret_cast<const unsigned char *>("nothing"), strlen("nothing"));
    #else
        SHA1Input(&sha, reinterpret_cast<const unsigned char *>("gnihton"), strlen("gnihton"));
    #endif

    if (!SHA1Result(&sha))
        return 1;

    REVERSE_DIGEST_ENDIANNESS(sha)

    std::array<unsigned char,20> filehash;
    memcpy(filehash.data(), &sha.Message_Digest[0], 20);

    // get hash 1
    auto& pboHash = reader.getHash();
  
    // calculate hash 2
    SHA1Reset(&sha);
    SHA1Input(&sha, pboHash.data(), 20);
    SHA1Input(&sha, namehash.data(), 20);
    if (!prefix.empty())
        SHA1Input(&sha, reinterpret_cast<const unsigned char *>(prefix.c_str()), prefix.length());

    if (!SHA1Result(&sha))
        return 1;

    REVERSE_DIGEST_ENDIANNESS(sha)
    
    std::array<unsigned char,20> hash2;
    memcpy(hash2.data(), &sha.Message_Digest[0], 20);

    // calculate hash 3
    SHA1Reset(&sha);
    SHA1Input(&sha, filehash.data(), 20);
    SHA1Input(&sha, namehash.data(), 20);
    if (!prefix.empty())
        SHA1Input(&sha, reinterpret_cast<const unsigned char *>(prefix.c_str()), prefix.length());

    if (!SHA1Result(&sha))
        return 1;

    REVERSE_DIGEST_ENDIANNESS(sha)
    
    std::array<unsigned char,20> hash3;
    memcpy(hash3.data(), &sha.Message_Digest[0], 20);

    // read private key data

    std::ifstream privateKeyFile(path_privatekey, std::ifstream::binary);

    if (!privateKeyFile.is_open())
        return 1;

    std::string keyname;
    std::getline(privateKeyFile, keyname, '\0');
    privateKeyFile.seekg(16, std::ifstream::cur);
    uint32_t keylength;
    uint32_t exponent_le;

    privateKeyFile.read(reinterpret_cast<char*>(&keylength), 4);
    privateKeyFile.read(reinterpret_cast<char*>(&exponent_le), 4);

    std::vector<char> buffer;
    buffer.resize(keylength/8);
    privateKeyFile.read(buffer.data(), keylength/8);

    reverse_endianness(buffer.data(), keylength / 8);

    ManagedObject<BIGNUM*,BN_free> modulus = BN_new();
    BN_bin2bn(reinterpret_cast<const unsigned char*>(buffer.data()), keylength / 8, modulus);

    privateKeyFile.seekg((keylength / 16) * 5, std::ifstream::cur);

    privateKeyFile.read(buffer.data(), keylength/8);
    reverse_endianness(buffer.data(), keylength / 8);
    ManagedObject<BIGNUM*,BN_free> exp = BN_new();
    BN_bin2bn(reinterpret_cast<const unsigned char*>(buffer.data()), keylength / 8, exp);

    // generate signature values
    pad_hash(pboHash, buffer, keylength / 8);
    ManagedObject<BIGNUM*,BN_free> hash1_padded = BN_new();
    BN_bin2bn(reinterpret_cast<const unsigned char*>(buffer.data()), keylength / 8, hash1_padded);

    pad_hash(hash2, buffer, keylength / 8);
    ManagedObject<BIGNUM*,BN_free> hash2_padded = BN_new();
    BN_bin2bn(reinterpret_cast<const unsigned char*>(buffer.data()), keylength / 8, hash2_padded);

    pad_hash(hash3, buffer, keylength / 8);
    ManagedObject<BIGNUM*,BN_free> hash3_padded = BN_new();
    BN_bin2bn(reinterpret_cast<const unsigned char*>(buffer.data()), keylength / 8, hash3_padded);

    ManagedObject<BN_CTX*,BN_CTX_free> bignum_context = BN_CTX_new();

    ManagedObject<BIGNUM*,BN_free> sig1 = BN_new();
    BN_mod_exp(sig1, hash1_padded, exp, modulus, bignum_context);

    ManagedObject<BIGNUM*,BN_free> sig2 = BN_new();
    BN_mod_exp(sig2, hash2_padded, exp, modulus, bignum_context);

    ManagedObject<BIGNUM*,BN_free> sig3 = BN_new();
    BN_mod_exp(sig3, hash3_padded, exp, modulus, bignum_context);

    // write to file

    std::ofstream signatureFile(path_signature, std::ofstream::binary);

    if (!signatureFile.is_open()) {
        return 1;
    }

    signatureFile.write(keyname.c_str(), keyname.length()+1); //max. 512 B


    uint32_t temp = keylength / 8 + 20;
    signatureFile.write(reinterpret_cast<const char*>(&temp), 4); //4 B
    signatureFile.write("\x06\x02\x00\x00\x00\x24\x00\x00", 8); //8 B
    signatureFile.write("RSA1", 4); //4 B

    signatureFile.write(reinterpret_cast<const char*>(&keylength), 4); //4 B
    signatureFile.write(reinterpret_cast<const char*>(&exponent_le), 4); //4 B

    custom_bn2lebinpad(modulus, reinterpret_cast<unsigned char *>(buffer.data()), keylength / 8);
    signatureFile.write(buffer.data(), keylength / 8); //128 B

    temp = keylength / 8;
    signatureFile.write(reinterpret_cast<const char*>(&temp), 4); //4 B

    custom_bn2lebinpad(sig1, reinterpret_cast<unsigned char *>(buffer.data()), keylength / 8);
    signatureFile.write(buffer.data(), keylength / 8); //128 B

    #ifdef BISIGN_V2
        temp = 2;
    #else
        temp = 3;
    #endif
    
    signatureFile.write(reinterpret_cast<const char*>(&temp), 4); //4 B

    temp = keylength / 8;
    signatureFile.write(reinterpret_cast<const char*>(&temp), 4); //4 B

    custom_bn2lebinpad(sig2, reinterpret_cast<unsigned char *>(buffer.data()), keylength / 8);
    signatureFile.write(buffer.data(), keylength / 8); //128 B

    temp = keylength / 8;
    signatureFile.write(reinterpret_cast<const char*>(&temp), 4); //4 B

    custom_bn2lebinpad(sig3, reinterpret_cast<unsigned char *>(buffer.data()), keylength / 8);
    signatureFile.write(buffer.data(), keylength / 8); //128 B
    return 0;
}

int cmd_sign(Logger& logger) {
    extern struct arguments args;

    if (args.num_positionals != 3)
        return 128;

    if (strcmp(strrchr(args.positionals[1], '.'), ".biprivatekey") != 0) {
        logger.error("File %s doesn't seem to be a valid private key.\n", args.positionals[1]);
        return 1;
    }


    std::filesystem::path keyname(args.positionals[1]);

    std::string path_signature;

    if (args.signature) {
        path_signature = args.signature;
    } else {
        path_signature = std::string(args.positionals[2]) + "." + keyname.filename().string();
    }

    if (std::filesystem::path(path_signature).extension() != ".bisign")
        path_signature += ".bisign";

    if (std::filesystem::exists(path_signature) && !args.force) {
        logger.error("File %s already exists and --force was not set.\n", path_signature);
        return 1;
    }

    int success = sign_pbo(args.positionals[2], args.positionals[1], path_signature);

    if (success)
        logger.error("Failed to sign file.\n");

    return success;
}
