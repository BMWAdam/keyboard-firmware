#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QObject>
#include <QStringList>
#include <QFile>
#include <QTextStream>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFontDatabase>
#include <QDir>
#include <QThread>

#ifndef DEFAULT_DARK_THEME
#define DEFAULT_DARK_THEME true
#endif

class SerialMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY availablePortsChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)
    Q_PROPERTY(bool isDarkTheme READ isDarkTheme WRITE setIsDarkTheme NOTIFY isDarkThemeChanged)

public:
    explicit SerialMonitor(QObject *parent = nullptr) : QObject(parent) {
        m_logText = "Welcome. Select a port and click Connect.\n";
        m_isDarkTheme = DEFAULT_DARK_THEME;
        refreshPorts();
    }

    QString logText() const { return m_logText; }
    QStringList availablePorts() const { return m_availablePorts; }
    bool isConnected() const { return m_serial.isOpen(); }
    
    // FIX: Theme Getter & Setter
    bool isDarkTheme() const { return m_isDarkTheme; }
    void setIsDarkTheme(bool dark) {
        if (m_isDarkTheme != dark) {
            m_isDarkTheme = dark;
            emit isDarkThemeChanged();
        }
    }

    Q_INVOKABLE void refreshPorts() {
        m_availablePorts.clear();
        for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
            QString name = info.portName();
            if (info.vendorIdentifier() == 0x2E8A)
                name += " (Pico)";
            m_availablePorts.append(name);
        }
        emit availablePortsChanged();
    }

    Q_INVOKABLE void connectToPort(const QString &portDisplayName) {
        if (m_serial.isOpen())
            m_serial.close();

        QString actualPortName = portDisplayName.split(" ").first();
        m_serial.setPortName(actualPortName);
        m_serial.setBaudRate(QSerialPort::Baud115200);

        if (m_serial.open(QIODevice::ReadWrite)) {
            m_logText += "✅ Connected to " + actualPortName + "\n";
            disconnect(&m_serial, &QSerialPort::readyRead, this, &SerialMonitor::readData);
            connect(&m_serial, &QSerialPort::readyRead, this, &SerialMonitor::readData);
        } else {
            m_logText += "❌ Failed to connect: " + m_serial.errorString() + "\n";
        }

        emit logTextChanged();
        emit isConnectedChanged();
    }

    Q_INVOKABLE void disconnectPort() {
        if (m_serial.isOpen()) {
            m_serial.close();
            m_logText += "🔌 Disconnected.\n";
            emit logTextChanged();
            emit isConnectedChanged();
        }
    }

    Q_INVOKABLE void sendCommand(const QString &command) {
        if (m_serial.isOpen() && m_serial.isWritable()) {
            m_serial.write(command.toUtf8() + "\r\n");
            m_logText += "> " + command + "\n";
        } else {
            m_logText += "❌ Cannot send: Not connected\n";
        }
        emit logTextChanged();
    }

    Q_INVOKABLE QString pickAndLoadConfig() {
        QString path = QFileDialog::getOpenFileName(
            nullptr,
            "Select configuration file",
            QDir::homePath(),
            "Config files (*.json *.yaml *.yml);;All files (*)"
        );
        if (!path.isEmpty())
            loadConfigFile(path);
        return path;
    }

    Q_INVOKABLE QString pickAndLoadUnderglowConfig() {
        QString path = QFileDialog::getOpenFileName(
            nullptr,
            "Select underglow configuration file",
            QDir::homePath(),
            "Config files (*.json *.yaml *.yml);;All files (*)"
        );
        if (!path.isEmpty())
            loadConfigUnderglowFile(path);
        return path;
    }

    Q_INVOKABLE void loadConfigFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_logText += "❌ Failed to open file: " + path + "\n";
            emit logTextChanged();
            return;
        }

        QString content = file.readAll();
        file.close();

        m_logText += "📄 Loaded config file (" + QString::number(content.size()) + " bytes)\n";
        emit logTextChanged();

        if (!m_serial.isOpen()) {
            m_logText += "❌ Cannot upload: Not connected\n";
            emit logTextChanged();
            return;
        }

        m_serial.write("CONFIG_BEGIN\n");
        m_serial.waitForBytesWritten(100);
        QThread::msleep(50); 

        const int chunkSize = 64; 
        for (int i = 0; i < content.size(); i += chunkSize) {
            m_serial.write(content.mid(i, chunkSize).toUtf8());
            m_serial.waitForBytesWritten(100);
            QThread::msleep(5); 
        }

        QThread::msleep(50);
        m_serial.write("\nCONFIG_END\n");
        m_serial.waitForBytesWritten(100);

        m_logText += "📤 Upload complete.\n";
        emit logTextChanged();
    }

    Q_INVOKABLE void loadConfigUnderglowFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_logText += "❌ Failed to open file: " + path + "\n";
            emit logTextChanged();
            return;
        }

        QString content = file.readAll();
        file.close();

        m_logText += "📄 Loaded config file (" + QString::number(content.size()) + " bytes)\n";
        emit logTextChanged();

        if (!m_serial.isOpen()) {
            m_logText += "❌ Cannot upload: Not connected\n";
            emit logTextChanged();
            return;
        }

        m_serial.write("UNDERGLOW_CONFIG_BEGIN\n");
        m_serial.waitForBytesWritten(100);
        QThread::msleep(50); 

        const int chunkSize = 64; 
        for (int i = 0; i < content.size(); i += chunkSize) {
            m_serial.write(content.mid(i, chunkSize).toUtf8());
            m_serial.waitForBytesWritten(100);
            QThread::msleep(5); 
        }

        QThread::msleep(50);
        m_serial.write("\nUNDERGLOW_CONFIG_END\n");
        m_serial.waitForBytesWritten(100);

        m_logText += "📤 Upload complete.\n";
        emit logTextChanged();
    }

    Q_INVOKABLE void readConfigFromPico() {
        if (!m_serial.isOpen() || !m_serial.isWritable()) {
            m_logText += "❌ Cannot read config: Not connected\n";
            emit logTextChanged();
            return;
        }

        m_logText += "📥 Requesting configuration from Pico...\n";
        emit logTextChanged();
        
        m_serial.write("READ_CONFIG\n");
    }

    Q_INVOKABLE void readUnderglowConfigFromPico() {
        if (!m_serial.isOpen() || !m_serial.isWritable()) {
            m_logText += "❌ Cannot read underglow config: Not connected\n";
            emit logTextChanged();
            return;
        }

        m_logText += "📥 Requesting underglow configuration from Pico...\n";
        emit logTextChanged();
        
        m_serial.write("READ_UNDERGLOW_CONFIG\n");
    }

signals:
    void logTextChanged();
    void availablePortsChanged();
    void isConnectedChanged();
    void isDarkThemeChanged();

private slots:
    void readData() {
        m_logText += QString::fromUtf8(m_serial.readAll());
        emit logTextChanged();
    }

private:
    QSerialPort m_serial;
    QString m_logText;
    QStringList m_availablePorts;
    bool m_isDarkTheme;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QQmlApplicationEngine engine;

    QString fontDir = QCoreApplication::applicationDirPath() + "/../share/fonts";
    QDir dir(fontDir);
    for (const QString &f : dir.entryList({"*.ttf", "*.otf"}, QDir::Files))
        QFontDatabase::addApplicationFont(fontDir + "/" + f);

    QFont defaultFont("Agave Nerd Font");
    defaultFont.setPointSize(11);
    app.setFont(defaultFont);

    QFile f(QCoreApplication::applicationDirPath() + "/colors.json");
    QVariantMap colors;
    if (f.open(QIODevice::ReadOnly)) {
        colors = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    }
    engine.rootContext()->setContextProperty("AppColors", colors);

    SerialMonitor monitor;
    engine.rootContext()->setContextProperty("SerialMonitor", &monitor);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    engine.loadFromModule("app", "Main");
    return app.exec();
}

#include "main.moc"
