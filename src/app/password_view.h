#pragma once

#include "wifi/wifi_backend.h"

#include <QTimer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace pi_control::keyboard {
class OnScreenKeyboard;
}

namespace pi_control::app {

class ElidedLabel;

// 8-63 printable ASCII characters, or exactly 64 hex digits.
bool isValidPassphrase(const QString& text);

// Full-window password entry with the on-screen keyboard, shown in the
// shell's task layer so it needs no scrolling at 800x480.
class PasswordView final : public QWidget {
    Q_OBJECT

public:
    explicit PasswordView(QWidget* parent = nullptr);

    // Starts entry for network with an empty field. message, if any, explains
    // why the password is needed.
    void open(const wifi::WifiNetwork& network, const QString& message = {});
    const wifi::WifiNetwork& network() const { return network_; }
    bool isConnecting() const { return connecting_; }
    // Ends the busy state with error shown; the text stays, selected.
    void showError(const QString& error);
    // Clears the typed password.
    void clear();
    // Hides the password again (the screensaver started).
    void conceal();

    QString password() const;
    QLineEdit* field() const { return field_; }
    QPushButton* showButton() const { return show_; }
    QPushButton* connectButton() const { return connect_; }
    QPushButton* cancelButton() const { return cancel_; }
    QLabel* messageLabel() const { return message_; }
    keyboard::OnScreenKeyboard* keyboard() const { return keyboard_; }

signals:
    // The user asked to join with this password.
    void submitted(const QString& password);
    void cancelled();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void submit();
    void setConnecting(bool connecting);
    void updateState();
    void flashMessage();

    wifi::WifiNetwork network_;
    QString error_;
    QString reason_;
    bool connecting_ = false;
    QPushButton* cancel_;
    ElidedLabel* title_;
    QPushButton* connect_;
    QLineEdit* field_;
    QPushButton* show_;
    QLabel* message_;
    QProgressBar* busy_;
    keyboard::OnScreenKeyboard* keyboard_;
    QTimer flash_;
};

} // namespace pi_control::app
