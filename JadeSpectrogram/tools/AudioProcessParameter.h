/* 
    AudioProcessParameter.h
    Author: J. Bitzer @ TGM, Jade Hochschule
    Date: 2021-07-01
    Description: This class is used to handle parameter in JUCE AudioProcessor. 
    It can transform the parameter value to a different representation.
    Version 1.0 
    Version 1.1: changed variable names to be more descriptive
    Version 1.2: changed prepareParameter to prepareParameter(std::atomic<float>* parampointer) and m_param to be a float pointer
                    since the parameter is always a pointer to an atomic float
    License: MIT
*/
#pragma once
#include <vector>

namespace jade
{
template <class T> class AudioProcessParameter
{
public:
    enum transformerFunc
    {
        notransform,
        db2gaintransform,
        db2powtransform,
        sqrttransform,
        exptransform,
    };
    AudioProcessParameter(){
        changeTransformer(transformerFunc::notransform);
        
    };
    void prepareParameter(std::atomic<float>* parampointer) {m_param = parampointer;};
    T update(){
        if (*m_param != m_ParamOld)
        {
            m_ParamOld = *m_param;
            m_transformedParam =  m_transformParamFunc();
        }
        return static_cast<T> (m_transformedParam);
    };
    bool updateWithNotification(T& param){
        if (*m_param != m_ParamOld)
        {
            m_ParamOld = *m_param;
            m_transformedParam =  m_transformParamFunc();
            param = static_cast<T> (m_transformedParam);
            return true;
        }
        param = m_transformedParam;
        return false;
    };
    void changeTransformer(transformerFunc tf)
    {
        switch (tf)
        {
            case transformerFunc::notransform:
                m_transformParamFunc = [this](){return m_ParamOld;};
                break;
            case transformerFunc::db2gaintransform:
                m_transformParamFunc = [this](){return pow(10.0,m_ParamOld/20.0);};
                break;
            case transformerFunc::db2powtransform:
                m_transformParamFunc = [this](){return pow(10.0,m_ParamOld/10.0);};
                break;
            case transformerFunc::sqrttransform:
                m_transformParamFunc = [this](){return sqrt(m_ParamOld);};
                break;
            case transformerFunc::exptransform:
                m_transformParamFunc = [this](){return exp(m_ParamOld);};
                break;

            default:
                m_transformParamFunc = [this](){return m_ParamOld;};
                break;

        }
            
        
    }

private:
    std::atomic<float>* m_param = nullptr; 
    T m_ParamOld = std::numeric_limits<T>::min(); //smallest possible number, will change in the first block
    T m_transformedParam = std::numeric_limits<T>::min(); //smallest possible number, will change in the first block

    std::function<T(void)> m_transformParamFunc;

};
}