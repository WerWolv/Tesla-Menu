#include "dir_iterator.hpp"

FsDirIterator::FsDirIterator(FsDir dir) : m_dir(dir) {
    if (R_FAILED(fsDirRead(&this->m_dir, &this->count, 1, &this->entry)))
        this->count = 0;
}

const FsDirectoryEntry &FsDirIterator::operator*() const {
    return this->entry;
}

const FsDirectoryEntry *FsDirIterator::operator->() const {
    return &**this;
}

FsDirIterator &FsDirIterator::operator++() {
    if (R_FAILED(fsDirRead(&this->m_dir, &this->count, 1, &this->entry)))
        this->count = 0;
    return *this;
}

bool FsDirIterator::operator!=(const FsDirIterator &__rhs) {
    return this->count > 0;
}
