// asCDualRefCount: functions and types are deleted once both their external
// and internal counts are zero, and exactly one decrement may make that call,
// whichever order and whichever threads the two counts reach zero on.
#include <gtest/gtest.h>
#include "as_atomic.h"
#include <atomic>
#include <thread>

namespace {

TEST(DualRefCount, CountsAreIndependent) {
	asCDualRefCount c;
	c.set(2, 3);
	EXPECT_EQ(c.getExternal(), 2u);
	EXPECT_EQ(c.getInternal(), 3u);
	EXPECT_EQ(c.incExternal(), 3u);
	EXPECT_EQ(c.getInternal(), 3u);
	EXPECT_EQ(c.incInternal(), 4u);
	EXPECT_EQ(c.getExternal(), 3u);
}

// The interleaving that two separate counters got wrong: each side zeroes its
// own count and then finds the other already at zero, so both deleted.
TEST(DualRefCount, ExternalThenInternalReportsLastOnce) {
	asCDualRefCount c;
	c.set(1, 1);
	bool last = true;
	EXPECT_EQ(c.decExternal(last), 0u);
	EXPECT_FALSE(last);
	EXPECT_EQ(c.decInternal(last), 0u);
	EXPECT_TRUE(last);
}

TEST(DualRefCount, InternalThenExternalReportsLastOnce) {
	asCDualRefCount c;
	c.set(1, 1);
	bool last = true;
	EXPECT_EQ(c.decInternal(last), 0u);
	EXPECT_FALSE(last);
	EXPECT_EQ(c.decExternal(last), 0u);
	EXPECT_TRUE(last);
}

// An external count may return to zero repeatedly while the engine still
// holds the function; none of those zeroes is the last reference.
TEST(DualRefCount, ExternalZeroWhileInternallyHeldIsNotLast) {
	asCDualRefCount c;
	c.set(0, 1);
	for (int i = 0; i < 3; ++i) {
		bool last = true;
		EXPECT_EQ(c.incExternal(), 1u);
		EXPECT_EQ(c.decExternal(last), 0u);
		EXPECT_FALSE(last);
	}
	bool last = false;
	EXPECT_EQ(c.decInternal(last), 0u);
	EXPECT_TRUE(last);
}

// Two threads race to zero opposite counts; across every round exactly one
// of them is told it released the last reference.
TEST(DualRefCount, ConcurrentZeroingReportsLastExactlyOnce) {
	const int rounds = 20000;
	asCDualRefCount c;
	std::atomic<int> go{0};
	std::atomic<int> done{0};
	std::atomic<int> lastCount{0};

	auto worker = [&](bool external) {
		for (int r = 1; r <= rounds; ++r) {
			while (go.load(std::memory_order_acquire) < r) {
				std::this_thread::yield();
			}
			bool last = false;
			if (external) {
				c.decExternal(last);
			} else {
				c.decInternal(last);
			}
			if (last) {
				lastCount.fetch_add(1, std::memory_order_relaxed);
			}
			done.fetch_add(1, std::memory_order_acq_rel);
		}
	};

	std::thread a(worker, true);
	std::thread b(worker, false);
	for (int r = 1; r <= rounds; ++r) {
		c.set(1, 1);
		go.store(r, std::memory_order_release);
		while (done.load(std::memory_order_acquire) < 2 * r) {
			std::this_thread::yield();
		}
		ASSERT_EQ(lastCount.load(std::memory_order_relaxed), r);
	}
	a.join();
	b.join();
}

}
