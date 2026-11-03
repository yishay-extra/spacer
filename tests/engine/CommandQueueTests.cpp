#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <thread>

#include "engine/AllocationGuard.h"
#include "engine/Command.h"
#include "engine/EngineProcessor.h"
#include "engine/Seqlock.h"
#include "engine/SpscQueue.h"

using namespace spacer;

TEST_CASE("Command queue: 1M commands pass through without allocating") {
    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();
    auto q = std::make_unique<SpscQueue<Command, EngineProcessor::kCommandCapacity>>();

    long received = 0;
    float lastGain = -1.0f;
    {
        allocguard::ScopedAudioThread guard;
        for (int round = 0; round < 1000; ++round) {
            for (int i = 0; i < 1000; ++i)
                REQUIRE(q->push(cmd::SetSourceGain{i, static_cast<float>(round * 1000 + i), 10.0f}));
            Command c;
            while (q->pop(c)) {
                ++received;
                lastGain = std::get<cmd::SetSourceGain>(c).gainDb;
            }
        }
    }
    CHECK(received == 1000000);
    CHECK(lastGain == 999999.0f);
    CHECK(q->dropped() == 0);
    CHECK(allocguard::violations() == 0);
}

TEST_CASE("Command queue: 1M commands from a producer thread arrive in order, without allocating") {
    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();
    auto q = std::make_unique<SpscQueue<Command, EngineProcessor::kCommandCapacity>>();
    constexpr int kTotal = 1000000;

    std::thread producer([&] {
        allocguard::ScopedAudioThread guard;
        for (int i = 0; i < kTotal; ++i)
            while (!q->push(cmd::ClockSetBeat{static_cast<double>(i)})) std::this_thread::yield();
    });
    int expected = 0;
    bool inOrder = true;
    {
        allocguard::ScopedAudioThread guard;
        Command c;
        while (expected < kTotal) {
            if (!q->pop(c)) continue;
            inOrder = inOrder && std::get<cmd::ClockSetBeat>(c).beat == static_cast<double>(expected);
            ++expected;
        }
    }
    producer.join();
    CHECK(inOrder);
    CHECK(expected == kTotal);
    CHECK(allocguard::violations() == 0);
}

TEST_CASE("Command queue: overflow is dropped and counted") {
    auto q = std::make_unique<SpscQueue<Command, 4096>>();
    for (int i = 0; i < 4096; ++i) REQUIRE(q->push(cmd::ClockPlay{}));
    CHECK_FALSE(q->push(cmd::ClockStop{}));
    CHECK_FALSE(q->push(cmd::ClockStop{}));
    CHECK(q->dropped() == 2);
    CHECK(q->size() == 4096);
    Command c;
    REQUIRE(q->pop(c));
    CHECK(q->push(cmd::ClockStop{}));
}

TEST_CASE("Allocation guard: detects an allocation inside the audio scope only") {
    REQUIRE(allocguard::enabled());
    allocguard::setMode(allocguard::Mode::Count);
    allocguard::resetViolations();
    auto outside = std::make_unique<int>(1);
    CHECK(allocguard::violations() == 0);
    {
        allocguard::ScopedAudioThread guard;
        // Call the allocation functions directly: optimisers may elide a
        // paired new-expression / delete-expression (e.g. make_unique<int>),
        // but not explicit calls to ::operator new / ::operator delete.
        void* volatile p = ::operator new(16);
        ::operator delete(p);
    }
    CHECK(allocguard::violations() == 2);
    allocguard::resetViolations();
}

TEST_CASE("Seqlock: readers never see a torn value while the writer stores (WP14)") {
    struct Pair {
        std::int64_t a{0};
        std::int64_t b{0};  // always -a
        double c{0.0};      // always a * 0.5
    };
    spacer::Seqlock<Pair> lock;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (std::int64_t i = 1; !stop.load(); ++i) lock.store(Pair{i, -i, static_cast<double>(i) * 0.5});
    });
    int torn = 0;
    std::int64_t last = 0;
    bool monotonic = true;
    for (int i = 0; i < 200000; ++i) {
        const Pair p = lock.load();
        if (p.b != -p.a || std::abs(p.c - static_cast<double>(p.a) * 0.5) > 0.0) ++torn;
        if (p.a < last) monotonic = false;
        last = p.a;
    }
    stop = true;
    writer.join();
    CHECK(torn == 0);
    CHECK(monotonic);
}

TEST_CASE("EngineProcessor: clockState() is the published snapshot of the last block (WP14)") {
    spacer::EngineProcessor engine;
    engine.push(spacer::cmd::ClockSetBpm{90.0});
    engine.push(spacer::cmd::ClockPlay{});
    float* out[1] = {nullptr};
    for (int i = 0; i < 4; ++i) engine.process(nullptr, 0, out, 0, 480);
    const auto s = engine.snapshot();
    CHECK(s.blockStartSample == 3 * 480);
    CHECK(engine.clockState().bpm == Catch::Approx(90.0));
    CHECK(engine.clockState().playing);
    CHECK(engine.clockState().beat == Catch::Approx(s.clock.beat));
}
