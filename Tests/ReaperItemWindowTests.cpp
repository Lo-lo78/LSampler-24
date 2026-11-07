// TEST176: executable validation of the geometry used by the REAPER VST3 import.
#include "../Source/ReaperItemWindow.h"
#include <cassert>
#include <cmath>
#include <limits>

int main()
{
    using namespace lsampler_reaper_item;
    Window out;
    // Three items referring to precisely the same 10-second source file.
    assert(computeUnprocessedWindow(0.0, 1.2, 10.0, 1.0, 0.0, 0, false, 0, out));
    assert(out.startSeconds == 0.0 && std::abs(out.endSeconds - 1.2) < 1e-10);
    assert(computeUnprocessedWindow(1.2, 2.3, 10.0, 1.0, 0.0, 0, false, 0, out));
    assert(out.startSeconds == 1.2 && std::abs(out.endSeconds - 3.5) < 1e-10);
    assert(computeUnprocessedWindow(3.5, 3.3, 10.0, 1.0, 0.0, 0, false, 0, out));
    assert(out.startSeconds == 3.5 && std::abs(out.endSeconds - 6.8) < 1e-10);
    assert(computeUnprocessedWindow(0.0, 10.0, 10.0, 1.0, 0.0, 0, false, 0, out));
    // Do not import the wrong audible region for complex items.
    assert(!computeUnprocessedWindow(1.0, 2.0, 10.0, 2.0, 0.0, 0, false, 0, out));
    assert(!computeUnprocessedWindow(1.0, 2.0, 10.0, 1.0, 3.0, 0, false, 0, out));
    assert(!computeUnprocessedWindow(1.0, 2.0, 10.0, 1.0, 0.0, 1, false, 0, out));
    assert(!computeUnprocessedWindow(1.0, 2.0, 10.0, 1.0, 0.0, 0, true, 0, out));
    assert(!computeUnprocessedWindow(1.0, 2.0, 10.0, 1.0, 0.0, 0, false, 2, out));
    assert(!computeUnprocessedWindow(9.5, 2.0, 10.0, 1.0, 0.0, 0, false, 0, out));
    assert(!computeUnprocessedWindow(0.0, 0.0, 10.0, 1.0, 0.0, 0, false, 0, out));
    assert(!computeUnprocessedWindow(-1.0, 1.0, 10.0, 1.0, 0.0, 0, false, 0, out));
    assert(!computeUnprocessedWindow(0.0, 1.0, std::numeric_limits<double>::quiet_NaN(),
                                      1.0, 0.0, 0, false, 0, out));
    return 0;
}
