#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// A number display that can be changed with the mouse, like the tempo field of a DAW: drag up/down
// (Shift: fine steps), mouse wheel, or double-click to type a value. While editable it shows two
// small triangles on the right and highlights on hover; while not editable it is a plain text
// display (setText). onDragStart / onValueChange / onDragEnd frame the changes of one gesture; a
// wheel step or a typed value is one gesture of its own (start, change, end).
class DragValueBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragValueBox() { setRepaintsOnMouseActivity(true); }

    void setRange(double minValue, double maxValue, double step) { m_min = minValue; m_max = maxValue; m_step = step; }
    // value change per pixel of drag, normal and with Shift
    void setDragSteps(double perPixel, double perPixelFine) { m_perPixel = perPixel; m_perPixelFine = perPixelFine; }
    void setEditable(bool editable)
    {
        if (editable == m_editable)
            return;
        m_editable = editable;
        setMouseCursor(editable ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        if (!editable)
            m_editor.reset();
        repaint();
    }
    bool isEditable() const { return m_editable; }

    // shown text (the owner formats the value, e.g. "120 BPM" or "no tempo")
    void setText(const juce::String& text)
    {
        if (text != m_text)
        {
            m_text = text;
            repaint();
        }
    }
    const juce::String& getText() const { return m_text; }

    double getValue() const { return m_value; }
    // no callbacks; does not change the value during a drag (the drag owns it)
    void setValue(double v)
    {
        if (!m_dragging)
            m_value = constrain(v);
    }

    void setFont(const juce::FontOptions& font) { m_font = font; repaint(); }
    void setTextColour(juce::Colour c) { m_textColour = c; repaint(); }

    std::function<void()> onDragStart, onValueChange, onDragEnd;

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (m_editable && (isMouseOverOrDragging() || m_dragging))
        {
            g.setColour(m_textColour.withAlpha(0.15f));
            g.fillRoundedRectangle(r, 3.f);
        }
        float textRight = r.getRight();
        if (m_editable)
        {
            // two small triangles on the right: up and down
            const float tw = juce::jmax(4.f, 0.28f*r.getHeight()), th = 0.6f*tw, cx = r.getRight() - 0.6f*tw - 2.f, cy = r.getCentreY();
            juce::Path p;
            p.addTriangle(cx - 0.5f*tw, cy - 1.f, cx + 0.5f*tw, cy - 1.f, cx, cy - 1.f - th);
            p.addTriangle(cx - 0.5f*tw, cy + 1.f, cx + 0.5f*tw, cy + 1.f, cx, cy + 1.f + th);
            g.setColour(m_textColour);
            g.fillPath(p);
            textRight = cx - 0.5f*tw - 2.f;
        }
        g.setColour(m_textColour);
        g.setFont(m_font);
        g.drawText(m_text, r.withRight(textRight).withTrimmedLeft(2.f), juce::Justification::centredLeft, false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!m_editable || e.getNumberOfClicks() > 1)
            return;
        m_dragging = true;
        m_dragStartValue = m_value;
        m_dragStartY = e.position.y;
        m_lastShift = e.mods.isShiftDown();
        if (onDragStart)
            onDragStart();
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!m_dragging)
            return;
        // switching Shift during the drag keeps the value continuous
        if (e.mods.isShiftDown() != m_lastShift)
        {
            m_lastShift = e.mods.isShiftDown();
            m_dragStartValue = m_value;
            m_dragStartY = e.position.y;
        }
        const double perPixel = m_lastShift ? m_perPixelFine : m_perPixel;
        change(m_dragStartValue + static_cast<double>(m_dragStartY - e.position.y)*perPixel);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (!m_dragging)
            return;
        m_dragging = false;
        if (onDragEnd)
            onDragEnd();
        repaint();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (!m_editable || m_dragging || juce::approximatelyEqual(wheel.deltaY, 0.f))
        {
            juce::Component::mouseWheelMove(e, wheel);
            return;
        }
        const double step = e.mods.isShiftDown() ? m_step : 10.0*m_step;
        gesture(m_value + (wheel.deltaY > 0.f ? step : -step));
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (!m_editable)
            return;
        m_editor = std::make_unique<juce::TextEditor>();
        addAndMakeVisible(*m_editor);
        m_editor->setBounds(getLocalBounds());
        m_editor->setFont(m_font);
        m_editor->setInputRestrictions(7, "0123456789.,");
        m_editor->setText(juce::String(m_value, 1), false);
        m_editor->selectAll();
        m_editor->onReturnKey = [this] { commitText(); };
        m_editor->onFocusLost = [this] { commitText(); };
        m_editor->onEscapeKey = [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<DragValueBox>(this)]
                                                                       { if (safe) safe->m_editor.reset(); }); };
        m_editor->grabKeyboardFocus();
    }

private:
    double constrain(double v) const
    {
        v = juce::jlimit(m_min, m_max, v);
        return m_step > 0.0 ? juce::jlimit(m_min, m_max, m_min + std::round((v - m_min)/m_step)*m_step) : v;
    }
    void change(double v)
    {
        v = constrain(v);
        if (juce::approximatelyEqual(v, m_value))
            return;
        m_value = v;
        if (onValueChange)
            onValueChange();
    }
    void gesture(double v)
    {
        if (onDragStart)
            onDragStart();
        change(v);
        if (onDragEnd)
            onDragEnd();
    }
    void commitText()
    {
        if (m_editor == nullptr)
            return;
        m_editor->onReturnKey = nullptr; // only once (return, then focus lost)
        m_editor->onFocusLost = nullptr;
        const auto text = m_editor->getText().replaceCharacter(',', '.').trim();
        // the editor must not be deleted inside its own callback
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<DragValueBox>(this)] { if (safe) safe->m_editor.reset(); });
        if (text.isNotEmpty())
            gesture(text.getDoubleValue());
    }

    double m_min = 0.0, m_max = 1.0, m_step = 0.0, m_value = 0.0;
    double m_perPixel = 0.01, m_perPixelFine = 0.001;
    bool m_editable = false, m_dragging = false, m_lastShift = false;
    double m_dragStartValue = 0.0;
    float m_dragStartY = 0.f;
    juce::String m_text;
    juce::FontOptions m_font {12.f};
    juce::Colour m_textColour {juce::Colours::grey};
    std::unique_ptr<juce::TextEditor> m_editor;
};
