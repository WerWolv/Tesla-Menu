#pragma once

#include <switch.h>

class FsDirIterator {
  private:
    FsDir m_dir;
    FsDirectoryEntry entry;
    s64 count;

  public:
    FsDirIterator() = default;
    FsDirIterator(FsDir dir);

    ~FsDirIterator() = default;

    const FsDirectoryEntry &operator*() const;
    const FsDirectoryEntry *operator->() const;
    FsDirIterator &operator++();

    bool operator!=(const FsDirIterator &rhs);
};

inline FsDirIterator begin(FsDirIterator iter) noexcept { return iter; }

inline FsDirIterator end(FsDirIterator) noexcept { return FsDirIterator(); }
