#include "ParallelProcessing.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

bool testEveryItemRunsOnce()
{
    constexpr std::size_t itemCount = 10000;
    std::vector<std::atomic<int>> visits(itemCount);
    for (std::atomic<int> &visit : visits) {
        visit.store(0);
    }

    superccd::parallel::forRanges(
        0,
        itemCount,
        64,
        [&](std::size_t begin, std::size_t end, unsigned) {
            for (std::size_t index = begin; index < end; ++index) {
                visits[index].fetch_add(1);
            }
        });

    for (const std::atomic<int> &visit : visits) {
        if (visit.load() != 1) {
            std::fprintf(stderr, "parallel range did not visit every item once\n");
            return false;
        }
    }
    return true;
}

bool testExceptionsPropagate()
{
    try {
        superccd::parallel::forRanges(
            0,
            1000,
            1,
            [](std::size_t begin, std::size_t end, unsigned) {
                if (begin <= 500 && 500 < end) {
                    throw std::runtime_error("expected");
                }
            });
    } catch (const std::runtime_error &) {
        return true;
    }

    std::fprintf(stderr, "parallel range swallowed a worker exception\n");
    return false;
}

} // namespace

int main()
{
    return testEveryItemRunsOnce() && testExceptionsPropagate() ? 0 : 1;
}
