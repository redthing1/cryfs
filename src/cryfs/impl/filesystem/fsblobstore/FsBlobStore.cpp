#include "FsBlobStore.h"
#include "FileBlob.h"
#include "DirBlob.h"
#include "SymlinkBlob.h"
#include <cryfs/impl/config/CryConfigFile.h>

using cpputils::unique_ref;
using cpputils::make_unique_ref;
using boost::none;

namespace cryfs {
namespace fsblobstore {

boost::optional<unique_ref<FsBlob>> FsBlobStore::load(const blockstore::BlockId &blockId) {
    auto blob = _baseBlobStore->load(blockId);
    if (blob == none) {
        return none;
    }
    const FsBlobView::BlobType blobType = FsBlobView::blobType(**blob);
    if (blobType == FsBlobView::BlobType::FILE) {
        return unique_ref<FsBlob>(make_unique_ref<FileBlob>(std::move(*blob)));
    } else if (blobType == FsBlobView::BlobType::DIR) {
        return unique_ref<FsBlob>(make_unique_ref<DirBlob>(std::move(*blob)));
    } else if (blobType == FsBlobView::BlobType::SYMLINK) {
        return unique_ref<FsBlob>(make_unique_ref<SymlinkBlob>(std::move(*blob)));
    } else {
        ASSERT(false, "Unknown magic number");
    }
}

}
}
