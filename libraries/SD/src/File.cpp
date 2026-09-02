/*

  SD - a slightly more friendly wrapper for sdfatlib

  This library aims to expose a subset of SD card functionality
  in the form of a higher level "wrapper" object.

  License: GNU General Public License V3
          (Because sdfatlib is licensed with this.)

  (C) Copyright 2010 SparkFun Electronics

*/

#include <SD.h>
#include "utility/SdLock.h"

/* for debugging file open/close leaks
   uint8_t nfilecount=0;
*/

File::File(SdFile f, const char *n) {
  // oh man you are kidding me, new() doesn't exist? Ok we do it by hand!
  _file = (SdFile *)malloc(sizeof(SdFile));
  if (_file) {
    memcpy(_file, &f, sizeof(SdFile));

    strncpy(_name, n, 12);
    _name[12] = 0;

    /* for debugging file open/close leaks
       nfilecount++;
       Serial.print("Created \"");
       Serial.print(n);
       Serial.print("\": ");
       Serial.println(nfilecount, DEC);
    */
  }
}

File::File(void) {
  _file = 0;
  _name[0] = 0;
  //Serial.print("Created empty file object");
}

// returns a pointer to the file name
char *File::name(void) {
  return _name;
}

// a directory is a special type of file
bool File::isDirectory(void) {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return false;
  return (_file && _file->isDir());
}


size_t File::write(uint8_t val) {
  return write(&val, 1);
}

size_t File::write(const uint8_t *buf, size_t size) {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) {
    setWriteError();
    return 0;
  }
  size_t t;
  if (!_file) {
    setWriteError();
    return 0;
  }
  _file->clearWriteError();
  t = _file->write(buf, size);
  if (_file->getWriteError()) {
    setWriteError();
    return 0;
  }
  return t;
}

int File::availableForWrite() {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return 0;
  if (_file) {
    return _file->availableForWrite();
  }
  return 0;
}

int File::peek() {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return 0;
  if (! _file) {
    return 0;
  }

  int c = _file->read();
  if (c != -1) {
    _file->seekCur(-1);
  }
  return c;
}

int File::read() {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return -1;
  if (_file) {
    return _file->read();
  }
  return -1;
}

// buffered read for more efficient, high speed reading
int File::read(void *buf, uint16_t nbyte) {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return 0;
  if (_file) {
    return _file->read(buf, nbyte);
  }
  return 0;
}

int File::available() {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return 0;
  if (! _file) {
    return 0;
  }

  uint32_t n = _file->fileSize() - _file->curPosition();

  return n > 0X7FFF ? 0X7FFF : n;
}

void File::flush() {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return;
  if (_file) {
    _file->sync();
  }
}

bool File::seek(uint32_t pos) {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return false;
  if (! _file) {
    return false;
  }

  return _file->seekSet(pos);
}

uint32_t File::position() {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return static_cast<uint32_t>(-1);
  if (! _file) {
    return -1;
  }
  return _file->curPosition();
}

uint32_t File::size() {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return 0;
  if (! _file) {
    return 0;
  }
  return _file->fileSize();
}

void File::close() {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return;
  if (_file) {
    _file->close();
    free(_file);
    _file = 0;

    /* for debugging file open/close leaks
      nfilecount--;
      Serial.print("Deleted ");
      Serial.println(nfilecount, DEC);
    */
  }
}

File::operator bool() {
  SDLib::detail::FileSystemLock lock;
  if (!lock.locked()) return false;
  if (_file) {
    return  _file->isOpen();
  }
  return false;
}

