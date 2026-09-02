#pragma once

#include <QLabel>
#include <QResizeEvent>

// A word-wrapping QLabel whose containing layout actually gives it enough
// vertical space. Plain QLabel::setWordWrap(true) alone isn't sufficient
// inside QFormLayout (and some QVBoxLayout nestings, e.g. inside a
// QScrollArea's resizable widget): the row height gets taken from
// sizeHint() computed before the label's final width is known, so
// multi-line text silently overlaps whatever comes after it instead of
// wrapping the layout to fit. Forcing minimumHeight to heightForWidth() on
// every resize closes that gap without every call site having to
// hand-tune a fixed height (which would also need re-tuning per
// UiScale/font).
class WrappedLabel : public QLabel {
public:
    explicit WrappedLabel(QWidget* parent = nullptr) : QLabel(parent) { setWordWrap(true); }
    explicit WrappedLabel(const QString& text, QWidget* parent = nullptr) : QLabel(text, parent) {
        setWordWrap(true);
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        setMinimumHeight(heightForWidth(width()));
    }
};
