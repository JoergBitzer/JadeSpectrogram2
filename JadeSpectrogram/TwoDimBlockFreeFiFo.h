#include <vector>
#include <memory.h>

class TwoDimBlockFreeFiFO
{
public:
    TwoDimBlockFreeFiFO();
    TwoDimBlockFreeFiFO(size_t max_x, size_t max_y){ setMaxCapacity(max_x, max_y); };

    // processing
    bool push(const std::vector <float> & inBlock); // one xSlice in each call
    bool pop(      std::vector <float> & outBlock); // one xSlice in each call
    int getNumAvailableToRead() const;
    bool getBlock(std::vector <std::vector <float> > & outBlock) const;

    // setter
    bool setMaxCapacity(size_t max_x, size_t max_y)
    {m_maxCapacity_x = max_x; m_maxCapacity_y = max_y; return buildMem();};
    bool setActSize(size_t act_x, size_t act_y);
    void reset(){if (m_maxCapacity_x>1){m_writecounter = 1; m_readcounter = 0;}; clearMem(); };
    void fill(float value); // fill the whole memory with a value
    void setInitValue(float value){ m_initValue = value; }; // set the initial value for each element
private:
    std::vector<std::vector <float> > m_Mem;
    size_t m_maxCapacity_x = -1;
    size_t m_maxCapacity_y = -1;
    size_t m_actSize_x = -1;
    size_t m_actSize_y = -1;

    size_t m_readcounter = -1;
    size_t m_writecounter = -1;
    size_t m_elemSizeInBytes = 4;
    float m_initValue = 0.0f;

    bool buildMem(){};
    void clearMem(){};
};
