#pragma once
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

// Slider with two thumbs (TwoValueVertical / TwoValueHorizontal). The thumbs cannot cross.
// In addition to the normal thumb dragging, a drag that starts between the two thumbs moves the
// whole range and keeps its width.
class RangeSlider : public juce::Slider
{
public:
    explicit RangeSlider(bool vertical)
        : juce::Slider(vertical ? juce::Slider::TwoValueVertical : juce::Slider::TwoValueHorizontal,
                       juce::Slider::NoTextBox)
    {
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const float p = position(e);
        const float pMin = static_cast<float>(getPositionOfValue(getMinValue()));
        const float pMax = static_cast<float>(getPositionOfValue(getMaxValue()));
        const float lo = std::min(pMin, pMax), hi = std::max(pMin, pMax);
        // near a thumb (inside the range) still grabs the thumb; the drawn thumbs lie outside the range
        const float grab = juce::jmax(6.f, 0.3f*static_cast<float>(isVertical() ? getWidth() : getHeight()));
        if (isEnabled() && p > lo + grab && p < hi - grab)
        {
            m_rangeDrag = true;
            m_startPos = p;
            m_startMin = valueToProportionOfLength(getMinValue());
            m_startMax = valueToProportionOfLength(getMaxValue());
            // proportion per pixel (sign: direction of the slider); in proportions the drag keeps the
            // width in slider positions, i.e. the ratio on a log scale and the width on a linear one
            m_valuePerPixel = (m_startMax - m_startMin)/static_cast<double>(pMax - pMin);
            if (onDragStart)
                onDragStart();
            return;
        }
        juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!m_rangeDrag)
        {
            juce::Slider::mouseDrag(e);
            return;
        }
        double delta = static_cast<double>(position(e) - m_startPos)*m_valuePerPixel;
        delta = juce::jlimit(-m_startMin, 1.0 - m_startMax, delta);
        setMinAndMaxValues(proportionOfLengthToValue(m_startMin + delta), proportionOfLengthToValue(m_startMax + delta),
                           juce::sendNotificationSync);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!m_rangeDrag)
        {
            juce::Slider::mouseUp(e);
            return;
        }
        m_rangeDrag = false;
        if (onDragEnd)
            onDragEnd();
    }

private:
    float position(const juce::MouseEvent& e) const { return isVertical() ? e.position.y : e.position.x; }
    bool m_rangeDrag = false;
    float m_startPos = 0.f;
    double m_startMin = 0.0, m_startMax = 0.0, m_valuePerPixel = 0.0; // proportions of the slider length
};

// Connects a RangeSlider to two parameters (lower and upper end of the range). Dragging writes the
// parameters (with host gestures); update() brings parameter changes (restored project, preset,
// automation) to the thumbs, except during a drag. Call update() regularly (timer, message thread).
class RangeParameterBinding
{
public:
    // toParameter / fromParameter: optional conversion between slider value and parameter value
    // (e.g. slider in Hz, parameter in log(Hz))
    RangeParameterBinding(RangeSlider& slider, juce::AudioProcessorValueTreeState& vts,
                          const juce::String& minID, const juce::String& maxID,
                          std::function<double(double)> toParameter = {}, std::function<double(double)> fromParameter = {})
        : m_slider(slider), m_toParameter(std::move(toParameter)), m_fromParameter(std::move(fromParameter)),
          m_minParam(dynamic_cast<juce::RangedAudioParameter*>(vts.getParameter(minID))),
          m_maxParam(dynamic_cast<juce::RangedAudioParameter*>(vts.getParameter(maxID))),
          m_minRaw(vts.getRawParameterValue(minID)),
          m_maxRaw(vts.getRawParameterValue(maxID))
    {
        jassert(m_minParam != nullptr && m_maxParam != nullptr);
        m_slider.onDragStart = [this]
        {
            m_dragging = true;
            m_dragStartMin = m_slider.getMinValue();
            m_dragStartMax = m_slider.getMaxValue();
            m_minParam->beginChangeGesture();
            m_maxParam->beginChangeGesture();
        };
        m_slider.onDragEnd = [this]
        {
            // JUCE reports thumb drags asynchronously (sendNotificationAsync): the last change may
            // not have arrived yet, so write the final values here, before comparing them
            write();
            m_minParam->endChangeGesture();
            m_maxParam->endChangeGesture();
            m_dragging = false;
            update(); // the parameters may have clamped the values
        };
        m_slider.onValueChange = [this]
        {
            if (m_updating)
                return;
            write();
            if (onChange)
                onChange();
        };
        update();
    }

    ~RangeParameterBinding()
    {
        m_slider.onDragStart = nullptr;
        m_slider.onDragEnd = nullptr;
        m_slider.onValueChange = nullptr;
    }

    void update()
    {
        if (m_dragging)
            return;
        const double mn = fromParameter(m_minRaw->load()), mx = fromParameter(m_maxRaw->load());
        if (!juce::approximatelyEqual(m_slider.getMinValue(), mn) || !juce::approximatelyEqual(m_slider.getMaxValue(), mx))
        {
            const juce::ScopedValueSetter<bool> updating(m_updating, true);
            m_slider.setMinAndMaxValues(mn, mx, juce::dontSendNotification);
        }
    }

    std::function<void()> onChange; // after a user change of the range

private:
    void write()
    {
        // during a drag only the end(s) the user moved; the other keeps an external change
        // (e.g. automation of the other parameter while one thumb is held)
        const bool gesture = !m_dragging; // e.g. keyboard or mouse wheel: one gesture per change
        const bool writeMin = !m_dragging || !juce::approximatelyEqual(m_slider.getMinValue(), m_dragStartMin);
        const bool writeMax = !m_dragging || !juce::approximatelyEqual(m_slider.getMaxValue(), m_dragStartMax);
        if (gesture) { m_minParam->beginChangeGesture(); m_maxParam->beginChangeGesture(); }
        if (writeMin)
            m_minParam->setValueNotifyingHost(m_minParam->convertTo0to1(static_cast<float>(toParameter(m_slider.getMinValue()))));
        if (writeMax)
            m_maxParam->setValueNotifyingHost(m_maxParam->convertTo0to1(static_cast<float>(toParameter(m_slider.getMaxValue()))));
        if (gesture) { m_minParam->endChangeGesture(); m_maxParam->endChangeGesture(); }
    }

    double toParameter(double v) const { return m_toParameter ? m_toParameter(v) : v; }
    double fromParameter(double v) const { return m_fromParameter ? m_fromParameter(v) : v; }

    RangeSlider& m_slider;
    std::function<double(double)> m_toParameter, m_fromParameter;
    juce::RangedAudioParameter* m_minParam;
    juce::RangedAudioParameter* m_maxParam;
    std::atomic<float>* m_minRaw;
    std::atomic<float>* m_maxRaw;
    bool m_dragging = false;
    bool m_updating = false;
    double m_dragStartMin = 0.0, m_dragStartMax = 0.0;
};

// Connects a normal slider to one parameter without taking over the parameter's range, so the
// slider can have its own scale (setNormalisableRange). Drags write the parameter inside one host
// gesture (also the final value, since JUCE may report drags asynchronously); update() brings
// parameter changes to the slider, except during a drag. Call update() regularly (timer).
class SliderParameterBinding
{
public:
    SliderParameterBinding(juce::Slider& slider, juce::AudioProcessorValueTreeState& vts, const juce::String& id)
        : m_slider(slider), m_param(dynamic_cast<juce::RangedAudioParameter*>(vts.getParameter(id))),
          m_raw(vts.getRawParameterValue(id))
    {
        jassert(m_param != nullptr);
        m_slider.onDragStart = [this] { m_dragging = true; m_param->beginChangeGesture(); };
        m_slider.onDragEnd = [this] { write(); m_param->endChangeGesture(); m_dragging = false; update(); };
        m_slider.onValueChange = [this]
        {
            if (m_updating)
                return;
            if (m_dragging)
                write();
            else
            {
                m_param->beginChangeGesture();
                write();
                m_param->endChangeGesture();
            }
        };
        update();
    }
    ~SliderParameterBinding()
    {
        m_slider.onDragStart = nullptr;
        m_slider.onDragEnd = nullptr;
        m_slider.onValueChange = nullptr;
    }
    void update()
    {
        if (m_dragging)
            return;
        const double v = m_raw->load();
        if (!juce::approximatelyEqual(m_slider.getValue(), v))
        {
            const juce::ScopedValueSetter<bool> updating(m_updating, true);
            m_slider.setValue(v, juce::dontSendNotification);
        }
    }

private:
    void write() { m_param->setValueNotifyingHost(m_param->convertTo0to1(static_cast<float>(m_slider.getValue()))); }
    juce::Slider& m_slider;
    juce::RangedAudioParameter* m_param;
    std::atomic<float>* m_raw;
    bool m_dragging = false;
    bool m_updating = false;
};
