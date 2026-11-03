#pragma once

// Edit counter and the revision last saved. The saved revision is only ever
// set by markSaved(), so a document is clean exactly when nothing has been
// bumped since. Message thread only.

#include <cstdint>

namespace spacer {

class DocumentRevision {
public:
    std::uint64_t value() const noexcept { return revision_; }
    void bump() noexcept { ++revision_; }
    void markSaved() noexcept { saved_ = revision_; }
    bool isDirty() const noexcept { return revision_ != saved_; }

private:
    std::uint64_t revision_{0};
    std::uint64_t saved_{0};
};

}  // namespace spacer
