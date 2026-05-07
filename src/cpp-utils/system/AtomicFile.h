#pragma once
#ifndef MESSMER_CPPUTILS_SYSTEM_ATOMICFILE_H_
#define MESSMER_CPPUTILS_SYSTEM_ATOMICFILE_H_

#include <boost/filesystem/path.hpp>
#include <cstddef>

namespace cpputils {

void storeFileAtomically(const boost::filesystem::path &filepath, const void *data, size_t size);
bool storeFileAtomicallyIfAbsent(const boost::filesystem::path &filepath, const void *data, size_t size);
bool isAtomicFileTemporaryFileFor(const boost::filesystem::path &filepath, const boost::filesystem::path &candidatePath);
bool createDirectoryDurably(const boost::filesystem::path &directory);
bool createDirectoryTreeDurably(const boost::filesystem::path &directory);
bool removeFileDurably(const boost::filesystem::path &filepath);
bool removeDirectoryIfEmptyDurably(const boost::filesystem::path &directory);
void syncDirectory(const boost::filesystem::path &directory);

}

#endif
