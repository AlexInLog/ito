// Fake downloads with terminal progress bars. Each download is a task written as a plain loop, and all of them
// interleave on one thread. The coordinator redraws the bars, and cancels whatever is still running at the deadline
// by destroying its task: the download stops at the sleep it was suspended in, and its frame is freed.

#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>
#include <ito/task.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    enum class status : std::uint8_t
    {
        running,
        done,
        cancelled
    };

    struct download_state
    {
        std::string               name;
        std::chrono::milliseconds chunk_delay;
        int                       percent{};
        status                    state{status::running};
    };

    ito::coro<> download(download_state& d)
    {
        while (d.percent < 100)
        {
            co_await ito::async::sleep_for(d.chunk_delay); // waiting for the "network"
            d.percent += 5;
        }
        d.state = status::done;
    }

    void draw(const std::vector<download_state>& downloads, bool first_frame)
    {
        constexpr int bar_width = 20;
        if (!first_frame) // move the cursor back up over the previous frame
            std::cout << "\x1b[" << downloads.size() << 'A';

        for (const auto& d : downloads)
        {
            const auto filled = static_cast<std::size_t>(d.percent * bar_width / 100);
            std::cout << d.name << "  [" << std::string(filled, '#') << std::string(bar_width - filled, '-') << "]  ";
            switch (d.state)
            {
                case status::running:
                    std::cout << d.percent << "%";
                    break;
                case status::done:
                    std::cout << "done";
                    break;
                case status::cancelled:
                    std::cout << "cancelled at " << d.percent << "% (timeout)";
                    break;
            }
            std::cout << "\x1b[K\n"; // clear what's left of the line from the previous frame
        }
        std::cout << std::flush;
    }

    ito::coro<> run_downloads(std::vector<download_state>& downloads, std::chrono::milliseconds timeout)
    {
        std::vector<std::optional<ito::task<void>>> tasks{};
        tasks.reserve(downloads.size());
        // cppcheck-suppress useStlAlgorithm
        for (auto& d : downloads) tasks.emplace_back(ito::loop::current().create_task(download(d)));

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        draw(downloads, true);
        while (std::ranges::any_of(downloads, [](const download_state& d) { return d.state == status::running; }))
        {
            co_await ito::async::sleep_for(50ms);

            if (std::chrono::steady_clock::now() >= deadline)
            {
                for (std::size_t i = 0; i < downloads.size(); ++i)
                {
                    if (downloads.at(i).state == status::running)
                    {
                        tasks.at(i).reset(); // cancels the download: its frame is destroyed mid-sleep
                        downloads.at(i).state = status::cancelled;
                    }
                }
            }
            draw(downloads, false);
        }
    }
} // namespace

int main()
{
    // 20 chunks each: about 0.5 s, 0.8 s, 1.4 s and 6 s, against a 3 s timeout
    std::vector<download_state> downloads{
        {.name = "small.iso ", .chunk_delay = 25ms},
        {.name = "medium.iso", .chunk_delay = 40ms},
        {.name = "large.iso ", .chunk_delay = 70ms},
        {.name = "stuck.iso ", .chunk_delay = 300ms},
    };

    try
    {
        ito::loop loop{};
        loop.run_until_complete(run_downloads(downloads, 3s));

        const bool as_expected = downloads.at(0).state == status::done && downloads.at(1).state == status::done
                              && downloads.at(2).state == status::done && downloads.at(3).state == status::cancelled;
        return as_expected ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        std::cerr << "failed: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
