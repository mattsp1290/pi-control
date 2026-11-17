#pragma once

#include <QElapsedTimer>
#include <QWidget>

#include <vector>

class QHBoxLayout;
class QStackedWidget;
class QToolButton;

namespace pi_control::keyboard {

inline constexpr int kKeyHeight = 52;
inline constexpr int kRowSpacing = 6;
inline constexpr int kVerticalMargin = 8;
inline constexpr int kKeyboardHeight = 4 * kKeyHeight + 3 * kRowSpacing + 2 * kVerticalMargin; // 242 px.
inline constexpr int kDoubleTapMs = 400;

// A touch keyboard for ASCII text. Keys never take focus: each tap sends key
// events to the focused widget.
class OnScreenKeyboard final : public QWidget {
    Q_OBJECT

public:
    enum class Layout { Letters, Symbols, More };
    enum class Shift { Off, Once, Locked };

    explicit OnScreenKeyboard(QWidget* parent = nullptr);

    Layout layout() const { return layout_; }
    Shift shift() const { return shift_; }
    // Every key of every layout, including hidden layouts.
    const std::vector<QToolButton*>& keys() const { return keys_; }
    // The visible key whose label or accessible name is name, or nullptr.
    QToolButton* key(const QString& name) const;

signals:
    void doneClicked();

private:
    QWidget* buildLayout(Layout layout);
    QToolButton* addKey(QHBoxLayout* row, const QString& label, int stretch = 1);
    QToolButton* addCharacterKey(QHBoxLayout* row, QChar character);
    void typeCharacter(QChar character);
    void sendKey(int key, const QString& text);
    void tapShift();
    void setShift(Shift shift);
    void showLayout(Layout layout);

    QStackedWidget* pages_;
    Layout layout_ = Layout::Letters;
    Shift shift_ = Shift::Off;
    QElapsedTimer lastShiftTap_;
    std::vector<QToolButton*> keys_;
    std::vector<QToolButton*> letterKeys_;
    std::vector<QToolButton*> shiftKeys_;
};

} // namespace pi_control::keyboard
