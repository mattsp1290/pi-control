#include "keyboard/on_screen_keyboard.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace pi_control::keyboard {

namespace {

constexpr auto kStyleSheet = R"(
QToolButton { background: #2c3b48; border: none; border-radius: 8px; color: #ffffff;
              font-size: 24px; min-height: 0px; padding: 0px; }
QToolButton:pressed { background: #4fa3e0; }
QToolButton#modifier { background: #1d2833; font-size: 20px; }
QToolButton#modifier:pressed { background: #4fa3e0; }
QToolButton#modifier[locked="true"] { background: #ffffff; color: #101820; }
QToolButton#modifier[armed="true"] { color: #4fa3e0; }
QToolButton#done { background: #2f7fbf; font-size: 20px; }
)";

const QString kShift = QStringLiteral("⇧");
const QString kBackspace = QStringLiteral("⌫");

QString rowText(OnScreenKeyboard::Layout layout, int row) {
    using Layout = OnScreenKeyboard::Layout;
    static const char* const letters[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const char* const symbols[] = {"1234567890", "-/:;()$&@\"", ".,?!'_"};
    static const char* const more[] = {"[]{}#%^*+=", "\\|~<>`'\",", ".,?!'_"};
    switch (layout) {
    case Layout::Letters: return QString::fromLatin1(letters[row]);
    case Layout::Symbols: return QString::fromLatin1(symbols[row]);
    case Layout::More: return QString::fromLatin1(more[row]);
    }
    return {};
}

} // namespace

OnScreenKeyboard::OnScreenKeyboard(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(kStyleSheet);
    setFixedHeight(kKeyboardHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    pages_ = new QStackedWidget;
    pages_->addWidget(buildLayout(Layout::Letters));
    pages_->addWidget(buildLayout(Layout::Symbols));
    pages_->addWidget(buildLayout(Layout::More));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, kVerticalMargin, 8, kVerticalMargin);
    layout->addWidget(pages_);
}

QToolButton* OnScreenKeyboard::addKey(QHBoxLayout* row, const QString& label, int stretch) {
    auto* key = new QToolButton;
    key->setText(label);
    key->setFocusPolicy(Qt::NoFocus);
    key->setFixedHeight(kKeyHeight);
    key->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    key->setMinimumWidth(40);
    row->addWidget(key, stretch);
    keys_.push_back(key);
    return key;
}

QToolButton* OnScreenKeyboard::addCharacterKey(QHBoxLayout* row, QChar character) {
    QToolButton* key = addKey(row, QString(character));
    key->setAccessibleName(QString(character));
    if (character.isLetter()) letterKeys_.push_back(key);
    connect(key, &QToolButton::clicked, this, [this, character] { typeCharacter(character); });
    return key;
}

QWidget* OnScreenKeyboard::buildLayout(Layout layout) {
    auto* page = new QWidget;
    auto* rows = new QVBoxLayout(page);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(kRowSpacing);
    for (int r = 0; r < 4; ++r) {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        rows->addLayout(row);
        if (r == 0) {
            for (const QChar c : rowText(layout, 0)) addCharacterKey(row, c);
        } else if (r == 1) {
            if (layout == Layout::Letters) row->addStretch(1); // Offset the shorter row.
            for (const QChar c : rowText(layout, 1)) addCharacterKey(row, c);
            if (layout == Layout::Letters) row->addStretch(1);
        } else if (r == 2) {
            QToolButton* modifier = nullptr;
            if (layout == Layout::Letters) {
                modifier = addKey(row, kShift, 2);
                modifier->setAccessibleName("Shift");
                shiftKeys_.push_back(modifier);
                connect(modifier, &QToolButton::clicked, this, &OnScreenKeyboard::tapShift);
            } else {
                const bool symbols = layout == Layout::Symbols;
                modifier = addKey(row, symbols ? "#+=" : "123", 2);
                connect(modifier, &QToolButton::clicked, this,
                        [this, symbols] { showLayout(symbols ? Layout::More : Layout::Symbols); });
            }
            modifier->setObjectName("modifier");
            for (const QChar c : rowText(layout, 2)) addCharacterKey(row, c);
            QToolButton* backspace = addKey(row, kBackspace, 2);
            backspace->setObjectName("modifier");
            backspace->setAccessibleName("Backspace");
            backspace->setAutoRepeat(true);
            backspace->setAutoRepeatDelay(500);
            backspace->setAutoRepeatInterval(80);
            connect(backspace, &QToolButton::clicked, this, [this] { sendKey(Qt::Key_Backspace, {}); });
        } else {
            const bool letters = layout == Layout::Letters;
            QToolButton* toggle = addKey(row, letters ? "?123" : "ABC", 3);
            toggle->setObjectName("modifier");
            connect(toggle, &QToolButton::clicked, this,
                    [this, letters] { showLayout(letters ? Layout::Symbols : Layout::Letters); });
            QToolButton* space = addKey(row, "space", 8);
            space->setObjectName("modifier");
            space->setAccessibleName("Space");
            connect(space, &QToolButton::clicked, this, [this] { typeCharacter(QChar(' ')); });
            row->setStretchFactor(addCharacterKey(row, QChar('.')), 2);
            QToolButton* done = addKey(row, "Done", 3);
            done->setObjectName("done");
            connect(done, &QToolButton::clicked, this, &OnScreenKeyboard::doneClicked);
        }
    }
    return page;
}

QToolButton* OnScreenKeyboard::key(const QString& name) const {
    QWidget* page = pages_->currentWidget();
    for (QToolButton* key : keys_) {
        if (!page->isAncestorOf(key)) continue;
        if (key->text() == name || key->accessibleName() == name) return key;
    }
    return nullptr;
}

void OnScreenKeyboard::typeCharacter(QChar character) {
    if (character.isLetter() && shift_ != Shift::Off) character = character.toUpper();
    // Qt key codes for printable ASCII equal the upper-case character code.
    sendKey(character.toUpper().unicode(), QString(character));
    if (character.isLetter() && shift_ == Shift::Once) setShift(Shift::Off);
}

void OnScreenKeyboard::sendKey(int key, const QString& text) {
    QWidget* target = QApplication::focusWidget();
    if (!target) return;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(target, &release);
}

void OnScreenKeyboard::tapShift() {
    const bool doubleTap = lastShiftTap_.isValid() && lastShiftTap_.elapsed() < kDoubleTapMs;
    lastShiftTap_.start();
    if (shift_ == Shift::Locked) setShift(Shift::Off);
    else if (doubleTap && shift_ == Shift::Once) setShift(Shift::Locked);
    else setShift(shift_ == Shift::Off ? Shift::Once : Shift::Off);
}

void OnScreenKeyboard::setShift(Shift shift) {
    shift_ = shift;
    for (QToolButton* key : letterKeys_) {
        key->setText(shift == Shift::Off ? key->text().toLower() : key->text().toUpper());
    }
    for (QToolButton* key : shiftKeys_) {
        key->setProperty("armed", shift == Shift::Once);
        key->setProperty("locked", shift == Shift::Locked);
        key->style()->unpolish(key);
        key->style()->polish(key);
    }
}

void OnScreenKeyboard::showLayout(Layout layout) {
    layout_ = layout;
    pages_->setCurrentIndex(int(layout));
}

} // namespace pi_control::keyboard
