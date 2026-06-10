#include "ina226.hpp"

namespace espp {
Ina226MockState& ina226_mock_state() {
    static Ina226MockState s;
    return s;
}
}  // namespace espp
