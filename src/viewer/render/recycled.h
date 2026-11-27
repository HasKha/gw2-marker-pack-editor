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
    std::shared_ptr<T> take() { return spare && spare.use_count() == 1 ? std::move(spare) : std::make_shared<T>(); }
    // Keeps `built`, which is about to be published.
    void keep(const std::shared_ptr<T>& built) {
        spare = std::move(current);
        current = built;
    }
    void clear() {
        current.reset();
        spare.reset();
    }

private:
    std::shared_ptr<T> current;
    std::shared_ptr<T> spare;
};

} // namespace viewer
