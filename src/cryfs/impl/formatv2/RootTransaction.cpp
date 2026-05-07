#include "RootTransaction.h"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace cryfs {
namespace formatv2 {
namespace {

constexpr uint64_t INITIAL_OBJECT_GENERATION = 1;

using DirectoryPathKey = std::vector<std::string>;

struct DirectoryEdit final {
  DirectoryPathKey path;
  DirectoryMetadata metadata;
  std::vector<DirectoryEntry> entries;
};

DirectoryRecord directoryRecordWithUpdatedChild(
  const DirectoryRecord &directory,
  uint64_t generation,
  DirectoryMetadata metadata,
  std::vector<DirectoryEntry> entries);

void validateSelectedBaseRoot(const RootOpenWithValidatedTreeResult &baseRoot) {
  if (baseRoot.status != RootOpenStatus::Selected
      || baseRoot.selectedRoot == boost::none
      || baseRoot.rootContent == boost::none
      || baseRoot.rootDirectory == boost::none) {
    throw std::runtime_error("Cannot build a format-v2 transaction without a selected base root");
  }

  const boost::optional<RootDirectoryRef> rootDirectoryRef =
    trustedRootDirectoryFromContent(
      *baseRoot.rootContent,
      baseRoot.rootDirectory->filesystemId,
      *baseRoot.selectedRoot);
  if (rootDirectoryRef == boost::none
      || !directoryRecordMatchesObject(
        *baseRoot.rootDirectory,
        baseRoot.rootContent->filesystemId,
        rootDirectoryRef->objectId,
        rootDirectoryRef->generation)) {
    throw std::runtime_error("Cannot build a format-v2 transaction from an inconsistent base root");
  }
}

void validateNextRootId(const RootOpenWithValidatedTreeResult &baseRoot, const RootId &nextRootId) {
  if (nextRootId == RootId::Null()) {
    throw std::runtime_error("Cannot build a format-v2 transaction with a null root id");
  }
  if (nextRootId == baseRoot.selectedRoot->rootId) {
    throw std::runtime_error("Cannot build a format-v2 transaction by reusing the base root id");
  }
}

uint64_t incrementGeneration(uint64_t generation, const char *description) {
  if (generation == std::numeric_limits<uint64_t>::max()) {
    throw std::runtime_error(std::string("Cannot build a format-v2 transaction with exhausted ") + description);
  }
  return generation + 1;
}

std::vector<DirectoryEntry> canonicalEntries(std::vector<DirectoryEntry> entries) {
  std::sort(
    entries.begin(),
    entries.end(),
    [] (const DirectoryEntry &lhs, const DirectoryEntry &rhs) {
      return lhs.name < rhs.name;
    });
  return entries;
}

const DirectoryEntry *findEntryByName(
  const std::vector<DirectoryEntry> &entries,
  const std::string &name) {
  for (const DirectoryEntry &entry: entries) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

DirectoryEntry childEntry(
  std::string childName,
  ObjectType childType,
  const ObjectId &childObjectId,
  uint64_t childGeneration) {
  return DirectoryEntry{
    std::move(childName),
    childType,
    childObjectId,
    childGeneration
  };
}

std::vector<DirectoryEntry> entriesWithChild(
  std::vector<DirectoryEntry> entries,
  std::string childName,
  ObjectType childType,
  const ObjectId &childObjectId,
  uint64_t childGeneration) {
  entries.push_back(childEntry(
    std::move(childName),
    childType,
    childObjectId,
    childGeneration));
  return entries;
}

std::vector<DirectoryEntry> entriesWithUpdatedDirectoryChild(
  const DirectoryRecord &parent,
  const std::string &childName,
  const ObjectId &childDirectoryId,
  uint64_t childGeneration) {
  std::vector<DirectoryEntry> entries = parent.entries;
  for (DirectoryEntry &entry: entries) {
    if (entry.name == childName) {
      if (entry.type != ObjectType::Directory || entry.objectId != childDirectoryId) {
        throw std::runtime_error("Cannot build a format-v2 transaction from an inconsistent directory path");
      }
      entry.generation = childGeneration;
      return entries;
    }
  }
  throw std::runtime_error("Cannot build a format-v2 transaction with a missing directory path component");
}

std::vector<DirectoryEntry> entriesWithUpdatedExistingChild(
  const DirectoryRecord &parent,
  const std::string &childName,
  ObjectType childType,
  const ObjectId &childObjectId,
  uint64_t expectedChildGeneration,
  uint64_t nextChildGeneration) {
  std::vector<DirectoryEntry> entries = parent.entries;
  for (DirectoryEntry &entry: entries) {
    if (entry.name == childName) {
      if (entry.type != childType
          || entry.objectId != childObjectId
          || entry.generation != expectedChildGeneration) {
        throw std::runtime_error("Cannot build a format-v2 transaction from an inconsistent directory path");
      }
      entry.generation = nextChildGeneration;
      return entries;
    }
  }
  throw std::runtime_error("Cannot build a format-v2 transaction with a missing directory path component");
}

std::vector<DirectoryEntry> entriesWithoutChildReference(
  std::vector<DirectoryEntry> entries,
  const DirectoryChildReference &child) {
  for (auto it = entries.begin(); it != entries.end(); ++it) {
    if (it->name == child.name) {
      if (it->type != child.type
          || it->objectId != child.objectId
          || it->generation != child.generation) {
        throw std::runtime_error("Cannot build a format-v2 transaction from an inconsistent directory path");
      }
      entries.erase(it);
      return entries;
    }
  }
  throw std::runtime_error("Cannot build a format-v2 transaction with a missing directory child");
}

std::vector<DirectoryEntry> entriesWithoutExistingChild(
  const DirectoryRecord &parent,
  const DirectoryChildReference &child) {
  return entriesWithoutChildReference(parent.entries, child);
}

std::vector<DirectoryEntry> entriesWithMovedChild(
  std::vector<DirectoryEntry> entries,
  std::string targetName,
  const DirectoryChildReference &child) {
  if (findEntryByName(entries, targetName) != nullptr) {
    throw std::runtime_error("Cannot build a non-overwriting format-v2 move transaction onto an existing child");
  }
  return entriesWithChild(
    std::move(entries),
    std::move(targetName),
    child.type,
    child.objectId,
    child.generation);
}

std::vector<DirectoryEntry> entriesWithMovedChildReplacing(
  std::vector<DirectoryEntry> entries,
  std::string targetName,
  const DirectoryChildReference &sourceChild,
  const DirectoryChildReference &targetChild) {
  if (targetChild.name != targetName) {
    throw std::runtime_error("Cannot build a format-v2 replace-move transaction with mismatched target names");
  }

  entries = entriesWithoutChildReference(std::move(entries), targetChild);
  return entriesWithMovedChild(
    std::move(entries),
    std::move(targetName),
    sourceChild);
}

std::vector<DirectoryEntry> entriesWithRenamedExistingChild(
  const DirectoryRecord &parent,
  const DirectoryChildReference &child,
  std::string targetName) {
  return entriesWithMovedChild(
    entriesWithoutExistingChild(parent, child),
    std::move(targetName),
    child);
}

void validateTransaction(const RootDirectoryTransaction &transaction) {
  (void)serializeRootRecord(transaction.rootRecord);
  (void)serializeRootContent(transaction.rootContent);
  (void)serializeDirectoryRecord(transaction.rootDirectory);
}

void validateChildObjectId(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const ObjectId &childObjectId,
  const char *description) {
  if (childObjectId == ObjectId::Null()) {
    throw std::runtime_error(std::string("Cannot build a format-v2 transaction with a null child ") + description + " id");
  }
  if (childObjectId == baseRoot.rootDirectory->directoryId) {
    throw std::runtime_error(std::string("Cannot build a format-v2 transaction reusing the root directory id for a child ") + description);
  }
}

void validateDistinctChildObjectIds(
  const ObjectId &lhs,
  const char *lhsDescription,
  const ObjectId &rhs,
  const char *rhsDescription) {
  if (lhs == rhs) {
    throw std::runtime_error(
      std::string("Cannot build a format-v2 transaction reusing the child ")
      + lhsDescription
      + " id as the child "
      + rhsDescription
      + " id");
  }
}

void validateDirectoryChildReference(const DirectoryChildReference &child) {
  if (child.objectId == ObjectId::Null()) {
    throw std::runtime_error("Cannot build a format-v2 transaction with a null child object id");
  }
  if (child.generation == 0) {
    throw std::runtime_error("Cannot build a format-v2 transaction with a zero child generation");
  }
  (void)childEntry(child.name, child.type, child.objectId, child.generation);
}

void validateMoveDoesNotCreateDirectoryCycle(
  const DirectoryChildReference &sourceChild,
  const std::vector<DirectoryPathSegment> &targetParentPath) {
  if (sourceChild.type != ObjectType::Directory) {
    return;
  }

  for (const DirectoryPathSegment &segment: targetParentPath) {
    if (segment.directory.directoryId == sourceChild.objectId) {
      throw std::runtime_error("Cannot build a format-v2 move transaction into the moved directory subtree");
    }
  }
}

void validateMoveReplacement(
  const DirectoryChildReference &sourceChild,
  const DirectoryChildReference &targetChild) {
  validateDirectoryChildReference(targetChild);

  const bool sourceIsDirectory = sourceChild.type == ObjectType::Directory;
  const bool targetIsDirectory = targetChild.type == ObjectType::Directory;
  if (sourceIsDirectory != targetIsDirectory) {
    throw std::runtime_error("Cannot build a format-v2 replace-move transaction across directory and non-directory types");
  }
  if (sourceChild.name == targetChild.name
      && sourceChild.type == targetChild.type
      && sourceChild.objectId == targetChild.objectId
      && sourceChild.generation == targetChild.generation) {
    throw std::runtime_error("Cannot build a format-v2 replace-move transaction replacing a child with itself");
  }
}

void validateDirectoryPathSegment(
  const DirectoryRecord &parent,
  const DirectoryPathSegment &segment) {
  const DirectoryEntry *entry = findEntryByName(parent.entries, segment.name);
  if (entry == nullptr) {
    throw std::runtime_error("Cannot build a format-v2 transaction with a missing directory path component");
  }
  if (entry->type != ObjectType::Directory) {
    throw std::runtime_error("Cannot build a format-v2 transaction through a non-directory path component");
  }
  if (!directoryRecordMatchesObject(
        segment.directory,
        parent.filesystemId,
        entry->objectId,
        entry->generation)) {
    throw std::runtime_error("Cannot build a format-v2 transaction from an inconsistent directory path");
  }
  (void)serializeDirectoryRecord(segment.directory);
}

void validateParentPath(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const std::vector<DirectoryPathSegment> &parentPath) {
  if (parentPath.empty()) {
    throw std::runtime_error("Cannot build a nested format-v2 transaction without a parent directory path");
  }

  const DirectoryRecord *parent = &*baseRoot.rootDirectory;
  for (const DirectoryPathSegment &segment: parentPath) {
    validateDirectoryPathSegment(*parent, segment);
    parent = &segment.directory;
  }
}

void validateDirectoryPath(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const std::vector<DirectoryPathSegment> &path) {
  if (path.empty()) {
    return;
  }
  validateParentPath(baseRoot, path);
}

DirectoryPathKey pathKeyFromSegments(
  const std::vector<DirectoryPathSegment> &path) {
  DirectoryPathKey key;
  key.reserve(path.size());
  for (const DirectoryPathSegment &segment: path) {
    key.push_back(segment.name);
  }
  return key;
}

void collectOriginalDirectories(
  const std::vector<DirectoryPathSegment> &path,
  std::map<DirectoryPathKey, DirectoryRecord> *directories) {
  DirectoryPathKey key;
  for (const DirectoryPathSegment &segment: path) {
    key.push_back(segment.name);
    directories->emplace(key, segment.directory);
  }
}

const DirectoryRecord &directoryAtPath(
  const std::map<DirectoryPathKey, DirectoryRecord> &directories,
  const DirectoryPathKey &path) {
  const auto found = directories.find(path);
  if (found == directories.end()) {
    throw std::runtime_error("Cannot build a format-v2 transaction with an unknown directory path");
  }
  return found->second;
}

DirectoryPathKey parentPathKey(DirectoryPathKey path) {
  if (path.empty()) {
    throw std::runtime_error("Cannot get the parent of the format-v2 root directory path");
  }
  path.pop_back();
  return path;
}

std::vector<DirectoryPathKey> keysAtDepth(
  const std::map<DirectoryPathKey, DirectoryRecord> &directories,
  size_t depth) {
  std::vector<DirectoryPathKey> keys;
  for (const auto &entry: directories) {
    if (entry.first.size() == depth) {
      keys.push_back(entry.first);
    }
  }
  return keys;
}

size_t maxDirectoryPathDepth(
  const std::map<DirectoryPathKey, DirectoryRecord> &directories) {
  size_t result = 0;
  for (const auto &entry: directories) {
    result = std::max(result, entry.first.size());
  }
  return result;
}

std::vector<DirectoryRecord> stagedNonRootDirectories(
  const std::map<DirectoryPathKey, DirectoryRecord> &directories) {
  std::vector<DirectoryRecord> staged;
  for (size_t depth = maxDirectoryPathDepth(directories); depth != 0; --depth) {
    for (const DirectoryPathKey &key: keysAtDepth(directories, depth)) {
      staged.push_back(directoryAtPath(directories, key));
    }
  }
  return staged;
}

DirectoryPathTransaction buildDirectoryEditTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  const std::vector<DirectoryPathSegment> &firstPath,
  const std::vector<DirectoryPathSegment> &secondPath,
  const std::vector<DirectoryEdit> &edits) {
  if (edits.empty()) {
    throw std::runtime_error("Cannot build a format-v2 transaction without directory edits");
  }

  std::map<DirectoryPathKey, DirectoryRecord> originals;
  originals.emplace(DirectoryPathKey{}, *baseRoot.rootDirectory);
  collectOriginalDirectories(firstPath, &originals);
  collectOriginalDirectories(secondPath, &originals);

  std::map<DirectoryPathKey, DirectoryRecord> updated;
  for (const DirectoryEdit &edit: edits) {
    const DirectoryRecord &original = directoryAtPath(originals, edit.path);
    if (updated.count(edit.path) != 0) {
      throw std::runtime_error("Cannot build a format-v2 transaction with duplicate directory edits");
    }
    updated.emplace(
      edit.path,
      directoryRecordWithUpdatedChild(
        original,
        incrementGeneration(original.generation, "directory generations"),
        edit.metadata,
        edit.entries));
  }

  for (size_t depth = maxDirectoryPathDepth(updated); depth != 0; --depth) {
    for (const DirectoryPathKey &childKey: keysAtDepth(updated, depth)) {
      const DirectoryRecord child = directoryAtPath(updated, childKey);
      const std::string childName = childKey.back();
      const DirectoryPathKey parentKey = parentPathKey(childKey);

      auto updatedParent = updated.find(parentKey);
      if (updatedParent == updated.end()) {
        const DirectoryRecord &originalParent = directoryAtPath(originals, parentKey);
        updated.emplace(
          parentKey,
          directoryRecordWithUpdatedChild(
            originalParent,
            incrementGeneration(originalParent.generation, "ancestor directory generations"),
            originalParent.metadata,
            entriesWithUpdatedDirectoryChild(
              originalParent,
              childName,
              child.directoryId,
              child.generation)));
      } else {
        updatedParent->second = directoryRecordWithUpdatedChild(
          updatedParent->second,
          updatedParent->second.generation,
          updatedParent->second.metadata,
          entriesWithUpdatedDirectoryChild(
            updatedParent->second,
            childName,
            child.directoryId,
            child.generation));
      }
    }
  }

  const DirectoryPathKey rootKey;
  const DirectoryRecord &updatedRoot = directoryAtPath(updated, rootKey);
  return DirectoryPathTransaction{
    stagedNonRootDirectories(updated),
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      updatedRoot.metadata,
      updatedRoot.entries)
  };
}

void validateChildObjectIdNotInParentPath(
  const std::vector<DirectoryPathSegment> &parentPath,
  const ObjectId &childObjectId,
  const char *description) {
  for (const DirectoryPathSegment &segment: parentPath) {
    if (childObjectId == segment.directory.directoryId) {
      throw std::runtime_error(std::string("Cannot build a format-v2 transaction reusing a parent directory id for a child ") + description);
    }
  }
}

DirectoryRecord emptyChildDirectoryRecord(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const ObjectId &childDirectoryId,
  DirectoryMetadata metadata) {
  return DirectoryRecord{
    baseRoot.rootDirectory->filesystemId,
    childDirectoryId,
    INITIAL_OBJECT_GENERATION,
    std::move(metadata),
    {}
  };
}

FileRecord emptyChildFileRecord(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const ObjectId &childFileId,
  FileMetadata metadata) {
  return FileRecord{
    baseRoot.rootDirectory->filesystemId,
    childFileId,
    INITIAL_OBJECT_GENERATION,
    std::move(metadata),
    0,
    {}
  };
}

FileDataRecord childFileDataRecord(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const ObjectId &childFileDataId,
  cpputils::Data payload) {
  return FileDataRecord{
    baseRoot.rootDirectory->filesystemId,
    childFileDataId,
    INITIAL_OBJECT_GENERATION,
    std::move(payload)
  };
}

FileRecord childFileRecordWithData(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const ObjectId &childFileId,
  FileMetadata metadata,
  const FileDataRecord &childFileData) {
  const uint64_t payloadSize = static_cast<uint64_t>(childFileData.payload.size());
  return FileRecord{
    baseRoot.rootDirectory->filesystemId,
    childFileId,
    INITIAL_OBJECT_GENERATION,
    std::move(metadata),
    payloadSize,
    {
      FileExtent{
        0,
        payloadSize,
        childFileData.dataId,
        childFileData.generation
      }
    }
  };
}

SymlinkRecord childSymlinkRecord(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const ObjectId &childSymlinkId,
  SymlinkMetadata metadata,
  std::string target) {
  return SymlinkRecord{
    baseRoot.rootDirectory->filesystemId,
    childSymlinkId,
    INITIAL_OBJECT_GENERATION,
    std::move(metadata),
    std::move(target)
  };
}

std::vector<DirectoryEntry> rootEntriesWithChild(
  const RootOpenWithValidatedTreeResult &baseRoot,
  std::string childName,
  ObjectType childType,
  const ObjectId &childObjectId) {
  return entriesWithChild(
    baseRoot.rootDirectory->entries,
    std::move(childName),
    childType,
    childObjectId,
    INITIAL_OBJECT_GENERATION);
}

DirectoryRecord directoryRecordWithUpdatedChild(
  const DirectoryRecord &directory,
  uint64_t generation,
  DirectoryMetadata metadata,
  std::vector<DirectoryEntry> entries) {
  return DirectoryRecord{
    directory.filesystemId,
    directory.directoryId,
    generation,
    std::move(metadata),
    canonicalEntries(std::move(entries))
  };
}

DirectoryPathTransaction buildNestedChildPathTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  const std::vector<DirectoryPathSegment> &parentPath,
  std::string childName,
  ObjectType childType,
  const ObjectId &childObjectId,
  uint64_t childGeneration,
  DirectoryMetadata parentDirectoryMetadata) {
  const DirectoryRecord &parentDirectory = parentPath.back().directory;
  DirectoryRecord updatedDirectory = directoryRecordWithUpdatedChild(
    parentDirectory,
    incrementGeneration(parentDirectory.generation, "nested directory generations"),
    std::move(parentDirectoryMetadata),
    entriesWithChild(
      parentDirectory.entries,
      std::move(childName),
      childType,
      childObjectId,
      childGeneration));

  std::vector<DirectoryRecord> stagedDirectories;
  stagedDirectories.push_back(updatedDirectory);

  for (size_t index = parentPath.size() - 1; index != 0; --index) {
    const DirectoryPathSegment &ancestor = parentPath[index - 1];
    updatedDirectory = directoryRecordWithUpdatedChild(
      ancestor.directory,
      incrementGeneration(ancestor.directory.generation, "nested ancestor directory generations"),
      ancestor.directory.metadata,
      entriesWithUpdatedDirectoryChild(
        ancestor.directory,
        parentPath[index].name,
        updatedDirectory.directoryId,
        updatedDirectory.generation));
    stagedDirectories.push_back(updatedDirectory);
  }

  const std::vector<DirectoryEntry> updatedRootEntries =
    entriesWithUpdatedDirectoryChild(
      *baseRoot.rootDirectory,
      parentPath.front().name,
      updatedDirectory.directoryId,
      updatedDirectory.generation);

  return DirectoryPathTransaction{
    std::move(stagedDirectories),
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      baseRoot.rootDirectory->metadata,
      updatedRootEntries)
  };
}

DirectoryPathTransaction buildNestedExistingChildPathTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  const std::vector<DirectoryPathSegment> &parentPath,
  const std::string &childName,
  ObjectType childType,
  const ObjectId &childObjectId,
  uint64_t expectedChildGeneration,
  uint64_t nextChildGeneration) {
  const DirectoryRecord &parentDirectory = parentPath.back().directory;
  DirectoryRecord updatedDirectory = directoryRecordWithUpdatedChild(
    parentDirectory,
    incrementGeneration(parentDirectory.generation, "nested directory generations"),
    parentDirectory.metadata,
    entriesWithUpdatedExistingChild(
      parentDirectory,
      childName,
      childType,
      childObjectId,
      expectedChildGeneration,
      nextChildGeneration));

  std::vector<DirectoryRecord> stagedDirectories;
  stagedDirectories.push_back(updatedDirectory);

  for (size_t index = parentPath.size() - 1; index != 0; --index) {
    const DirectoryPathSegment &ancestor = parentPath[index - 1];
    updatedDirectory = directoryRecordWithUpdatedChild(
      ancestor.directory,
      incrementGeneration(ancestor.directory.generation, "nested ancestor directory generations"),
      ancestor.directory.metadata,
      entriesWithUpdatedDirectoryChild(
        ancestor.directory,
        parentPath[index].name,
        updatedDirectory.directoryId,
        updatedDirectory.generation));
    stagedDirectories.push_back(updatedDirectory);
  }

  const std::vector<DirectoryEntry> updatedRootEntries =
    entriesWithUpdatedDirectoryChild(
      *baseRoot.rootDirectory,
      parentPath.front().name,
      updatedDirectory.directoryId,
      updatedDirectory.generation);

  return DirectoryPathTransaction{
    std::move(stagedDirectories),
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      baseRoot.rootDirectory->metadata,
      updatedRootEntries)
  };
}

DirectoryPathTransaction buildNestedRemoveChildPathTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  const std::vector<DirectoryPathSegment> &parentPath,
  const DirectoryChildReference &child,
  DirectoryMetadata parentDirectoryMetadata) {
  const DirectoryRecord &parentDirectory = parentPath.back().directory;
  DirectoryRecord updatedDirectory = directoryRecordWithUpdatedChild(
    parentDirectory,
    incrementGeneration(parentDirectory.generation, "nested directory generations"),
    std::move(parentDirectoryMetadata),
    entriesWithoutExistingChild(parentDirectory, child));

  std::vector<DirectoryRecord> stagedDirectories;
  stagedDirectories.push_back(updatedDirectory);

  for (size_t index = parentPath.size() - 1; index != 0; --index) {
    const DirectoryPathSegment &ancestor = parentPath[index - 1];
    updatedDirectory = directoryRecordWithUpdatedChild(
      ancestor.directory,
      incrementGeneration(ancestor.directory.generation, "nested ancestor directory generations"),
      ancestor.directory.metadata,
      entriesWithUpdatedDirectoryChild(
        ancestor.directory,
        parentPath[index].name,
        updatedDirectory.directoryId,
        updatedDirectory.generation));
    stagedDirectories.push_back(updatedDirectory);
  }

  const std::vector<DirectoryEntry> updatedRootEntries =
    entriesWithUpdatedDirectoryChild(
      *baseRoot.rootDirectory,
      parentPath.front().name,
      updatedDirectory.directoryId,
      updatedDirectory.generation);

  return DirectoryPathTransaction{
    std::move(stagedDirectories),
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      baseRoot.rootDirectory->metadata,
      updatedRootEntries)
  };
}

DirectoryPathTransaction buildMoveChildPathTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  const std::vector<DirectoryPathSegment> &sourceParentPath,
  const DirectoryChildReference &sourceChild,
  const std::vector<DirectoryPathSegment> &targetParentPath,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  const DirectoryPathKey sourcePathKey = pathKeyFromSegments(sourceParentPath);
  const DirectoryPathKey targetPathKey = pathKeyFromSegments(targetParentPath);
  const DirectoryRecord &sourceParent = sourceParentPath.empty()
      ? *baseRoot.rootDirectory
      : sourceParentPath.back().directory;
  const DirectoryRecord &targetParent = targetParentPath.empty()
      ? *baseRoot.rootDirectory
      : targetParentPath.back().directory;

  if (sourcePathKey == targetPathKey) {
    return buildDirectoryEditTransaction(
      baseRoot,
      nextRootId,
      sourceParentPath,
      targetParentPath,
      {
        DirectoryEdit{
          sourcePathKey,
          std::move(targetParentDirectoryMetadata),
          entriesWithRenamedExistingChild(
            sourceParent,
            sourceChild,
            std::move(targetChildName))
        }
      });
  }

  return buildDirectoryEditTransaction(
    baseRoot,
    nextRootId,
    sourceParentPath,
    targetParentPath,
    {
      DirectoryEdit{
        sourcePathKey,
        std::move(sourceParentDirectoryMetadata),
        entriesWithoutExistingChild(sourceParent, sourceChild)
      },
      DirectoryEdit{
        targetPathKey,
        std::move(targetParentDirectoryMetadata),
        entriesWithMovedChild(
          targetParent.entries,
          std::move(targetChildName),
          sourceChild)
      }
    });
}

DirectoryPathTransaction buildMoveChildReplacingPathTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  const std::vector<DirectoryPathSegment> &sourceParentPath,
  const DirectoryChildReference &sourceChild,
  const std::vector<DirectoryPathSegment> &targetParentPath,
  const DirectoryChildReference &targetChild,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  const DirectoryPathKey sourcePathKey = pathKeyFromSegments(sourceParentPath);
  const DirectoryPathKey targetPathKey = pathKeyFromSegments(targetParentPath);
  const DirectoryRecord &sourceParent = sourceParentPath.empty()
      ? *baseRoot.rootDirectory
      : sourceParentPath.back().directory;
  const DirectoryRecord &targetParent = targetParentPath.empty()
      ? *baseRoot.rootDirectory
      : targetParentPath.back().directory;

  if (sourcePathKey == targetPathKey) {
    std::vector<DirectoryEntry> entries =
      entriesWithoutChildReference(sourceParent.entries, sourceChild);
    entries = entriesWithMovedChildReplacing(
      std::move(entries),
      std::move(targetChildName),
      sourceChild,
      targetChild);
    return buildDirectoryEditTransaction(
      baseRoot,
      nextRootId,
      sourceParentPath,
      targetParentPath,
      {
        DirectoryEdit{
          sourcePathKey,
          std::move(targetParentDirectoryMetadata),
          std::move(entries)
        }
      });
  }

  return buildDirectoryEditTransaction(
    baseRoot,
    nextRootId,
    sourceParentPath,
    targetParentPath,
    {
      DirectoryEdit{
        sourcePathKey,
        std::move(sourceParentDirectoryMetadata),
        entriesWithoutExistingChild(sourceParent, sourceChild)
      },
      DirectoryEdit{
        targetPathKey,
        std::move(targetParentDirectoryMetadata),
        entriesWithMovedChildReplacing(
          targetParent.entries,
          std::move(targetChildName),
          sourceChild,
          targetChild)
      }
    });
}

void validateFileUpdateData(const FileRecord &baseFile, const FileUpdate &update) {
  for (const FileDataRecord &data: update.fileData) {
    if (data.filesystemId != baseFile.filesystemId) {
      throw std::runtime_error("Cannot build a format-v2 file update with wrong-filesystem file data");
    }
    if (data.dataId == baseFile.fileId) {
      throw std::runtime_error("Cannot build a format-v2 file update reusing the file id as a file data id");
    }
    (void)serializeFileDataRecord(data);
  }

  for (const FileExtent &extent: update.extents) {
    for (const FileDataRecord &data: update.fileData) {
      if (extent.dataId == data.dataId && extent.generation == data.generation) {
        if (extent.size != data.payload.size()) {
          throw std::runtime_error("Cannot build a format-v2 file update with wrong-sized staged file data");
        }
        break;
      }
    }
  }
}

FileRecord updatedFileRecord(const FileUpdate &update) {
  (void)serializeFileRecord(update.baseFile);
  validateFileUpdateData(update.baseFile, update);

  FileRecord file{
    update.baseFile.filesystemId,
    update.baseFile.fileId,
    incrementGeneration(update.baseFile.generation, "file generations"),
    update.metadata,
    update.size,
    update.extents
  };
  (void)serializeFileRecord(file);
  return file;
}

SymlinkRecord updatedSymlinkRecord(const SymlinkUpdate &update) {
  (void)serializeSymlinkRecord(update.baseSymlink);

  SymlinkRecord symlink{
    update.baseSymlink.filesystemId,
    update.baseSymlink.symlinkId,
    incrementGeneration(update.baseSymlink.generation, "symlink generations"),
    update.metadata,
    update.target
  };
  (void)serializeSymlinkRecord(symlink);
  return symlink;
}

void validateCreateRootDirectoryChildTransaction(
  const CreateRootDirectoryChildTransaction &transaction) {
  (void)serializeDirectoryRecord(transaction.childDirectory);
  validateTransaction(transaction.rootUpdate);
}

void validateCreateRootFileChildTransaction(
  const CreateRootFileChildTransaction &transaction) {
  (void)serializeFileRecord(transaction.childFile);
  validateTransaction(transaction.rootUpdate);
}

void validateCreateRootFileWithDataChildTransaction(
  const CreateRootFileWithDataChildTransaction &transaction) {
  (void)serializeFileDataRecord(transaction.childFileData);
  (void)serializeFileRecord(transaction.childFile);
  validateTransaction(transaction.rootUpdate);
}

void validateCreateRootSymlinkChildTransaction(
  const CreateRootSymlinkChildTransaction &transaction) {
  (void)serializeSymlinkRecord(transaction.childSymlink);
  validateTransaction(transaction.rootUpdate);
}

void validateDirectoryPathTransaction(const DirectoryPathTransaction &transaction) {
  for (const DirectoryRecord &directory: transaction.stagedDirectories) {
    (void)serializeDirectoryRecord(directory);
  }
  validateTransaction(transaction.rootUpdate);
}

void validateCreateNestedDirectoryChildTransaction(
  const CreateNestedDirectoryChildTransaction &transaction) {
  (void)serializeDirectoryRecord(transaction.childDirectory);
  validateDirectoryPathTransaction(transaction.pathUpdate);
}

void validateCreateNestedFileChildTransaction(
  const CreateNestedFileChildTransaction &transaction) {
  (void)serializeFileRecord(transaction.childFile);
  validateDirectoryPathTransaction(transaction.pathUpdate);
}

void validateCreateNestedFileWithDataChildTransaction(
  const CreateNestedFileWithDataChildTransaction &transaction) {
  (void)serializeFileDataRecord(transaction.childFileData);
  (void)serializeFileRecord(transaction.childFile);
  validateDirectoryPathTransaction(transaction.pathUpdate);
}

void validateCreateNestedSymlinkChildTransaction(
  const CreateNestedSymlinkChildTransaction &transaction) {
  (void)serializeSymlinkRecord(transaction.childSymlink);
  validateDirectoryPathTransaction(transaction.pathUpdate);
}

void validateUpdateRootFileChildTransaction(
  const UpdateRootFileChildTransaction &transaction) {
  for (const FileDataRecord &fileData: transaction.fileData) {
    (void)serializeFileDataRecord(fileData);
  }
  (void)serializeFileRecord(transaction.file);
  validateTransaction(transaction.rootUpdate);
}

void validateUpdateNestedFileChildTransaction(
  const UpdateNestedFileChildTransaction &transaction) {
  for (const FileDataRecord &fileData: transaction.fileData) {
    (void)serializeFileDataRecord(fileData);
  }
  (void)serializeFileRecord(transaction.file);
  validateDirectoryPathTransaction(transaction.pathUpdate);
}

void validateUpdateRootSymlinkChildTransaction(
  const UpdateRootSymlinkChildTransaction &transaction) {
  (void)serializeSymlinkRecord(transaction.symlink);
  validateTransaction(transaction.rootUpdate);
}

void validateUpdateNestedSymlinkChildTransaction(
  const UpdateNestedSymlinkChildTransaction &transaction) {
  (void)serializeSymlinkRecord(transaction.symlink);
  validateDirectoryPathTransaction(transaction.pathUpdate);
}

void validateRemoveRootChildTransaction(
  const RemoveRootChildTransaction &transaction) {
  validateTransaction(transaction.rootUpdate);
}

void validateRemoveNestedChildTransaction(
  const RemoveNestedChildTransaction &transaction) {
  validateDirectoryPathTransaction(transaction.pathUpdate);
}

void validateMoveChildTransaction(
  const MoveChildTransaction &transaction) {
  validateDirectoryPathTransaction(transaction.pathUpdate);
}

RootOpenWithValidatedTreeResult publishDirectoryPathTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const DirectoryPathTransaction &transaction) {
  for (const DirectoryRecord &directory: transaction.stagedDirectories) {
    directoryRecordStore.store(directory);
  }
  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.rootUpdate.rootRecord,
    transaction.rootUpdate.rootContent,
    transaction.rootUpdate.rootDirectory);
}

}

std::vector<DirectoryPathSegment> loadDirectoryPathFromRoot(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const DirectoryRecordStore &directoryRecordStore,
  const std::vector<std::string> &directoryPath) {
  validateSelectedBaseRoot(baseRoot);

  std::vector<DirectoryPathSegment> result;
  DirectoryRecord parent = *baseRoot.rootDirectory;
  for (const std::string &name: directoryPath) {
    const DirectoryEntry *entry = findEntryByName(parent.entries, name);
    if (entry == nullptr) {
      throw std::runtime_error("Cannot load format-v2 directory path with a missing component");
    }
    if (entry->type != ObjectType::Directory) {
      throw std::runtime_error("Cannot load format-v2 directory path through a non-directory component");
    }

    const boost::optional<DirectoryRecord> child =
      directoryRecordStore.load(entry->objectId, entry->generation);
    if (child == boost::none) {
      throw std::runtime_error("Cannot load format-v2 directory path with an unavailable directory record");
    }

    result.push_back(DirectoryPathSegment{name, *child});
    parent = *child;
  }
  return result;
}

RootDirectoryTransaction buildRootDirectoryTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  DirectoryMetadata rootDirectoryMetadata,
  std::vector<DirectoryEntry> rootDirectoryEntries) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);

  const uint64_t nextEpoch = incrementGeneration(baseRoot.selectedRoot->epoch, "root epochs");
  const uint64_t nextRootDirectoryGeneration =
    incrementGeneration(baseRoot.rootDirectory->generation, "root directory generations");

  RootDirectoryTransaction transaction{
    RootRecord{
      baseRoot.rootContent->filesystemId,
      nextEpoch,
      nextRootId
    },
    RootContent{
      baseRoot.rootContent->filesystemId,
      nextEpoch,
      nextRootId,
      baseRoot.rootDirectory->directoryId,
      nextRootDirectoryGeneration
    },
    DirectoryRecord{
      baseRoot.rootDirectory->filesystemId,
      baseRoot.rootDirectory->directoryId,
      nextRootDirectoryGeneration,
      std::move(rootDirectoryMetadata),
      canonicalEntries(std::move(rootDirectoryEntries))
    }
  };
  validateTransaction(transaction);
  return transaction;
}

CreateRootDirectoryChildTransaction buildCreateRootDirectoryChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childDirectoryId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateChildObjectId(baseRoot, childDirectoryId, "directory");

  CreateRootDirectoryChildTransaction transaction{
    emptyChildDirectoryRecord(
      baseRoot,
      childDirectoryId,
      std::move(childDirectoryMetadata)),
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      std::move(rootDirectoryMetadata),
      rootEntriesWithChild(
        baseRoot,
        std::move(childName),
        ObjectType::Directory,
        childDirectoryId))
  };
  validateCreateRootDirectoryChildTransaction(transaction);
  return transaction;
}

CreateRootFileChildTransaction buildCreateRootFileChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childFileId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateChildObjectId(baseRoot, childFileId, "file");

  CreateRootFileChildTransaction transaction{
    emptyChildFileRecord(
      baseRoot,
      childFileId,
      std::move(childFileMetadata)),
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      std::move(rootDirectoryMetadata),
      rootEntriesWithChild(
        baseRoot,
        std::move(childName),
        ObjectType::File,
        childFileId))
  };
  validateCreateRootFileChildTransaction(transaction);
  return transaction;
}

CreateRootFileWithDataChildTransaction buildCreateRootFileWithDataChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childFileId,
  ObjectId childFileDataId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateChildObjectId(baseRoot, childFileId, "file");
  validateChildObjectId(baseRoot, childFileDataId, "file data");
  validateDistinctChildObjectIds(childFileId, "file", childFileDataId, "file data");

  FileDataRecord childFileData =
    childFileDataRecord(baseRoot, childFileDataId, std::move(childFilePayload));
  CreateRootFileWithDataChildTransaction transaction{
    childFileRecordWithData(
      baseRoot,
      childFileId,
      std::move(childFileMetadata),
      childFileData),
    std::move(childFileData),
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      std::move(rootDirectoryMetadata),
      rootEntriesWithChild(
        baseRoot,
        std::move(childName),
        ObjectType::File,
        childFileId))
  };
  validateCreateRootFileWithDataChildTransaction(transaction);
  return transaction;
}

CreateRootSymlinkChildTransaction buildCreateRootSymlinkChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childSymlinkId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateChildObjectId(baseRoot, childSymlinkId, "symlink");

  CreateRootSymlinkChildTransaction transaction{
    childSymlinkRecord(
      baseRoot,
      childSymlinkId,
      std::move(childSymlinkMetadata),
      std::move(childSymlinkTarget)),
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      std::move(rootDirectoryMetadata),
      rootEntriesWithChild(
        baseRoot,
        std::move(childName),
        ObjectType::Symlink,
        childSymlinkId))
  };
  validateCreateRootSymlinkChildTransaction(transaction);
  return transaction;
}

CreateNestedDirectoryChildTransaction buildCreateNestedDirectoryChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childDirectoryId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateParentPath(baseRoot, parentPath);
  validateChildObjectId(baseRoot, childDirectoryId, "directory");
  validateChildObjectIdNotInParentPath(parentPath, childDirectoryId, "directory");

  CreateNestedDirectoryChildTransaction transaction{
    emptyChildDirectoryRecord(
      baseRoot,
      childDirectoryId,
      std::move(childDirectoryMetadata)),
    buildNestedChildPathTransaction(
      baseRoot,
      nextRootId,
      parentPath,
      std::move(childName),
      ObjectType::Directory,
      childDirectoryId,
      INITIAL_OBJECT_GENERATION,
      std::move(parentDirectoryMetadata))
  };
  validateCreateNestedDirectoryChildTransaction(transaction);
  return transaction;
}

CreateNestedFileChildTransaction buildCreateNestedFileChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childFileId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateParentPath(baseRoot, parentPath);
  validateChildObjectId(baseRoot, childFileId, "file");
  validateChildObjectIdNotInParentPath(parentPath, childFileId, "file");

  CreateNestedFileChildTransaction transaction{
    emptyChildFileRecord(
      baseRoot,
      childFileId,
      std::move(childFileMetadata)),
    buildNestedChildPathTransaction(
      baseRoot,
      nextRootId,
      parentPath,
      std::move(childName),
      ObjectType::File,
      childFileId,
      INITIAL_OBJECT_GENERATION,
      std::move(parentDirectoryMetadata))
  };
  validateCreateNestedFileChildTransaction(transaction);
  return transaction;
}

CreateNestedFileWithDataChildTransaction buildCreateNestedFileWithDataChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childFileId,
  ObjectId childFileDataId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateParentPath(baseRoot, parentPath);
  validateChildObjectId(baseRoot, childFileId, "file");
  validateChildObjectId(baseRoot, childFileDataId, "file data");
  validateChildObjectIdNotInParentPath(parentPath, childFileId, "file");
  validateChildObjectIdNotInParentPath(parentPath, childFileDataId, "file data");
  validateDistinctChildObjectIds(childFileId, "file", childFileDataId, "file data");

  FileDataRecord childFileData =
    childFileDataRecord(baseRoot, childFileDataId, std::move(childFilePayload));
  CreateNestedFileWithDataChildTransaction transaction{
    childFileRecordWithData(
      baseRoot,
      childFileId,
      std::move(childFileMetadata),
      childFileData),
    std::move(childFileData),
    buildNestedChildPathTransaction(
      baseRoot,
      nextRootId,
      parentPath,
      std::move(childName),
      ObjectType::File,
      childFileId,
      INITIAL_OBJECT_GENERATION,
      std::move(parentDirectoryMetadata))
  };
  validateCreateNestedFileWithDataChildTransaction(transaction);
  return transaction;
}

CreateNestedSymlinkChildTransaction buildCreateNestedSymlinkChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childSymlinkId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateParentPath(baseRoot, parentPath);
  validateChildObjectId(baseRoot, childSymlinkId, "symlink");
  validateChildObjectIdNotInParentPath(parentPath, childSymlinkId, "symlink");

  CreateNestedSymlinkChildTransaction transaction{
    childSymlinkRecord(
      baseRoot,
      childSymlinkId,
      std::move(childSymlinkMetadata),
      std::move(childSymlinkTarget)),
    buildNestedChildPathTransaction(
      baseRoot,
      nextRootId,
      parentPath,
      std::move(childName),
      ObjectType::Symlink,
      childSymlinkId,
      INITIAL_OBJECT_GENERATION,
      std::move(parentDirectoryMetadata))
  };
  validateCreateNestedSymlinkChildTransaction(transaction);
  return transaction;
}

UpdateRootFileChildTransaction buildUpdateRootFileChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::string childName,
  FileUpdate update) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  if (update.baseFile.filesystemId != baseRoot.rootDirectory->filesystemId) {
    throw std::runtime_error("Cannot build a format-v2 file update with a wrong-filesystem file record");
  }

  FileRecord file = updatedFileRecord(update);
  UpdateRootFileChildTransaction transaction{
    file,
    std::move(update.fileData),
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      baseRoot.rootDirectory->metadata,
      entriesWithUpdatedExistingChild(
        *baseRoot.rootDirectory,
        childName,
        ObjectType::File,
        file.fileId,
        file.generation - 1,
        file.generation))
  };
  validateUpdateRootFileChildTransaction(transaction);
  return transaction;
}

UpdateNestedFileChildTransaction buildUpdateNestedFileChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  std::string childName,
  FileUpdate update) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateParentPath(baseRoot, parentPath);
  if (update.baseFile.filesystemId != baseRoot.rootDirectory->filesystemId) {
    throw std::runtime_error("Cannot build a format-v2 file update with a wrong-filesystem file record");
  }
  validateChildObjectIdNotInParentPath(parentPath, update.baseFile.fileId, "file");

  FileRecord file = updatedFileRecord(update);
  UpdateNestedFileChildTransaction transaction{
    file,
    std::move(update.fileData),
    buildNestedExistingChildPathTransaction(
      baseRoot,
      nextRootId,
      parentPath,
      childName,
      ObjectType::File,
      file.fileId,
      file.generation - 1,
      file.generation)
  };
  validateUpdateNestedFileChildTransaction(transaction);
  return transaction;
}

UpdateRootSymlinkChildTransaction buildUpdateRootSymlinkChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::string childName,
  SymlinkUpdate update) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  if (update.baseSymlink.filesystemId != baseRoot.rootDirectory->filesystemId) {
    throw std::runtime_error("Cannot build a format-v2 symlink update with a wrong-filesystem symlink record");
  }

  SymlinkRecord symlink = updatedSymlinkRecord(update);
  UpdateRootSymlinkChildTransaction transaction{
    symlink,
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      baseRoot.rootDirectory->metadata,
      entriesWithUpdatedExistingChild(
        *baseRoot.rootDirectory,
        childName,
        ObjectType::Symlink,
        symlink.symlinkId,
        symlink.generation - 1,
        symlink.generation))
  };
  validateUpdateRootSymlinkChildTransaction(transaction);
  return transaction;
}

UpdateNestedSymlinkChildTransaction buildUpdateNestedSymlinkChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  std::string childName,
  SymlinkUpdate update) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateParentPath(baseRoot, parentPath);
  if (update.baseSymlink.filesystemId != baseRoot.rootDirectory->filesystemId) {
    throw std::runtime_error("Cannot build a format-v2 symlink update with a wrong-filesystem symlink record");
  }
  validateChildObjectIdNotInParentPath(parentPath, update.baseSymlink.symlinkId, "symlink");

  SymlinkRecord symlink = updatedSymlinkRecord(update);
  UpdateNestedSymlinkChildTransaction transaction{
    symlink,
    buildNestedExistingChildPathTransaction(
      baseRoot,
      nextRootId,
      parentPath,
      childName,
      ObjectType::Symlink,
      symlink.symlinkId,
      symlink.generation - 1,
      symlink.generation)
  };
  validateUpdateNestedSymlinkChildTransaction(transaction);
  return transaction;
}

DirectoryPathTransaction buildUpdateNestedDirectoryMetadataTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> directoryPath,
  DirectoryMetadata directoryMetadata) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateParentPath(baseRoot, directoryPath);

  DirectoryPathTransaction transaction = buildDirectoryEditTransaction(
    baseRoot,
    nextRootId,
    directoryPath,
    {},
    {
      DirectoryEdit{
        pathKeyFromSegments(directoryPath),
        std::move(directoryMetadata),
        directoryPath.back().directory.entries
      }
    });
  validateDirectoryPathTransaction(transaction);
  return transaction;
}

RemoveRootChildTransaction buildRemoveRootChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  DirectoryChildReference child,
  DirectoryMetadata rootDirectoryMetadata) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateDirectoryChildReference(child);

  RemoveRootChildTransaction transaction{
    buildRootDirectoryTransaction(
      baseRoot,
      nextRootId,
      std::move(rootDirectoryMetadata),
      entriesWithoutExistingChild(*baseRoot.rootDirectory, child))
  };
  validateRemoveRootChildTransaction(transaction);
  return transaction;
}

RemoveNestedChildTransaction buildRemoveNestedChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  DirectoryChildReference child,
  DirectoryMetadata parentDirectoryMetadata) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateParentPath(baseRoot, parentPath);
  validateDirectoryChildReference(child);

  RemoveNestedChildTransaction transaction{
    buildNestedRemoveChildPathTransaction(
      baseRoot,
      nextRootId,
      parentPath,
      child,
      std::move(parentDirectoryMetadata))
  };
  validateRemoveNestedChildTransaction(transaction);
  return transaction;
}

MoveChildTransaction buildMoveChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> sourceParentPath,
  DirectoryChildReference sourceChild,
  std::vector<DirectoryPathSegment> targetParentPath,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateDirectoryPath(baseRoot, sourceParentPath);
  validateDirectoryPath(baseRoot, targetParentPath);
  validateDirectoryChildReference(sourceChild);
  validateMoveDoesNotCreateDirectoryCycle(sourceChild, targetParentPath);

  MoveChildTransaction transaction{
    buildMoveChildPathTransaction(
      baseRoot,
      nextRootId,
      sourceParentPath,
      sourceChild,
      targetParentPath,
      std::move(targetChildName),
      std::move(sourceParentDirectoryMetadata),
      std::move(targetParentDirectoryMetadata))
  };
  validateMoveChildTransaction(transaction);
  return transaction;
}

MoveChildTransaction buildMoveChildReplacingTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> sourceParentPath,
  DirectoryChildReference sourceChild,
  std::vector<DirectoryPathSegment> targetParentPath,
  DirectoryChildReference targetChild,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  validateSelectedBaseRoot(baseRoot);
  validateNextRootId(baseRoot, nextRootId);
  validateDirectoryPath(baseRoot, sourceParentPath);
  validateDirectoryPath(baseRoot, targetParentPath);
  validateDirectoryChildReference(sourceChild);
  validateMoveReplacement(sourceChild, targetChild);
  validateMoveDoesNotCreateDirectoryCycle(sourceChild, targetParentPath);

  MoveChildTransaction transaction{
    buildMoveChildReplacingPathTransaction(
      baseRoot,
      nextRootId,
      sourceParentPath,
      sourceChild,
      targetParentPath,
      targetChild,
      std::move(targetChildName),
      std::move(sourceParentDirectoryMetadata),
      std::move(targetParentDirectoryMetadata))
  };
  validateMoveChildTransaction(transaction);
  return transaction;
}

RootOpenWithValidatedTreeResult publishRootDirectoryTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  DirectoryMetadata rootDirectoryMetadata,
  std::vector<DirectoryEntry> rootDirectoryEntries) {
  const RootDirectoryTransaction transaction = buildRootDirectoryTransaction(
    baseRoot,
    nextRootId,
    std::move(rootDirectoryMetadata),
    std::move(rootDirectoryEntries));

  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.rootRecord,
    transaction.rootContent,
    transaction.rootDirectory);
}

RootOpenWithValidatedTreeResult publishCreateRootDirectoryChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childDirectoryId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata) {
  const CreateRootDirectoryChildTransaction transaction =
    buildCreateRootDirectoryChildTransaction(
      baseRoot,
      nextRootId,
      childDirectoryId,
      std::move(childName),
      std::move(rootDirectoryMetadata),
      std::move(childDirectoryMetadata));

  directoryRecordStore.store(transaction.childDirectory);
  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.rootUpdate.rootRecord,
    transaction.rootUpdate.rootContent,
    transaction.rootUpdate.rootDirectory);
}

RootOpenWithValidatedTreeResult publishCreateRootFileChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childFileId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata) {
  const CreateRootFileChildTransaction transaction =
    buildCreateRootFileChildTransaction(
      baseRoot,
      nextRootId,
      childFileId,
      std::move(childName),
      std::move(rootDirectoryMetadata),
      std::move(childFileMetadata));

  fileRecordStore.store(transaction.childFile);
  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.rootUpdate.rootRecord,
    transaction.rootUpdate.rootContent,
    transaction.rootUpdate.rootDirectory);
}

RootOpenWithValidatedTreeResult publishCreateRootFileWithDataChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childFileId,
  ObjectId childFileDataId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload) {
  const CreateRootFileWithDataChildTransaction transaction =
    buildCreateRootFileWithDataChildTransaction(
      baseRoot,
      nextRootId,
      childFileId,
      childFileDataId,
      std::move(childName),
      std::move(rootDirectoryMetadata),
      std::move(childFileMetadata),
      std::move(childFilePayload));

  fileRecordStore.storeData(transaction.childFileData);
  fileRecordStore.store(transaction.childFile);
  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.rootUpdate.rootRecord,
    transaction.rootUpdate.rootContent,
    transaction.rootUpdate.rootDirectory);
}

RootOpenWithValidatedTreeResult publishCreateRootSymlinkChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childSymlinkId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget) {
  const CreateRootSymlinkChildTransaction transaction =
    buildCreateRootSymlinkChildTransaction(
      baseRoot,
      nextRootId,
      childSymlinkId,
      std::move(childName),
      std::move(rootDirectoryMetadata),
      std::move(childSymlinkMetadata),
      std::move(childSymlinkTarget));

  symlinkRecordStore.store(transaction.childSymlink);
  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.rootUpdate.rootRecord,
    transaction.rootUpdate.rootContent,
    transaction.rootUpdate.rootDirectory);
}

RootOpenWithValidatedTreeResult publishCreateNestedDirectoryChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childDirectoryId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata) {
  const CreateNestedDirectoryChildTransaction transaction =
    buildCreateNestedDirectoryChildTransaction(
      baseRoot,
      nextRootId,
      std::move(parentPath),
      childDirectoryId,
      std::move(childName),
      std::move(parentDirectoryMetadata),
      std::move(childDirectoryMetadata));

  directoryRecordStore.store(transaction.childDirectory);
  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.pathUpdate);
}

RootOpenWithValidatedTreeResult publishCreateNestedFileChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childFileId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata) {
  const CreateNestedFileChildTransaction transaction =
    buildCreateNestedFileChildTransaction(
      baseRoot,
      nextRootId,
      std::move(parentPath),
      childFileId,
      std::move(childName),
      std::move(parentDirectoryMetadata),
      std::move(childFileMetadata));

  fileRecordStore.store(transaction.childFile);
  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.pathUpdate);
}

RootOpenWithValidatedTreeResult publishCreateNestedFileWithDataChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childFileId,
  ObjectId childFileDataId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload) {
  const CreateNestedFileWithDataChildTransaction transaction =
    buildCreateNestedFileWithDataChildTransaction(
      baseRoot,
      nextRootId,
      std::move(parentPath),
      childFileId,
      childFileDataId,
      std::move(childName),
      std::move(parentDirectoryMetadata),
      std::move(childFileMetadata),
      std::move(childFilePayload));

  fileRecordStore.storeData(transaction.childFileData);
  fileRecordStore.store(transaction.childFile);
  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.pathUpdate);
}

RootOpenWithValidatedTreeResult publishCreateNestedSymlinkChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childSymlinkId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget) {
  const CreateNestedSymlinkChildTransaction transaction =
    buildCreateNestedSymlinkChildTransaction(
      baseRoot,
      nextRootId,
      std::move(parentPath),
      childSymlinkId,
      std::move(childName),
      std::move(parentDirectoryMetadata),
      std::move(childSymlinkMetadata),
      std::move(childSymlinkTarget));

  symlinkRecordStore.store(transaction.childSymlink);
  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.pathUpdate);
}

RootOpenWithValidatedTreeResult publishUpdateRootFileChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::string childName,
  FileUpdate update) {
  const UpdateRootFileChildTransaction transaction =
    buildUpdateRootFileChildTransaction(
      baseRoot,
      nextRootId,
      std::move(childName),
      std::move(update));

  for (const FileDataRecord &fileData: transaction.fileData) {
    fileRecordStore.storeData(fileData);
  }
  fileRecordStore.store(transaction.file);
  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.rootUpdate.rootRecord,
    transaction.rootUpdate.rootContent,
    transaction.rootUpdate.rootDirectory);
}

RootOpenWithValidatedTreeResult publishUpdateNestedFileChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  std::string childName,
  FileUpdate update) {
  const UpdateNestedFileChildTransaction transaction =
    buildUpdateNestedFileChildTransaction(
      baseRoot,
      nextRootId,
      std::move(parentPath),
      std::move(childName),
      std::move(update));

  for (const FileDataRecord &fileData: transaction.fileData) {
    fileRecordStore.storeData(fileData);
  }
  fileRecordStore.store(transaction.file);
  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.pathUpdate);
}

RootOpenWithValidatedTreeResult publishUpdateRootSymlinkChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::string childName,
  SymlinkUpdate update) {
  const UpdateRootSymlinkChildTransaction transaction =
    buildUpdateRootSymlinkChildTransaction(
      baseRoot,
      nextRootId,
      std::move(childName),
      std::move(update));

  symlinkRecordStore.store(transaction.symlink);
  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.rootUpdate.rootRecord,
    transaction.rootUpdate.rootContent,
    transaction.rootUpdate.rootDirectory);
}

RootOpenWithValidatedTreeResult publishUpdateNestedSymlinkChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  std::string childName,
  SymlinkUpdate update) {
  const UpdateNestedSymlinkChildTransaction transaction =
    buildUpdateNestedSymlinkChildTransaction(
      baseRoot,
      nextRootId,
      std::move(parentPath),
      std::move(childName),
      std::move(update));

  symlinkRecordStore.store(transaction.symlink);
  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.pathUpdate);
}

RootOpenWithValidatedTreeResult publishUpdateNestedDirectoryMetadataTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> directoryPath,
  DirectoryMetadata directoryMetadata) {
  const DirectoryPathTransaction transaction =
    buildUpdateNestedDirectoryMetadataTransaction(
      baseRoot,
      nextRootId,
      std::move(directoryPath),
      std::move(directoryMetadata));

  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction);
}

RootOpenWithValidatedTreeResult publishRemoveRootChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  DirectoryChildReference child,
  DirectoryMetadata rootDirectoryMetadata) {
  const RemoveRootChildTransaction transaction =
    buildRemoveRootChildTransaction(
      baseRoot,
      nextRootId,
      std::move(child),
      std::move(rootDirectoryMetadata));

  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.rootUpdate.rootRecord,
    transaction.rootUpdate.rootContent,
    transaction.rootUpdate.rootDirectory);
}

RootOpenWithValidatedTreeResult publishRemoveNestedChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  DirectoryChildReference child,
  DirectoryMetadata parentDirectoryMetadata) {
  const RemoveNestedChildTransaction transaction =
    buildRemoveNestedChildTransaction(
      baseRoot,
      nextRootId,
      std::move(parentPath),
      std::move(child),
      std::move(parentDirectoryMetadata));

  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.pathUpdate);
}

RootOpenWithValidatedTreeResult publishMoveChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> sourceParentPath,
  DirectoryChildReference sourceChild,
  std::vector<DirectoryPathSegment> targetParentPath,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  const MoveChildTransaction transaction =
    buildMoveChildTransaction(
      baseRoot,
      nextRootId,
      std::move(sourceParentPath),
      std::move(sourceChild),
      std::move(targetParentPath),
      std::move(targetChildName),
      std::move(sourceParentDirectoryMetadata),
      std::move(targetParentDirectoryMetadata));

  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.pathUpdate);
}

RootOpenWithValidatedTreeResult publishMoveChildReplacingTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> sourceParentPath,
  DirectoryChildReference sourceChild,
  std::vector<DirectoryPathSegment> targetParentPath,
  DirectoryChildReference targetChild,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  const MoveChildTransaction transaction =
    buildMoveChildReplacingTransaction(
      baseRoot,
      nextRootId,
      std::move(sourceParentPath),
      std::move(sourceChild),
      std::move(targetParentPath),
      std::move(targetChild),
      std::move(targetChildName),
      std::move(sourceParentDirectoryMetadata),
      std::move(targetParentDirectoryMetadata));

  return publishDirectoryPathTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    transaction.pathUpdate);
}

}
}
