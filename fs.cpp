#include <iostream>
#include <string>
#include <cstring>
#include <iomanip>
#include "fs.h"

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
    disk.read(ROOT_BLOCK, (uint8_t*)root);
    disk.read(FAT_BLOCK, (uint8_t*)fat);
    disk.read(ROOT_BLOCK, (uint8_t*)workingDir);
}

FS::~FS()
{

}

// returns block number of target directory
int
FS::findTargetDir(std::string inPath)
{
    std::string path = inPath;
    dir_entry curDir[64];
    if (path.at(0) == '/') { // absolute path
        //std::cout << "abs" << std::endl;
        if (path.length() == 1) {
            //std::cout << "root" << std::endl;
            return ROOT_BLOCK;
        }
        disk.read(ROOT_BLOCK, (uint8_t*)curDir);
        path.erase(0, 1);
    }
    else { // relative path
        //std::cout << "rel" << std::endl;
        disk.read(workingDir[0].first_blk, (uint8_t*)curDir);
    }
    int nameLen;
    std::string name;
    while (true) {
        nameLen = path.find('/');
        if (nameLen == std::string::npos) { // path string has no more slashes
            return curDir[0].first_blk;
        }
        name = path.substr(0, nameLen);
        for (int i = 1; i < BLOCK_SIZE / 64 + 1; i++) {
            if (strncmp(curDir[i].file_name, name.c_str(), 56) == 0 && curDir[i].type == TYPE_DIR) {
                disk.read(curDir[i].first_blk, (uint8_t*)curDir);
                path = path.substr(nameLen + 1);
                break;
            }
            if (i == 64) {
                return -1;
            }
        }    
    }
}

// returns first block in FAT marked as FAT_FREE
int
FS::firstFreeBlk()
{
    for (int i = 0; i < BLOCK_SIZE/2; i++) {
        if (fat[i] == FAT_FREE) {
            return i;
        }
    }
    return -1;
}
int
FS::updateWorkingDir()
{
    disk.read(workingDir[0].first_blk, (uint8_t*)workingDir);
    return 0;
}

// formats the disk, i.e., creates an empty file system
int
FS::format()
{
    for (int i = 0; i < BLOCK_SIZE / 2; i++) {
        fat[i] = FAT_FREE;
    }
    fat[ROOT_BLOCK] = EOF;
    fat[FAT_BLOCK] = EOF;

    for (int i = 0; i < BLOCK_SIZE / 64; i++) { //initialize every dir_entry in root directory
        root[i].access_rights = 0;
        root[i].first_blk = 0;
        root[i].size = 0;
        root[i].type = TYPE_FILE;
        root[i].file_name[0] = '\0';
    }
    root[0].first_blk == ROOT_BLOCK;
    root[0].access_rights = READ | WRITE | EXECUTE;
    strncpy(root[0].file_name, "/", 56);
    root[0].size = BLOCK_SIZE;
    root[0].type = TYPE_DIR;
    root[1].first_blk == ROOT_BLOCK;
    root[1].access_rights = READ | WRITE | EXECUTE;
    strncpy(root[1].file_name, "..", 56);
    root[1].size = BLOCK_SIZE;
    root[1].type = TYPE_DIR;
    for (int i = 0; i < BLOCK_SIZE / 64; i++) {
        workingDir[i] = root[i];
    }
    disk.write(ROOT_BLOCK, (uint8_t*)root);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(std::string filepath)
{
    int16_t fatSnapshot[BLOCK_SIZE / 2]; // backup of FAT 
    memcpy(fatSnapshot, fat, BLOCK_SIZE);

    std::string path = filepath;
    dir_entry curDir[64];
    int curDirBlk = this->findTargetDir(path);
    if (curDirBlk == -1) {
        std::cout << "Invalid path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDir);
    dir_entry newFile;
    int nameInd = path.find_last_of('/');
    std::string filename;
    if (nameInd == std::string::npos) {
        filename = path;
    }
    else {
        filename = path.substr(nameInd);
        filename.erase(0, 1);
    }
    if (!(curDir[0].access_rights & WRITE)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    if (filename.empty()) {
        std::cout << "File needs a name." << std::endl;
        return -1;
    }
    if (filename.length() >= 56) {
        std::cout << "File name too long, max 55 characters." << std::endl;
        return -1;
    }
    for (int i = 2; i < BLOCK_SIZE / 64; i++) {
        if (strncmp(curDir[i].file_name, filename.c_str(), 56) == 0) {
            std::cout << "File with name '" << filename << "' already exists." << std::endl;
            return -1;
        }
    }
    strncpy(newFile.file_name, filename.c_str(), 56);
    int dirIndex;
    for (int i = 0; i < (BLOCK_SIZE / 64) + 1; i++) {
        if (i == BLOCK_SIZE / 64) {
            std::cout << "Directory full, cannot create file." << std::endl;
            return -1;
        }
        if (strlen(curDir[i].file_name) == 0 && curDir[i].first_blk == 0) {
            dirIndex = i;
            break;
        }
    }

    std::string toFile, input;
    while (std::getline(std::cin, input)) {
        if (input.empty()) {
            break;
        }
        toFile.append(input);
        toFile.append("\n");
    }
    newFile.size = toFile.size();
    newFile.access_rights = READ | WRITE | EXECUTE;
    newFile.type = TYPE_FILE;

    int freeBlk = this->firstFreeBlk();
    int prevBlk;
    if (freeBlk == -1) {
        std::cout << "No free blocks." << std::endl;
        return -1;
    }
    newFile.first_blk = freeBlk;

    int blksUsed = 1 + (int)newFile.size / (int)BLOCK_SIZE;
    if (blksUsed == 1) {
        fat[newFile.first_blk] = FAT_EOF;
    }
    else {
        for (int i = 0; i < blksUsed - 1; i++) {
            prevBlk = freeBlk;
            fat[prevBlk] = FAT_EOF;
            freeBlk = this->firstFreeBlk();
            if (freeBlk == -1) {
                std::cout << "Not enough free blocks." << std::endl;
                memcpy(fat, fatSnapshot, BLOCK_SIZE); // restore FAT
                return -1;
            }
            fat[prevBlk] = freeBlk;
        }
        fat[freeBlk] = FAT_EOF;
    }
    prevBlk = newFile.first_blk;
    int dataStart;
    std::string data;
    for (int i = 0; i < blksUsed; i++) {
        dataStart = i * BLOCK_SIZE;
        data = toFile.substr(dataStart, BLOCK_SIZE);
        disk.write(prevBlk, (uint8_t*)data.c_str());
        prevBlk = fat[prevBlk];
    }

    curDir[dirIndex] = newFile;
    disk.write(curDir[0].first_blk, (uint8_t*)curDir);
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    this->updateWorkingDir();

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    std::string path = filepath;
    dir_entry curDir[64];
    int curDirBlk = this->findTargetDir(path);
    if (curDirBlk == -1) {
        std::cout << "Invalid path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDir);
    dir_entry newFile;
    int nameInd = path.find_last_of('/');
    std::string filename;
    if (nameInd == std::string::npos) {
        filename = path;
    }
    else {
        filename = path.substr(nameInd);
        filename.erase(0, 1);
    }

    int inDir = 0;
    int index;
    if (filename.empty()) {
        std::cout << "Must enter a file name." << std::endl;
        return -1;
    }
    for (int i = 2; i < BLOCK_SIZE / 64; i++) {
        if (strncmp(curDir[i].file_name, filename.c_str(), 56) == 0) {
            inDir = 1;
            index = i;
            break;
        }
    }
    if (inDir == 0) {
        std::cout << "No such file found." << std::endl;
        return -1;
    }
    if (!(curDir[index].access_rights & READ)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    if (curDir[index].type == TYPE_DIR) {
        std::cout << filepath << " is a directory." << std::endl;
        return -1;
    }
    int currentBlk = curDir[index].first_blk;
    char data[BLOCK_SIZE];
    int remaining = curDir[index].size;
    while (true) {
        char buf[BLOCK_SIZE];
        disk.read(currentBlk, (uint8_t*)data);
        if (remaining > BLOCK_SIZE) {
            memcpy(buf, data, BLOCK_SIZE);
        }
        else {
            memcpy(buf, data, remaining);
        }
        std::cout << buf << std::endl;
        if (fat[currentBlk] == FAT_EOF) {
            break;
        }
        remaining = remaining - BLOCK_SIZE;
        currentBlk = fat[currentBlk];
    }

    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    updateWorkingDir();
    if (!(workingDir[0].access_rights & READ)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    std::cout << std::left << std::setw(56) << "name" << "type\taccess rights\tsize" << std::endl;
    for (int i = 2; i < BLOCK_SIZE / 64; i++) {
        if (strlen(workingDir[i].file_name) != 0) {
            std::string rights;
            std::cout << std::left << std::setw(56) << workingDir[i].file_name;
            if (workingDir[i].access_rights & READ) {
                rights.push_back('r');
            }
            else {
                rights.push_back('-');
            }
            if (workingDir[i].access_rights & WRITE) {
                rights.push_back('w');
            }
            else {
                rights.push_back('-');
            }
            if (workingDir[i].access_rights & EXECUTE) {
                rights.push_back('x');
            }
            else {
                rights.push_back('-');
            }
            if (workingDir[i].type == TYPE_DIR) {
                std::cout << "dir\t";
            }
            else {
                std::cout << "file\t";
            }
            std::cout << rights << "\t\t";
            if (workingDir[i].type == TYPE_DIR) {
                std::cout << "-" << std::endl;
            }
            else {
                std::cout << std::to_string(workingDir[i].size) << std::endl;
            }
        }
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    int16_t fatSnapshot[BLOCK_SIZE / 2];
    memcpy(fatSnapshot, fat, BLOCK_SIZE);

    dir_entry copy;

    std::string source = sourcepath;
    std::string destination = destpath;

    dir_entry curDirS[64];
    int curDirBlk = this->findTargetDir(source);
    if (curDirBlk == -1) {
        std::cout << "Invalid source path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDirS);
    int nameInd = source.find_last_of('/');
    std::string srcname;
    if (nameInd == std::string::npos) {
        srcname = source;
    }
    else {
        srcname = source.substr(nameInd);
        srcname.erase(0, 1);
    }

    dir_entry curDirD[64];
    curDirBlk = this->findTargetDir(destination);
    if (curDirBlk == -1) {
        std::cout << "Invalid destination path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDirD);
    nameInd = destination.find_last_of('/');
    std::string destname;
    if (nameInd == std::string::npos) {
        destname = destination;
    }
    else {
        destname = destination.substr(nameInd);
        if (destname != "/") {
            destname.erase(0, 1);
        }
        else {
            destname = srcname;
        }
    }

    if (srcname.empty()) {
        std::cout << "Source file name must not be empty." << std::endl;
        return -1;
    }
    if (destname.empty()) {
        std::cout << "Destination file name must not be empty." << std::endl;
        return -1;
    }
    if (destname.length() >= 56) {
        std::cout << "Destination file name too long, max 55 characters." << std::endl;
        return -1;
    }
    int sInDir = 0;
    int index;
    int destDir = 0;
    int freeIndex;
    for (int i = 1; i < (BLOCK_SIZE / 64); i++) {
        if (strncmp(curDirS[i].file_name, srcname.c_str(), 56) == 0) {
            sInDir = 1;
            index = i;
            break;
        }
    }
    if (!(curDirS[index].access_rights & READ)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    if (curDirS[index].type != TYPE_FILE) {
        std::cout << "Cannot copy a directory." << std::endl;
    }
    if (sInDir == 0) {
        std::cout << source << " could not be found." << std::endl;
        return -1;
    }
    for (int i = 1; i < BLOCK_SIZE / 64; i++) {
        if (strncmp(curDirD[i].file_name, destname.c_str(), 56) == 0) {
            if (curDirD[i].type == TYPE_DIR) {
                destDir = 1;
                disk.read(curDirD[i].first_blk, (uint8_t*)curDirD);
                destname = srcname;
                for (int i = 1; i < BLOCK_SIZE / 64; i++) {
                    if (strncmp(curDirD[i].file_name, destname.c_str(), 56) == 0) {
                        std::cout << "File " << destname << " already exists." << std::endl;
                        return -1;
                    }
                }
                //strncpy(copy.file_name, curDirS[index].file_name, 56);
                break;
            }
            else {
                std::cout << "File " << destname << " already exists." << std::endl;
                return -1;
            }
        }
    }
    strncpy(copy.file_name, destname.c_str(), 56);

    if (!(curDirD[0].access_rights & WRITE)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    for (int i = 1; i < BLOCK_SIZE / 64 + 1; i++) {
        if (strlen(curDirD[i].file_name) == 0 && curDirD[i].first_blk == 0) {
            freeIndex = i;
            break;
        }
        if (i == BLOCK_SIZE / 64) {
            std::cout << "No free space in destination directory." << std::endl;
            return -1;
        }
    }

    copy.size = curDirS[index].size;
    copy.access_rights = curDirS[index].access_rights;
    copy.type = curDirS[index].type;
    int freeBlk = this->firstFreeBlk();
    int prevBlk;
    if (freeBlk == -1) {
        std::cout << "No free blocks." << std::endl;
        return -1;
    }
    copy.first_blk = freeBlk;

    int currentCpBlk = curDirS[index].first_blk;
    uint8_t cpData[BLOCK_SIZE];
    while (true) {
        prevBlk = freeBlk;
        disk.read(currentCpBlk, cpData);
        disk.write(prevBlk, cpData);
        fat[prevBlk] = FAT_EOF;
        if (fat[currentCpBlk] == FAT_EOF) {
            break;
        }
        freeBlk = this->firstFreeBlk();
        if (freeBlk == -1) {
            std::cout << "Not enough free blocks." << std::endl;
            memcpy(fat, fatSnapshot, BLOCK_SIZE);
            return -1;
        }
        fat[prevBlk] = freeBlk;
        currentCpBlk = fat[currentCpBlk];
    }
    curDirD[freeIndex] = copy;
    disk.write(curDirD[0].first_blk, (uint8_t*)curDirD);
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    this->updateWorkingDir();

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::string source = sourcepath;
    std::string destination = destpath;

    dir_entry curDirS[64];
    int curDirBlk = this->findTargetDir(source);
    if (curDirBlk == -1) {
        std::cout << "Invalid source path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDirS);
    int nameInd = source.find_last_of('/');
    std::string srcname;
    if (nameInd == std::string::npos) {
        srcname = source;
    }
    else {
        srcname = source.substr(nameInd);
        srcname.erase(0, 1);
    }

    dir_entry curDirD[64];
    curDirBlk = this->findTargetDir(destination);
    if (curDirBlk == -1) {
        std::cout << "Invalid destination path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDirD);
    nameInd = destination.find_last_of('/');
    std::string destname;
    if (nameInd == std::string::npos) {
        destname = destination;
    }
    else {
        destname = destination.substr(nameInd);
        if (destname != "/") {
            destname.erase(0, 1);
        }
        else {
            destname = srcname;
        }
    }

    if (source.empty()) {
        std::cout << "Source file name must not be empty." << std::endl;
        return -1;
    }
    if (destination.empty()) {
        std::cout << "Destination file name must not be empty." << std::endl;
        return -1;
    }
    if (destination.length() >= 56){
        std::cout << "Destination file name too long, max 55 characters." << std::endl;
        return -1;
    }
    int sInDir = 0;
    int dInDir = 0;
    int index;
    int freeIndex;
    for (int i = 1; i < BLOCK_SIZE / 64; i++) {
        if (strncmp(curDirS[i].file_name, srcname.c_str(), 56) == 0) {
            sInDir = 1;
            index = i;
            break;
        }
    }
    if (!(curDirS[index].access_rights & READ || curDirS[index].access_rights & WRITE)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    if (sInDir == 0) {
        std::cout << source << " could not be found." << std::endl;
        return -1;
    }
    if (curDirS[index].type != TYPE_FILE) {
        std::cout << "Cannot move directory." << std::endl;
        return -1;
    }
    for (int i = 1; i < 64; i++) {
        if (strncmp(curDirD[i].file_name, destname.c_str(), 56) == 0) {
            if (curDirD[i].type == TYPE_DIR) {
                disk.read(curDirD[i].first_blk, (uint8_t*)curDirD);
                dInDir = 1;
                destname = srcname;
                for (int i = 1; i < 64; i++) {
                    if (strncmp(curDirD[i].file_name, destname.c_str(), 56) == 0) {
                        std::cout << destname << " already exists." << std::endl;
                        return -1;
                    }
                }
                break;
            }
            else {
                std::cout << destname << " already exists." << std::endl;
                return -1;
            }
        }
    }
    if (!(curDirD[0].access_rights & WRITE)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    for (int i = 0; i < 64 + 1; i++) {
        if (strlen(curDirD[i].file_name) == 0 && curDirD[i].first_blk == 0) {
            freeIndex = i;
            break;
        }
        if (i == 64) {
            std::cout << "Directory " << destination << " is full." << std::endl;
            return -1;
        }
    }
    if (dInDir == 0) {
        strncpy(curDirS[index].file_name, destname.c_str(), 56);
    }
    if (curDirS[0].first_blk != curDirD[0].first_blk) {
        curDirD[freeIndex] = curDirS[index];
        disk.write(curDirD[0].first_blk, (uint8_t*)curDirD);
        curDirS[index].access_rights = 0;
        curDirS[index].first_blk = 0;
        curDirS[index].size = 0;
        curDirS[index].file_name[0] = '\0';
        curDirS[index].type = TYPE_FILE;
    }
    disk.write(curDirS[0].first_blk, (uint8_t*)curDirS);
    this->updateWorkingDir();

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::string path = filepath;
    dir_entry curDir[64];
    int curDirBlk = this->findTargetDir(path);
    if (curDirBlk == -1) {
        std::cout << "Invalid path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDir);
    int nameInd = path.find_last_of('/');
    std::string filename;
    if (nameInd == std::string::npos) {
        filename = path;
    }
    else {
        filename = path.substr(nameInd);
        filename.erase(0, 1);
    }

    if (filename.empty()) {
        std::cout << "File name must not be empty." << std::endl;
        return -1;
    }
    int inDir = 0;
    int index;
    for (int i = 2; i < BLOCK_SIZE / 64; i++) {
        if (strncmp(curDir[i].file_name, filename.c_str(), 56) == 0) {
            inDir = 1;
            index = i;
            break;
        }
    }
    if (inDir == 0) {
        std::cout << "File could not be found." << std::endl;
        return -1;
    }
    if (!(curDir[index].access_rights & WRITE)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    if (curDir[index].type == TYPE_FILE) {
        int prev = curDir[index].first_blk;
        int next;
        while (true) {
            next = fat[prev];
            fat[prev] = FAT_FREE;
            if (next == FAT_EOF) {
                break;
            }
            prev = fat[prev];
        }
    }
    else if (curDir[index].type == TYPE_DIR) {
        dir_entry directory[64];
        disk.read(curDir[index].first_blk, (uint8_t*)directory);
        for (int i = 2; i < BLOCK_SIZE / 64; i++) {
            if (directory[i].access_rights != 0 || strlen(directory[i].file_name) > 0 ) {
                std::cout << "Directory must be empty." << std::endl;
                return -1;
            }
        }
        fat[curDir[index].first_blk] = FAT_FREE;
    }
    
    curDir[index].access_rights = 0;
    curDir[index].first_blk = 0;
    curDir[index].size = 0;
    curDir[index].file_name[0] = '\0';
    curDir[index].type = TYPE_FILE;

    disk.write(curDir[0].first_blk, (uint8_t*)curDir);
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    this->updateWorkingDir();

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    int16_t fatSnapshot[BLOCK_SIZE / 2];
    memcpy(fatSnapshot, fat, BLOCK_SIZE);

    std::string path1 = filepath1;
    std::string path2 = filepath2;

    dir_entry curDirS[64];
    int curDirBlk = this->findTargetDir(path1);
    if (curDirBlk == -1) {
        std::cout << "Invalid first path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDirS);
    int nameInd = path1.find_last_of('/');
    std::string name1;
    if (nameInd == std::string::npos) {
        name1 = path1;
    }
    else {
        name1 = path1.substr(nameInd);
        name1.erase(0, 1);
    }

    dir_entry curDirD[64];
    curDirBlk = this->findTargetDir(path2);
    if (curDirBlk == -1) {
        std::cout << "Invalid second path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDirD);
    nameInd = path2.find_last_of('/');
    std::string name2;
    if (nameInd == std::string::npos) {
        name2 = path2;
    }
    else {
        name2 = path2.substr(nameInd);
        name2.erase(0, 1);
    }

    if (name1.empty()) {
        std::cout << "File name 1 must not be empty." << std::endl;
        return -1;
    }
    if (name2.empty()) {
        std::cout << "File name 2 must not be empty." << std::endl;
        return -1;
    }
    int sInDir = 0;
    int sIndex;
    int dInDir = 0;
    int dIndex;
    for (int i = 1; i < 64; i++) {
        if (strncmp(name1.c_str(), curDirS[i].file_name, 56) == 0) {
            sInDir = 1;
            sIndex = i;
        }
        if (strncmp(name2.c_str(), curDirD[i].file_name, 56) == 0) {
            dInDir = 1;
            dIndex = i;
        }
    }
    if (sInDir == 0) {
        std::cout << path1 << " could not be found." << std::endl;
        return -1;
    }
    if (dInDir == 0) {
        std::cout << path2 << " could not be found." << std::endl;
        return -1;
    }
    if (curDirS[sIndex].type == TYPE_DIR) {
        std::cout << "Cannot append from directory." << std::endl;
        return - 1;
    }
    if (curDirD[dIndex].type == TYPE_DIR) {
        std::cout << "Cannot append to directory." << std::endl;
        return -1;
    }
    if (!(curDirS[sIndex].access_rights & READ) || !(curDirD[dIndex].access_rights & WRITE)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }

    int newSize = curDirS[sIndex].size + curDirD[dIndex].size;
    curDirD[dIndex].size = newSize;
    int destLastBlk = curDirD[dIndex].first_blk;
    while (fat[destLastBlk] != FAT_EOF) {
        destLastBlk = fat[destLastBlk];
    }
    int destlastBlkSize = curDirD[dIndex].size % BLOCK_SIZE;
    int appendSize = destlastBlkSize + curDirS[sIndex].size - 1;
    char data[appendSize];
    char buf[BLOCK_SIZE];
    disk.read(destLastBlk, (uint8_t*)buf);
    buf[destlastBlkSize - 1] = '\0';
    memcpy(data, buf, destlastBlkSize);
    int srcBlk = curDirS[sIndex].first_blk;
    int srcSize = curDirS[sIndex].size;
    while (true) {
        disk.read(srcBlk, (uint8_t*)buf);
        if (srcSize > BLOCK_SIZE) {
            strncat(data, buf, BLOCK_SIZE);
            destlastBlkSize = destlastBlkSize + BLOCK_SIZE;
            data[destlastBlkSize] = '\0';
        }
        else {
            strncat(data, buf, srcSize);
        }
        if (fat[srcBlk] == FAT_EOF) {
            break;
        }
        srcSize = srcSize - BLOCK_SIZE;
        srcBlk = fat[srcBlk];
    }
    int appendBlk = destLastBlk;
    std::string stringData(data);
    while (true) {
        if (stringData.length() == 0) {
            break;
        }
        disk.write(appendBlk, (uint8_t*)stringData.c_str());
        if (stringData.length() > BLOCK_SIZE) {
            stringData = stringData.substr(BLOCK_SIZE);
        }
        else {
            stringData.clear();
            break;
        }
        destLastBlk = appendBlk;
        appendBlk = this->firstFreeBlk();
        if (appendBlk == -1) {
            std::cout << "Not enough free blocks." << std::endl;
            memcpy(fat, fatSnapshot, BLOCK_SIZE);
            return -1;
        }
        fat[destLastBlk] = appendBlk;
    }
    fat[appendBlk] = FAT_EOF;
    /*
    while (true) {
        disk.read(blk, (uint8_t*)data);
        strcat(newData, data);
        if (fat[blk] == FAT_EOF) {
            break;
        }
        blk = fat[blk];
    }
    std::string datastr(newData);
    blk = curDirD[dIndex].first_blk;
    if (newSize < BLOCK_SIZE) {
        disk.write(blk, (uint8_t*)datastr.c_str());
    }
    else {
        int k = 0;
        std::string dataBlock;
        int freeBlk;
        while ((k + 1) * BLOCK_SIZE < newSize) {
            if (fat[blk] == FAT_EOF) {
                freeBlk = this->firstFreeBlk();
                if (freeBlk == -1) {
                    std::cout << "Not enough free blocks." << std::endl;
                    memcpy(fat, fatSnapshot, BLOCK_SIZE);
                    return -1;
                }
                fat[blk] = freeBlk;
                fat[freeBlk] = FAT_EOF;
            }
            dataBlock = datastr.substr(k * BLOCK_SIZE, BLOCK_SIZE);
            k++;
            disk.write(blk, (uint8_t*)dataBlock.c_str());
            blk = fat[blk];
        }
    }
    curDirD[dIndex].size = datastr.size();
    */
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    disk.write(curDirD[0].first_blk, (uint8_t*)curDirD);
    this->updateWorkingDir();

    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    std::string path = dirpath;
    dir_entry curDir[64];
    int curDirBlk = this->findTargetDir(path);
    if (curDirBlk == -1) {
        std::cout << "Invalid path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDir);
    int nameInd = path.find_last_of('/');
    dir_entry newDir;
    std::string dirname;
    if (nameInd == std::string::npos) {
        dirname = path;
    }
    else {
        dirname = path.substr(nameInd);
        dirname.erase(0, 1);
    }  
    if (!(curDir[0].access_rights & WRITE)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    if (dirname.empty()) {
        std::cout << "File name must not be empty." << std::endl;
        return -1;
    }
    if (dirname.length() >= 56) {
        std::cout << "File name too long, max 55 characters." << std::endl;
        return -1;
    }
    for (int i = 2; i < BLOCK_SIZE / 64; i++) {
        if (strncmp(curDir[i].file_name, dirname.c_str(), 56) == 0) {
            std::cout << "File with name '" << dirname << "' already exists." << std::endl;
            return -1;
        }
    }
    strncpy(newDir.file_name, dirname.c_str(), 56);
    int dirIndex;
    for (int i = 0; i < (BLOCK_SIZE / 64) + 1; i++) {
        if (i == BLOCK_SIZE / 64) {
            std::cout << "Directory full, cannot create sub-directory." << std::endl;
            return -1;
        }
        if (strlen(curDir[i].file_name) == 0 && curDir[i].first_blk == 0) {
            dirIndex = i;
            break;
        }
    }
  
    newDir.size = BLOCK_SIZE;
    newDir.access_rights = READ | WRITE | EXECUTE;
    newDir.type = TYPE_DIR;
    newDir.first_blk = this->firstFreeBlk();
    if (newDir.first_blk == -1) {
        std::cout << "No free blocks." << std::endl;
        return -1;
    }
    dir_entry directory[BLOCK_SIZE / 64];
    for (int i = 0; i < BLOCK_SIZE / 64; i++) { //initiate every dir_entry in directory
        directory[i].access_rights = 0;
        directory[i].first_blk = 0;
        directory[i].size = 0;
        directory[i].type = TYPE_FILE;
        directory[i].file_name[0] = '\0';
    }
    directory[0] = newDir; // first entry points to self
    directory[1] = curDir[0]; // second entry points to parent directory
    strncpy(directory[1].file_name, "..", 56);

    fat[newDir.first_blk] = FAT_EOF;
    disk.write(newDir.first_blk, (uint8_t*)directory);

    curDir[dirIndex] = newDir;
    disk.write(curDir[0].first_blk, (uint8_t*)curDir);
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    this->updateWorkingDir();
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    std::string path = dirpath;
    dir_entry curDir[64];
    int curDirBlk = this->findTargetDir(path);
    //std::cout << path << std::endl;
    //std::cout << curDirBlk << std::endl;
    if (curDirBlk == -1) {
        std::cout << "Invalid path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDir);
    int nameInd = path.find_last_of('/');
    std::string dirname;
    if (nameInd == std::string::npos) { // path has no '/'
        dirname = path;
    }
    else if ((nameInd == 0) && (path.length() == 1)) // path is "/"
    {
        dir_entry directory[BLOCK_SIZE / 64];
        disk.read(ROOT_BLOCK, (uint8_t*)directory);
        memcpy(workingDir, directory, BLOCK_SIZE);
        return 0;
    }
    else {
        dirname = path.substr(nameInd);
        dirname.erase(0, 1);
    }

    if (dirname.empty()) {
        std::cout << "Directory name must not be empty." << std::endl;
        return -1;
    }
    int inDir = 0;
    int index;
    for (int i = 1; i < BLOCK_SIZE / 64; i++) {
        if (strncmp(curDir[i].file_name, dirname.c_str(), 56) == 0) {
            inDir = 1;
            index = i;
            break;
        }
    }
    if (inDir == 0) {
        std::cout << "Directory could not be found." << std::endl;
        return -1;
    }
    if (curDir[index].type == TYPE_FILE) {
        std::cout << dirpath << " is not a directory." << std::endl;
        return -1;
    }
    if (!(curDir[index].access_rights & READ)) {
        std::cout << "Insufficient access rights." << std::endl;
        return -1;
    }
    dir_entry directory[BLOCK_SIZE / 64];
    disk.read(curDir[index].first_blk, (uint8_t*)directory);
    memcpy(workingDir, directory, BLOCK_SIZE);
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    if (workingDir[0].first_blk == 0) {
        std::cout << "/" << std::endl;
        return 0;
    }
    std::string path = "";
    dir_entry curDir[BLOCK_SIZE / 64];
    memcpy(curDir, workingDir, BLOCK_SIZE);
    while (curDir[0].first_blk != 0) {
        path.insert(0, curDir[0].file_name);
        path.insert(0, "/");
        disk.read(curDir[1].first_blk, (uint8_t*)curDir);
    }
    std::cout << path << std::endl;
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    std::string path = filepath;
    dir_entry curDir[64];
    int curDirBlk = this->findTargetDir(path);
    if (curDirBlk == -1) {
        std::cout << "Invalid path." << std::endl;
        return -1;
    }
    disk.read(curDirBlk, (uint8_t*)curDir);
    int nameInd = path.find_last_of('/');
    std::string filename;
    if (nameInd == std::string::npos) {
        filename = path;
    }
    else {
        filename = path.substr(nameInd);
        filename.erase(0, 1);
    }

    if (filename.empty()) {
        std::cout << "File name must not be empty." << std::endl;
        return -1;
    }
    int inDir = 0;
    int index;
    for (int i = 1; i < BLOCK_SIZE / 64; i++) {
        if (strncmp(curDir[i].file_name, filename.c_str(), 56) == 0) {
            inDir = 1;
            index = i;
            break;
        }
    }
    if (inDir == 0) {
        std::cout << "File could not be found." << std::endl;
        return -1;
    }
    int rights = stoi(accessrights);
    if (rights > 7 || rights < 0) {
        std::cout << "Invalid access rights argument." << std::endl;
        return -1;
    }
    curDir[index].access_rights = 0 | rights;
    disk.write(curDir[0].first_blk, (uint8_t*)curDir);
    if (curDir[index].type == TYPE_DIR) {
        int blk = curDir[index].first_blk;
        disk.read(blk, (uint8_t*)curDir);
        for (int i = 0; i < 64; i++) {
            if (curDir[i].first_blk == blk) {
                curDir[i].access_rights == 0 | rights;
            }
        }
        disk.write(curDir[0].first_blk, (uint8_t*)curDir);
    }
    this->updateWorkingDir();

    return 0;
}
