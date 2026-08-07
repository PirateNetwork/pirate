// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Regression coverage for CManagedProcess::IsRunning() actually reaping a
// child that exited on its own, rather than just returning a flag that only
// Stop() ever clears. Before this, a crashed/OOM-killed embedded i2pd or tor
// daemon was left as a zombie forever and nothing - not even a supervisor -
// could tell it had died, since IsRunning() always reported "still running"
// until someone called Stop() (see i2pd_process.cpp / tor_process.cpp's
// ThreadSupervise*() for the caller this exists for).

#include "embeddedprocess.h"
#include "utiltime.h"

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

namespace {

// Polls IsRunning() until it reports false or the deadline passes; returns
// the final observed value so callers can assert on it with a clear failure
// message instead of just timing out silently.
bool WaitUntilNotRunning(CManagedProcess& proc, int timeoutMs)
{
    const int64_t deadline = GetTimeMillis() + timeoutMs;
    do {
        if (!proc.IsRunning()) return true;
        MilliSleep(20);
    } while (GetTimeMillis() < deadline);
    return !proc.IsRunning();
}

} // namespace

TEST(EmbeddedProcess, NeverSpawnedIsNotRunning)
{
    CManagedProcess proc;
    EXPECT_FALSE(proc.IsRunning());
    EXPECT_EQ(proc.GetProcessId(), 0);
    // Must be a safe no-op: nothing was ever spawned.
    proc.Stop(100);
}

TEST(EmbeddedProcess, IsRunningReapsChildThatExitsOnItsOwn)
{
    const boost::filesystem::path logDir = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("pirate-embeddedprocess-test-%%%%-%%%%");
    boost::filesystem::create_directories(logDir);

    CManagedProcess proc;
    ASSERT_TRUE(proc.Spawn("/bin/sh", {"-c", "exit 7"}, logDir / "stdout.log", logDir / "stderr.log"));
    EXPECT_GT(proc.GetProcessId(), 0);

    // Simulates a daemon crashing mid-run with nobody having called Stop():
    // IsRunning() alone - polled the way a supervisor thread would - must
    // notice the exit and reap it, not report "still running" forever.
    EXPECT_TRUE(WaitUntilNotRunning(proc, 3000));
    EXPECT_EQ(proc.GetProcessId(), 0);

    // Calling it again after the child is already reaped must stay false
    // rather than misinterpreting a stale/recycled pid.
    EXPECT_FALSE(proc.IsRunning());

    boost::filesystem::remove_all(logDir);
}

TEST(EmbeddedProcess, StopTerminatesLongRunningChild)
{
    const boost::filesystem::path logDir = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("pirate-embeddedprocess-test-%%%%-%%%%");
    boost::filesystem::create_directories(logDir);

    CManagedProcess proc;
    ASSERT_TRUE(proc.Spawn("/bin/sleep", {"30"}, logDir / "stdout.log", logDir / "stderr.log"));
    EXPECT_TRUE(proc.IsRunning());

    proc.Stop(2000);
    EXPECT_FALSE(proc.IsRunning());
    EXPECT_EQ(proc.GetProcessId(), 0);

    boost::filesystem::remove_all(logDir);
}
