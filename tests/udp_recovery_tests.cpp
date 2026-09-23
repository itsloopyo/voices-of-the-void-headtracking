// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The port-conflict recovery, measured against a real socket.
//
// udp_link_tests.cpp locks how the link READS once the receiver's flags move.
// This one locks that they move at all, and how fast: a second process holds
// the tracker port, this mod starts anyway, the player closes the other game,
// and the receiver has to take the port and start delivering poses without
// anyone touching it. Every number this prints is measured in the run, because
// the cadence comes out of the supervisor thread's clock arithmetic rather than
// off any constant that can be read from a header.
//
// The holder is a socket in this process, not a second process. Windows refuses
// the second bind identically either way - the core socket deliberately does not
// set SO_REUSEADDR - so the conflict is the real one.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cameraunlock/protocol/socket_types.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "test_harness.h"

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Collects everything the receiver logged, so the bind failure can be read back
// and checked for what it actually blames.
class LogCapture {
public:
    void Add(const std::string& line) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lines.push_back(line);
    }

    std::vector<std::string> Lines() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lines;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_lines;
};

// A UDP socket held open on an OS-assigned port, standing in for the game the
// player forgot to close. Port 0 lets the OS pick a free one, so this never
// fights whatever is really on 4242 on the machine running the suite.
class PortHolder {
public:
    bool Open() {
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) return false;

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0;
        if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            Close();
            return false;
        }

        sockaddr_in bound = {};
        int boundSize = sizeof(bound);
        if (getsockname(m_socket, reinterpret_cast<sockaddr*>(&bound), &boundSize) == SOCKET_ERROR) {
            Close();
            return false;
        }
        m_port = ntohs(bound.sin_port);
        return true;
    }

    void Close() {
        if (m_socket == INVALID_SOCKET) return;
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    ~PortHolder() { Close(); }

    uint16_t Port() const { return m_port; }

private:
    SOCKET m_socket = INVALID_SOCKET;
    uint16_t m_port = 0;
};

// One OpenTrack datagram: six little-endian doubles, position first.
void SendPose(uint16_t port, double yaw, double pitch, double roll) {
    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender == INVALID_SOCKET) return;

    const double payload[6] = {0.0, 0.0, 0.0, yaw, pitch, roll};

    sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

    sendto(sender, reinterpret_cast<const char*>(payload), sizeof(payload), 0,
           reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    closesocket(sender);
}

// A tracker app left running the whole time, sending at 60Hz to a port it does
// not care is bound. This is the ordinary case - OpenTrack is started once and
// left alone - and datagrams sent while nothing is listening are dropped by the
// OS, so the first pose the mod sees is whichever packet lands after the bind.
class ContinuousSender {
public:
    void Start(uint16_t port) {
        m_stop.store(false);
        m_thread = std::thread([this, port] {
            SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock == INVALID_SOCKET) return;

            sockaddr_in dest = {};
            dest.sin_family = AF_INET;
            dest.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

            // Walked rather than held: the receiver's pose gate discards a
            // bit-identical repeat, so a constant test pose would be accepted
            // once and then look like a tracker that has lost the head.
            double yaw = 0.0;
            while (!m_stop.load(std::memory_order_relaxed)) {
                yaw += 0.25;
                if (yaw > 20.0) yaw = 0.0;
                const double payload[6] = {0.0, 0.0, 0.0, yaw, 1.0, -1.0};
                sendto(sock, reinterpret_cast<const char*>(payload), sizeof(payload), 0,
                       reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            closesocket(sock);
        });
    }

    void Stop() {
        m_stop.store(true, std::memory_order_relaxed);
        if (m_thread.joinable()) m_thread.join();
    }

    ~ContinuousSender() { Stop(); }

private:
    std::atomic<bool> m_stop{true};
    std::thread m_thread;
};

// Poll until `ready`, or until the budget runs out. Returns how long it took, or
// a negative number if it never happened.
template <typename Fn>
double WaitFor(Fn ready, double budgetMs) {
    const Clock::time_point start = Clock::now();
    while (MsSince(start) < budgetMs) {
        if (ready()) return MsSince(start);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ready() ? MsSince(start) : -1.0;
}

// The whole scenario, timed: port held, mod starts, port freed, tracker sends.
void TestHeldPortIsReclaimedAndDeliversPoses() {
    PortHolder holder;
    CHECK_MSG(holder.Open(), "the stand-in for the other game could not take a port");
    if (holder.Port() == 0) return;

    LogCapture log;
    cameraunlock::UdpReceiver receiver;
    receiver.SetLog([&log](const std::string& line) { log.Add(line); });

    const bool bound = receiver.Start(holder.Port());
    CHECK_MSG(!bound, "Start() must report the port as busy while another socket holds it");
    CHECK_MSG(receiver.IsRetrying(),
              "a busy port must leave the supervisor retrying, not the receiver dead");
    CHECK(!receiver.IsRunning());

    // What the log blames has to be what the OS said. A bind fails for reasons
    // other than a conflict, and a line naming the wrong one sends the player
    // hunting an app that is not running.
    bool namesTheOsError = false;
    for (const std::string& line : log.Lines()) {
        std::printf("  log: %s\n", line.c_str());
        // WSAEADDRINUSE. The code is the OS's own and FormatMessage's text rides
        // with it; asserting on the code keeps this readable on a machine with a
        // non-English system locale.
        if (line.find("bind failed with error 10048") != std::string::npos) {
            namesTheOsError = true;
        }
    }
    CHECK_MSG(namesTheOsError,
              "the bind failure line must carry the OS's own code, not an assumed cause");

    // The player closes the other game.
    const Clock::time_point released = Clock::now();
    holder.Close();

    const double bindMs = WaitFor([&receiver] { return receiver.IsRunning(); }, 5000.0);
    std::printf("  port freed -> bound: %.0f ms\n", bindMs);
    CHECK_MSG(bindMs >= 0.0, "the receiver never took the port back");
    // The 500ms retry interval plus a 100ms supervisor tick, with slack for a
    // loaded CI runner. What the number is for is that it stays sub-second in
    // practice: the player must never have to relaunch the game.
    CHECK_MSG(bindMs >= 0.0 && bindMs < 1500.0,
              "the port must be reclaimed within a second or so of it coming free");
    CHECK(!receiver.IsRetrying());
    CHECK(!receiver.IsFailed());

    // And it is a working socket, not merely a bound one.
    SendPose(holder.Port(), 12.0, -4.0, 2.5);
    const double poseMs = WaitFor(
        [&receiver] {
            float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
            return receiver.GetRotation(yaw, pitch, roll);
        },
        2000.0);
    std::printf("  bound -> first pose: %.0f ms\n", poseMs);
    std::printf("  port freed -> first pose: %.0f ms\n", MsSince(released));
    CHECK_MSG(poseMs >= 0.0, "the reclaimed socket delivered no pose");

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    CHECK(receiver.GetRotation(yaw, pitch, roll));
    CHECK_NEAR(yaw, 12.0, 0.001);
    CHECK_NEAR(pitch, -4.0, 0.001);
    CHECK_NEAR(roll, 2.5, 0.001);
    CHECK_MSG(receiver.IsReceiving(), "a delivered pose must read as a live connection");

    receiver.Stop();
}

// The retry does not stop after a round or two. A player who leaves the other
// game running for a while and closes it later must get the port as promptly as
// one who closes it straight away, which means the supervisor is still
// attempting several intervals in.
void TestRetryKeepsItsCadenceOverManyIntervals() {
    PortHolder holder;
    CHECK(holder.Open());
    if (holder.Port() == 0) return;

    cameraunlock::UdpReceiver receiver;
    receiver.SetLog([](const std::string&) {});
    receiver.Start(holder.Port());
    CHECK(receiver.IsRetrying());

    // Six retry intervals of holding on.
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    CHECK_MSG(receiver.IsRetrying(), "the supervisor gave up while the port was still held");
    CHECK(!receiver.IsRunning());

    holder.Close();
    const double bindMs = WaitFor([&receiver] { return receiver.IsRunning(); }, 5000.0);
    std::printf("  held 3s, then freed -> bound: %.0f ms\n", bindMs);
    CHECK_MSG(bindMs >= 0.0 && bindMs < 1500.0,
              "a long wait must not slow the eventual bind");

    receiver.Stop();
}

// Stop() during the retry loop has to return, or the mod hangs the game on its
// way out over a port that may never come free.
void TestStopWhileWaitingReturnsPromptly() {
    PortHolder holder;
    CHECK(holder.Open());
    if (holder.Port() == 0) return;

    cameraunlock::UdpReceiver receiver;
    receiver.SetLog([](const std::string&) {});
    receiver.Start(holder.Port());
    CHECK(receiver.IsRetrying());

    const Clock::time_point start = Clock::now();
    receiver.Stop();
    const double stopMs = MsSince(start);
    std::printf("  Stop() while waiting for the port: %.0f ms\n", stopMs);
    CHECK_MSG(stopMs < 1000.0, "Stop() must not block for a port that is still held");
    CHECK(!receiver.IsRetrying());
    CHECK(!receiver.IsRunning());
}

// The number the player actually feels: from the other game closing to the view
// starting to move, with the tracker running throughout and nobody touching
// anything. Both halves are in it - the supervisor noticing the port came free,
// and the reopened socket picking up a feed that was already in flight.
void TestFreedPortToFirstPoseWithTrackerAlreadyRunning() {
    PortHolder holder;
    CHECK(holder.Open());
    if (holder.Port() == 0) return;

    cameraunlock::UdpReceiver receiver;
    receiver.SetLog([](const std::string&) {});
    receiver.Start(holder.Port());
    CHECK(receiver.IsRetrying());

    ContinuousSender tracker;
    tracker.Start(holder.Port());
    // Two retry intervals of the tracker talking to a port nobody is on.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    CHECK_MSG(!receiver.GetRotation(yaw, pitch, roll),
              "no pose may reach the game while the port is still held");

    holder.Close();

    const double poseMs = WaitFor(
        [&receiver] {
            float y = 0.0f, p = 0.0f, r = 0.0f;
            return receiver.GetRotation(y, p, r);
        },
        5000.0);
    std::printf("  port freed -> first pose, tracker already sending: %.0f ms\n", poseMs);
    CHECK_MSG(poseMs >= 0.0, "a running tracker never reached the reclaimed socket");
    // The bind retry dominates: one interval plus a supervisor tick, then the
    // next datagram of a 60Hz feed. Anything approaching a second here would be
    // a player sitting looking at a frozen view wondering whether to relaunch.
    CHECK_MSG(poseMs >= 0.0 && poseMs < 1500.0,
              "the view must start moving within a second or so of the port coming free");
    CHECK(receiver.IsRunning());
    CHECK(receiver.IsReceiving());

    tracker.Stop();
    receiver.Stop();
}

}  // namespace

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::printf("FAIL WSAStartup\n");
        return 1;
    }
#endif

    TestHeldPortIsReclaimedAndDeliversPoses();
    TestFreedPortToFirstPoseWithTrackerAlreadyRunning();
    TestRetryKeepsItsCadenceOverManyIntervals();
    TestStopWhileWaitingReturnsPromptly();

    const int result = gr_test::Report();
#ifdef _WIN32
    WSACleanup();
#endif
    return result;
}
