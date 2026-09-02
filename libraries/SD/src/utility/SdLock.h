#pragma once

namespace SDLib {
namespace detail {

bool lockFileSystem();
void unlockFileSystem();

class FileSystemLock {
 public:
  FileSystemLock() : _locked(lockFileSystem()) {}
  ~FileSystemLock() {
    if (_locked) {
      unlockFileSystem();
    }
  }

  FileSystemLock(const FileSystemLock &) = delete;
  FileSystemLock &operator=(const FileSystemLock &) = delete;

  bool locked() const { return _locked; }

 private:
  bool _locked;
};

}  // namespace detail
}  // namespace SDLib
