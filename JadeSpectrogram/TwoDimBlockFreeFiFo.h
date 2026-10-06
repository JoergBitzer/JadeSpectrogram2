#pragma once
#include <vector>
#include <atomic>
#include <memory.h>

// Single-producer / single-consumer FIFO of slices with up to act_y elements.
// Each slot remembers the size of the slice pushed into it, so the slice size may change
// while running (e.g. new FFT size) without resetting the FIFO.
// push() must only be called by one thread (audio), pop()/getBlock()/getNumAvailableToRead()
// only by one other thread (GUI). The producer is the only one writing m_writecounter,
// the consumer the only one writing m_readcounter; release/acquire on these counters makes
// the slice data (and its size) visible before the counter that publishes it.
// The setters, reset() and fill() are NOT thread-safe: call them only while neither
// push() nor pop() can run (e.g. prepareToPlay or while the audio thread is locked out).
// information that travels with each slice (written by the producer with the slice)
struct SliceInfo
{
    size_t hop = 0; // samples between this slice and the previous one
    // musical position of the end of the slice (from the host); hasPpq = false: no host tempo
    bool hasPpq = false;
    double ppq = 0.0;          // position in quarter notes
    double barStartPpq = 0.0;  // a bar start (bar lines at barStartPpq + k*barLengthPpq)
    float barLengthPpq = 4.f;  // numerator * 4 / denominator
    float beatPpq = 1.f;       // 4 / denominator
    float bpm = 0.f;
    long long endSample = 0;   // sample count (since prepareToPlay) at the end of the slice
    // a new section of the musical time starts inside this slice (start of play, locate, loop
    // jump): its position; the positions from there to ppq belong to this slice
    bool hasSegmentStart = false;
    double segmentStartPpq = 0.0;
};

class TwoDimBlockFreeFiFO
{
public:
    TwoDimBlockFreeFiFO();
    TwoDimBlockFreeFiFO(size_t max_x, size_t max_y);

    // processing
    bool push(const std::vector <float> & inBlock, const SliceInfo& info = {}); // one xSlice in each call, false if full (slice dropped)
    bool pop(      std::vector <float> & outBlock); // one xSlice in each call, false if empty; outBlock.size() must be getNextSliceSize()
    size_t getNextSliceSize() const; // consumer only: size of the slice pop() returns next, 0 if empty
    SliceInfo getNextSliceInfo() const; // consumer only: info of the slice pop() returns next (default if empty)
    size_t getNumAvailableToRead() const;
    bool getBlock(std::vector <std::vector <float> > & outBlock);

    // setter
    bool setMaxCapacity(size_t max_x, size_t max_y)
    {m_maxCapacity_x = max_x; m_maxCapacity_y = max_y; return buildMem();};
    bool setActSize(size_t act_x, size_t act_y);
    void reset(){m_writecounter.store(0); m_readcounter.store(0); clearMem(); };
    void fill(float value); // fill the whole memory with a value
    void setInitValue(float value){ m_initValue = value; }; // set the initial value for each element
private:
    std::vector<std::vector <float> > m_Mem;
    std::vector<size_t> m_sliceSize; // number of valid elements in each slot
    std::vector<SliceInfo> m_sliceInfo;
    size_t m_maxCapacity_x = 0;
    size_t m_maxCapacity_y = 0;
    size_t m_actSize_x = 0;
    size_t m_actSize_y = 0;

    // read == write: empty; next(write) == read: full (one slot always stays unused)
    std::atomic<size_t> m_readcounter {0};
    std::atomic<size_t> m_writecounter {0};
    //size_t m_elemSizeInBytes = 4;
    float m_initValue = 0.0f;

    size_t nextIndex(size_t index) const {return (index + 1 == m_actSize_x) ? 0 : index + 1;};
    bool buildMem();
    void clearMem();
};
