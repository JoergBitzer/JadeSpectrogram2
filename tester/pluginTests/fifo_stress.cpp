// Lock-free FIFO between audio and GUI: no torn or reordered slices (producer and consumer thread).
#include "TwoDimBlockFreeFiFo.h"
#include <thread>
#include <cstdio>
#include <cstdlib>
// Producer pushes slices whose every element equals a running sequence number.
// Consumer checks each popped slice is uniform (no torn slice) and numbers increase.
int main()
{
    const size_t X = 64, Y = 1025, N = 2000000;
    TwoDimBlockFreeFiFO f(X, Y);
    f.setActSize(X - 1, Y);
    f.reset();
    size_t dropped = 0, received = 0; bool bad = false;
    std::thread prod([&]{
        std::vector<float> s(Y);
        for (size_t n = 1; n <= N; ++n) { std::fill(s.begin(), s.end(), float(n % 8000000)); if (!f.push(s)) ++dropped; }
    });
    std::thread cons([&]{
        std::vector<float> s(Y); float last = 0.f; size_t idle = 0;
        while (idle < 50000000) {
            if (!f.pop(s)) { ++idle; continue; }
            idle = 0; ++received;
            for (float v : s) if (v != s[0]) { bad = true; break; }
            if (s[0] <= last) bad = true;
            last = s[0];
            if (s[0] == float(N % 8000000)) break;
        }
    });
    prod.join(); cons.join();
    std::printf("received %zu dropped %zu sum %zu %s\n", received, dropped, received + dropped, bad ? "CORRUPT" : "OK");
    return bad;
}
