#ifndef PARALLELPROCESSING_H
#define PARALLELPROCESSING_H

#include <algorithm>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace superccd::parallel {

inline unsigned availableWorkers()
{
    const unsigned detected = std::thread::hardware_concurrency();
    return detected == 0 ? 1u : detected;
}

template <typename Function>
void forRanges(std::size_t begin,
               std::size_t end,
               std::size_t minimumItemsPerWorker,
               Function &&function)
{
    if (end <= begin) {
        return;
    }

    const std::size_t itemCount = end - begin;
    const std::size_t minimumItems = std::max<std::size_t>(minimumItemsPerWorker, 1);
    const unsigned workerCount = static_cast<unsigned>(std::min<std::size_t>(
        availableWorkers(),
        (itemCount + minimumItems - 1) / minimumItems));
    if (workerCount <= 1) {
        function(begin, end, 0);
        return;
    }

    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    const std::size_t chunkSize =
        (itemCount + static_cast<std::size_t>(workerCount) - 1) /
        static_cast<std::size_t>(workerCount);

    auto runWorker = [&](unsigned workerIndex) {
        const std::size_t rangeBegin =
            begin + static_cast<std::size_t>(workerIndex) * chunkSize;
        const std::size_t rangeEnd = std::min(end, rangeBegin + chunkSize);
        if (rangeBegin >= rangeEnd) {
            return;
        }
        try {
            function(rangeBegin, rangeEnd, workerIndex);
        } catch (...) {
            std::lock_guard<std::mutex> lock(exceptionMutex);
            if (!firstException) {
                firstException = std::current_exception();
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(workerCount - 1);
    try {
        for (unsigned workerIndex = 1; workerIndex < workerCount; ++workerIndex) {
            workers.emplace_back(runWorker, workerIndex);
        }
    } catch (...) {
        for (std::thread &worker : workers) {
            worker.join();
        }
        throw;
    }
    runWorker(0);
    for (std::thread &worker : workers) {
        worker.join();
    }

    if (firstException) {
        std::rethrow_exception(firstException);
    }
}

template <typename Function>
void forRows(int rowCount, int minimumRowsPerWorker, Function &&function)
{
    if (rowCount <= 0) {
        return;
    }
    forRanges(
        0,
        static_cast<std::size_t>(rowCount),
        static_cast<std::size_t>(std::max(minimumRowsPerWorker, 1)),
        [&](std::size_t begin, std::size_t end, unsigned workerIndex) {
            for (std::size_t row = begin; row < end; ++row) {
                function(static_cast<int>(row), workerIndex);
            }
        });
}

} // namespace superccd::parallel

#endif // PARALLELPROCESSING_H
