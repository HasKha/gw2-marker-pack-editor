#pragma once

#include <memory>

namespace viewer {

// The last scene published, and the one before it, the older reused once no sink holds it any
// more. A scene is thousands of items, and allocating one a frame is a waste.
template <class T>
class Recycled
{
public:
    // A scene to build into: the spare, if nothing holds it, or a new one.
    std::shared_ptr<T> take() { return spare_ && spare_.use_count() == 1 ? std::move(spare_) : std::make_shared<T>(); }
    // Keeps `built`, which is about to be published.
    void keep(const std::shared_ptr<T>& built) {
        spare_ = std::move(current_);
        current_ = built;
    }
    void clear() {
        current_.reset();
        spare_.reset();
    }

private:
    std::shared_ptr<T> current_;
    std::shared_ptr<T> spare_;
};

} // namespace viewer
