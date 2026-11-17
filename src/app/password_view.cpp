#include "app/password_view.h"

#include "app/elided_label.h"
#include "keyboard/on_screen_keyboard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pi_control::app {

namespace {

constexpr int kRowHeight = 56;
constexpr int kMessageHeight = 28;
constexpr int kMinLength = 8;
constexpr int kMaxLength = 63;
constexpr int kHexLength = 64;

bool isPrintableAscii(const QString& text) {
    return std::all_of(text.begin(), text.end(), [](QChar c) { return c.unicode() >= 0x20 && c.unicode() <= 0x7E; });
}

bool isHex(const QString& text) {
    return std::all_of(text.begin(), text.end(), [](QChar c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

} // namespace

bool isValidPassphrase(const QString& text) {
    if (!isPrintableAscii(text)) return false;
    if (text.size() == kHexLength) return isHex(text);
    return text.size() >= kMinLength && text.size() <= kMaxLength;
}

PasswordView::PasswordView(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground);
    cancel_ = new QPushButton("‹ Cancel");
    cancel_->setFixedHeight(kRowHeight);
    title_ = new ElidedLabel;
    title_->setObjectName("passwordTitle");
    title_->setStyleSheet("font-size: 24px; font-weight: bold;");
    title_->setAlignment(Qt::AlignCenter);
    connect_ = new QPushButton("Connect");
    connect_->setObjectName("connectButton");
    connect_->setFixedHeight(kRowHeight);
    connect_->setMinimumWidth(150);

    field_ = new QLineEdit;
    field_->setObjectName("passwordField");
    field_->setEchoMode(QLineEdit::Password);
    field_->setContextMenuPolicy(Qt::NoContextMenu);
    field_->setFixedHeight(kRowHeight);
    field_->setMaxLength(kHexLength);
    field_->setAccessibleName("Password");
    show_ = new QPushButton("Show");
    show_->setFixedHeight(kRowHeight);
    show_->setMinimumWidth(110);
    show_->setFocusPolicy(Qt::NoFocus);
    cancel_->setFocusPolicy(Qt::NoFocus);
    connect_->setFocusPolicy(Qt::NoFocus);

    message_ = new QLabel;
    message_->setObjectName("passwordMessage");
    message_->setFixedHeight(kMessageHeight);
    busy_ = new QProgressBar;
    busy_->setRange(0, 0);
    busy_->setFixedSize(80, 10);
    busy_->hide();

    keyboard_ = new keyboard::OnScreenKeyboard;

    auto* top = new QHBoxLayout;
    top->setSpacing(12);
    top->addWidget(cancel_);
    top->addWidget(title_, 1);
    top->addWidget(connect_);
    auto* entry = new QHBoxLayout;
    entry->setSpacing(12);
    entry->addWidget(field_, 1);
    entry->addWidget(show_);
    auto* messageRow = new QHBoxLayout;
    messageRow->setSpacing(12);
    messageRow->addWidget(message_, 1);
    messageRow->addWidget(busy_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    layout->addLayout(top);
    layout->addLayout(entry);
    layout->addLayout(messageRow);
    layout->addStretch();
    layout->addWidget(keyboard_);

    connect(cancel_, &QPushButton::clicked, this, [this] {
        clear();
        emit cancelled();
    });
    connect(connect_, &QPushButton::clicked, this, &PasswordView::submit);
    connect(keyboard_, &keyboard::OnScreenKeyboard::doneClicked, this, &PasswordView::submit);
    connect(field_, &QLineEdit::returnPressed, this, &PasswordView::submit);
    connect(show_, &QPushButton::clicked, this, [this] {
        const bool hidden = field_->echoMode() == QLineEdit::Password;
        field_->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        show_->setText(hidden ? "Hide" : "Show");
    });
    connect(field_, &QLineEdit::textChanged, this, [this] {
        if (!connecting_) error_.clear();
        updateState();
    });
    flash_.setSingleShot(true);
    flash_.setInterval(600);
    connect(&flash_, &QTimer::timeout, this, [this] { message_->setStyleSheet({}); });
    updateState();
}

void PasswordView::open(const wifi::WifiNetwork& network, const QString& message) {
    network_ = network;
    reason_ = message;
    error_.clear();
    setConnecting(false);
    title_->setText(QString("Join \"%1\"").arg(network.ssid));
    clear();
    conceal();
    updateState();
}

void PasswordView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    field_->setFocus();
}

void PasswordView::clear() {
    field_->clear();
    error_.clear();
    updateState();
}

void PasswordView::conceal() {
    field_->setEchoMode(QLineEdit::Password);
    show_->setText("Show");
}

void PasswordView::submit() {
    if (connecting_) return;
    if (!isValidPassphrase(field_->text())) {
        flashMessage();
        return;
    }
    error_.clear();
    reason_.clear();
    setConnecting(true);
    emit submitted(field_->text());
}

void PasswordView::showError(const QString& error) {
    error_ = error;
    setConnecting(false);
    field_->setFocus();
    field_->selectAll();
}

void PasswordView::setConnecting(bool connecting) {
    connecting_ = connecting;
    field_->setEnabled(!connecting);
    keyboard_->setEnabled(!connecting);
    show_->setEnabled(!connecting);
    busy_->setVisible(connecting);
    updateState();
}

void PasswordView::updateState() {
    const QString text = field_->text();
    connect_->setEnabled(!connecting_ && isValidPassphrase(text));
    connect_->setText(connecting_ ? "Connecting…" : "Connect");
    QString message;
    if (connecting_) message = "Connecting…";
    else if (!error_.isEmpty()) message = "⚠ " + error_;
    else if (!isPrintableAscii(text)) message = "⚠ Use only letters, numbers and symbols from the keyboard.";
    else if (!reason_.isEmpty() && text.isEmpty()) message = "⚠ " + reason_;
    else if (text.size() < kMinLength) message = "At least 8 characters";
    else if (text.size() == kHexLength && !isHex(text)) message = "A 64-character key uses only 0–9 and a–f.";
    message_->setText(message);
}

void PasswordView::flashMessage() {
    if (message_->text().isEmpty()) message_->setText("At least 8 characters");
    message_->setStyleSheet("color: #ffd166;");
    flash_.start();
}

} // namespace pi_control::app
