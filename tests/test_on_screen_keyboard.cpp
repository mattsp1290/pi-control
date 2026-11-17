#include "keyboard/on_screen_keyboard.h"

#include <QApplication>
#include <QLineEdit>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

using namespace pi_control::keyboard;

class KeyboardTests : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QWidget> window_;
    QLineEdit* field_ = nullptr;
    OnScreenKeyboard* keyboard_ = nullptr;

    void tap(const QString& name) {
        QToolButton* key = keyboard_->key(name);
        QVERIFY2(key, qPrintable(name));
        QTest::mouseClick(key, Qt::LeftButton);
        QCOMPARE(QApplication::focusWidget(), field_);
    }

private slots:
    void init() {
        window_ = std::make_unique<QWidget>();
        window_->resize(800, 480);
        auto* layout = new QVBoxLayout(window_.get());
        field_ = new QLineEdit;
        keyboard_ = new OnScreenKeyboard;
        layout->addWidget(field_);
        layout->addStretch();
        layout->addWidget(keyboard_);
        window_->show();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        field_->setFocus();
        QCOMPARE(QApplication::focusWidget(), field_);
    }

    void cleanup() { window_.reset(); }

    void typesIntoFocusedField() {
        tap("h");
        tap("i");
        QCOMPARE(field_->text(), QString("hi"));
    }

    void shiftOnceThenLock() {
        tap("Shift");
        tap("a");
        tap("b");
        QCOMPARE(field_->text(), QString("Ab"));
        field_->clear();
        tap("Shift");
        tap("Shift");
        QCOMPARE(keyboard_->shift(), OnScreenKeyboard::Shift::Locked);
        tap("a");
        tap("b");
        QCOMPARE(field_->text(), QString("AB"));
        tap("Shift");
        QCOMPARE(keyboard_->shift(), OnScreenKeyboard::Shift::Off);
    }

    void symbolLayouts() {
        tap("?123");
        QCOMPARE(keyboard_->layout(), OnScreenKeyboard::Layout::Symbols);
        tap("1");
        tap("#+=");
        tap("~");
        QCOMPARE(field_->text(), QString("1~"));
        tap("ABC");
        QCOMPARE(keyboard_->layout(), OnScreenKeyboard::Layout::Letters);
        tap("Space");
        QCOMPARE(field_->text(), QString("1~ "));
    }

    void everyCharacterKeyIsPrintableAscii() {
        int checked = 0;
        for (QToolButton* key : keyboard_->keys()) {
            const QString name = key->accessibleName();
            if (name.size() != 1) continue; // Modifiers and named keys.
            const ushort code = name.at(0).unicode();
            QVERIFY2(code >= 0x20 && code <= 0x7E, qPrintable(name));
            field_->clear();
            key->click();
            QCOMPARE(field_->text(), name);
            ++checked;
        }
        QVERIFY(checked >= 26 + 10 + 20);
    }

    void backspaceAndRepeat() {
        field_->setText("abcdefghij");
        tap("Backspace");
        QCOMPARE(field_->text(), QString("abcdefghi"));
        QToolButton* backspace = keyboard_->key("Backspace");
        QTest::mousePress(backspace, Qt::LeftButton);
        QTest::qWait(700);
        QTest::mouseRelease(backspace, Qt::LeftButton);
        QVERIFY2(field_->text().size() <= 6, qPrintable(field_->text()));
        QCOMPARE(QApplication::focusWidget(), field_);
    }

    void noFocusWidgetIsHarmless() {
        field_->clearFocus();
        QCOMPARE(QApplication::focusWidget(), nullptr);
        keyboard_->key("a")->click();
        QCOMPARE(field_->text(), QString());
    }

    void touchSized() {
        QCOMPARE(keyboard_->height(), kKeyboardHeight);
        QVERIFY(keyboard_->height() <= 242);
        for (QToolButton* key : keyboard_->keys()) {
            if (!key->isVisible()) continue;
            QVERIFY2(key->width() >= 60 && key->height() >= 48,
                     qPrintable(QString("%1 %2x%3").arg(key->text()).arg(key->width()).arg(key->height())));
        }
    }
};

QTEST_MAIN(KeyboardTests)
#include "test_on_screen_keyboard.moc"
