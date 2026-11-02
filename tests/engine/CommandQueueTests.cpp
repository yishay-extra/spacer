#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <thread>

#include "engine/AllocationGuard.h"
#include "engine/Command.h"
#include "engine/EngineProcessor.h"
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
