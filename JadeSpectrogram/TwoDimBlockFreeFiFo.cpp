#include <memory.h>
#include <cassert>
#include "TwoDimBlockFreeFiFo.h"

TwoDimBlockFreeFiFO::TwoDimBlockFreeFiFO()
:m_initValue(0.f)
{
    m_maxCapacity_x = static_cast<int>(10*96000/128) + 1; // 10s * 96000 Hz / smallest feed (25% with 512)
    m_maxCapacity_y = 16384;
    buildMem();
}

TwoDimBlockFreeFiFO::TwoDimBlockFreeFiFO(size_t max_x, size_t max_y)
:m_maxCapacity_x(max_x), m_maxCapacity_y(max_y), m_initValue(0.f)
{ 
    buildMem();
}

bool TwoDimBlockFreeFiFO::push(const std::vector<float> &inBlock, const SliceInfo& info)
{
    assert(inBlock.size() <= m_actSize_y);
    const size_t write = m_writecounter.load(std::memory_order_relaxed); // only this thread writes it
    const size_t next = nextIndex(write);

    // Full: the reader is too slow or not reading at all (e.g. editor closed).
    // Drop the new slice: overwriting the oldest one would mean the producer moves
    // m_readcounter, which races with a pop() copying out of that very slot.
    if (next == m_readcounter.load(std::memory_order_acquire))
        return false;

    memcpy(m_Mem[write].data(), inBlock.data(), inBlock.size()*sizeof(float));
    m_sliceSize[write] = inBlock.size();
    m_sliceInfo[write] = info;
    m_writecounter.store(next, std::memory_order_release); // publish the slice
    return true;
}

bool TwoDimBlockFreeFiFO::pop(std::vector<float> &outBlock)
{
    const size_t read = m_readcounter.load(std::memory_order_relaxed); // only this thread writes it
    if (read == m_writecounter.load(std::memory_order_acquire)) // reading is faster than writing
        return false;

    assert(outBlock.size() == m_sliceSize[read]);
    memcpy(outBlock.data(), m_Mem[read].data(), m_sliceSize[read]*sizeof(float));
    m_readcounter.store(nextIndex(read), std::memory_order_release); // hand the slot back to the writer
    return true;
}

size_t TwoDimBlockFreeFiFO::getNextSliceSize() const
{
    const size_t read = m_readcounter.load(std::memory_order_relaxed);
    if (read == m_writecounter.load(std::memory_order_acquire))
        return 0;
    return m_sliceSize[read];
}

SliceInfo TwoDimBlockFreeFiFO::getNextSliceInfo() const
{
    const size_t read = m_readcounter.load(std::memory_order_relaxed);
    if (read == m_writecounter.load(std::memory_order_acquire))
        return {};
    return m_sliceInfo[read];
}

size_t TwoDimBlockFreeFiFO::getNumAvailableToRead() const
{
    const size_t write = m_writecounter.load(std::memory_order_acquire);
    const size_t read = m_readcounter.load(std::memory_order_relaxed);
    if (write >= read)
        return write - read;
    return m_actSize_x - read + write;
}

bool TwoDimBlockFreeFiFO::getBlock(std::vector<std::vector<float>> &outBlock) 
{
    for (auto &slice : outBlock)
    {
        if (!pop(slice))
            return false;
    }
    return true;
}

bool TwoDimBlockFreeFiFO::setActSize(size_t act_x, size_t act_y)
{
    m_actSize_x = act_x;
    m_writecounter.store(0); // old counters may lie outside the new size
    m_readcounter.store(0);
    m_Mem.resize(m_actSize_x);
    m_sliceSize.assign(m_actSize_x, act_y);
    m_sliceInfo.assign(m_actSize_x, SliceInfo{});
    m_actSize_y = act_y;
    for (size_t kk = 0; kk < m_actSize_x ; kk++)
    {
        m_Mem[kk].resize(m_actSize_y);
    }
    return true;
}

void TwoDimBlockFreeFiFO::fill(float value)
{
    size_t x_size = m_Mem.size();
    for (size_t kk = 0; kk < x_size ; kk++)
    {
        std::fill(m_Mem[kk].begin(), m_Mem[kk].end(),value);
    }
}

bool TwoDimBlockFreeFiFO::buildMem()
{
    m_Mem.resize(m_maxCapacity_x);
    assert(m_Mem.size() == m_maxCapacity_x);

    for (size_t kk = 0; kk < m_maxCapacity_x ; kk++)
    {
        m_Mem[kk].resize(m_maxCapacity_y);
        assert(m_Mem[kk].size() == m_maxCapacity_y);
    }

    return true;
}

void TwoDimBlockFreeFiFO::clearMem()
{
    fill(m_initValue);
}
