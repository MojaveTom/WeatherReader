#include "readweather.h"

//ReadWeather::ReadWeather()
//{

//}

#include "CCITT.H"
#include "../SupportRoutines/supportfunctions.h"

QString MQTT_WEATHER_TOPIC = "demay_farm/Weather";
QString MQTT_STATUS_TOPIC = "demay_farm/Weather/Status";
const qint8   MQTT_QoS_AT_MOST_ONCE  = 0;
const qint8   MQTT_QoS_AT_LEAST_ONCE = 1;
const qint8   MQTT_QoS_EXACTLY_ONCE  = 2;

Publisher::Publisher(const QHostAddress& host,
                     const quint16 port,
                     QObject* parent)
    : QMQTT::Client(host, port, parent)
    , _errorId(QMQTT::UnknownError)
    , _outOfService(true)
{
    qInfo("Begin creating Publisher with host address.");
    connect(this, &Publisher::connected, this, &Publisher::onConnected);
    connect(this, &Publisher::disconnected, this, &Publisher::onDisconnected);
    connect(this, &Publisher::error, this, &Publisher::onError);
    qInfo("Return");
}

Publisher::Publisher(const QString& hostname,
                     const quint16 port,
                     QObject* parent)
    : QMQTT::Client(hostname, port, false, true, parent)
    , _errorId(QMQTT::UnknownError)
    , _outOfService(true)
{
    qInfo("Begin creating Publisher with host name.");
    connect(this, &Publisher::connected, this, &Publisher::onConnected);
    connect(this, &Publisher::disconnected, this, &Publisher::onDisconnected);
    connect(this, &Publisher::error, this, &Publisher::onError);
    qInfo("Return");
}

Publisher::~Publisher() {
    disconnectFromHost();
}

void Publisher::publishMsg(const QString &msg, const QString &topic)
{
    qInfo("Begin");
    if (_outOfService)
        qDebug("Tried to Mqtt publish a message while out-of-service.");
    else
    {
        qInfo("Publishing %s", qUtf8Printable(msg));
        QMQTT::Message message(0, topic, msg.toUtf8());
        publish(message);
    }
    qInfo("Return");
}

void Publisher::onConnected()
{
    qInfo("Begin");
    qInfo("Mqtt publisher connected.");
    _outOfService = false;
    _errorId = QMQTT::UnknownError;
    qInfo("Return");
}

void Publisher::onError(const QMQTT::ClientError error)
{
    qInfo("Begin");
    qInfo("Mqtt error # %d", error);
    if (isConnectedToHost() && (! _outOfService))
    {   // first time we got here; save the error id & disconnect.
        // do assignments before trying to disconnect to avoid recursion
        _errorId = error; // error >= 0 by definition
        _outOfService = true;
        qInfo("Publisher disconnecting.");
        disconnectFromHost();
    }
    qInfo("Return");
}

void Publisher::closeConnection()
{
    qInfo("Begin");
    if (isConnectedToHost() && (! _outOfService))
    {   // first time we got here; save the error id & disconnect.
        // do assignments before trying to disconnect to avoid recursion
        _outOfService = true;
        qInfo("Publisher disconnecting.");
        disconnectFromHost();
    }
    qInfo("Return");
}

void Publisher::onDisconnected()
{
    qInfo("Begin");
    qInfo("Publisher disconnected.");
    _outOfService = true;
    qInfo("Return");
}

#define ACK '\x06'
#define NAK '\x15'
#define CANCEL '\x18'
#define ESC '\x1B'
#define RESEND '\x21'

QString SerialDeviceName;

enum {
    kMaxSyncRetries = 5
    , kMaxLoopRetries = 5
    , kMaxWakeUps = 5
    , kMaxMoreLoop = 5
    , kMaxArchiveUpdateRetries = 10
};

uint16_t ReadWeather::ComputeCRC(const QByteArray &buf, int num)
{
    qInfo("Begin");
    uint16_t crc = 0, crcTableVal = 0;
    if (num < 0)
        num = buf.size();
    for (int i = 0; i < num; i++) {
        char c = buf.at(i) & 0XFF;
        uint8_t tblIndex = ((crc >> 8) ^ c) & 0XFF;
        crcTableVal = crc_table [tblIndex];
        crc = crcTableVal ^ (crc << 8);
    }
    qInfo("Return");
    return crc;
}

const char *ReadWeather::PrintByteArrayInHex(const QByteArray &str)
{
    qInfo("Begin");
    static char     buf[4096];
    char            *ptr = buf;
    int             i;
    foreach (char c, str) {
        i = c;
        (void)sprintf(ptr, " %02x", i & 0XFF);
        ptr += 3;
        if (ptr >= buf + sizeof(buf) - 3)
            break;
    }
    *ptr = '\0';    // make sure "C" string is terminated.
    qInfo("Return");
    return buf;
}

// Replace non-printable characters in str with '\'-escaped equivalents.
// This function is used for convenient logging of data traffic.
const char *ReadWeather::LogString(const QByteArray &str)
{
    qInfo("Begin");
    static char     buf[2048];
    char            *ptr = buf;
    int             i;

    *ptr = '\0';

    foreach (char c, str) {
        if (ptr >= buf + sizeof(buf) - 5)
            break;
        switch(c)
        {
        case ' ':
            *ptr++ = c;
            break;

        case 0x06:
            *ptr++ = '<';
            *ptr++ = 'A';
            *ptr++ = 'C';
            *ptr++ = 'K';
            *ptr++ = '>';
            break;

        case 0x15:
            *ptr++ = '<';
            *ptr++ = 'N';
            *ptr++ = 'A';
            *ptr++ = 'K';
            *ptr++ = '>';
            break;

        case 0x18:
            *ptr++ = '<';
            *ptr++ = 'C';
            *ptr++ = 'A';
            *ptr++ = 'N';
            *ptr++ = '>';
            break;

        case 0x1B:
            *ptr++ = '<';
            *ptr++ = 'E';
            *ptr++ = 'S';
            *ptr++ = 'C';
            *ptr++ = '>';
            break;

            //        case 0x21:            // '!'
            //            *ptr++ = '<';
            //            *ptr++ = 'R';
            //            *ptr++ = 'E';
            //            *ptr++ = 'S';
            //            *ptr++ = 'E';
            //            *ptr++ = 'N';
            //            *ptr++ = 'D';
            //            *ptr++ = '>';
            //            break;

        case '\t':
            *ptr++ = '/';
            *ptr++ = 't';
            break;

        case 0x0A:
            *ptr++ = '/';
            *ptr++ = 'n';
            break;

        case '\r':
            *ptr++ = '/';
            *ptr++ = 'r';
            break;

        default:
            i = c & 0XFF;
            if ((i > 32) && (i < 127))
            {
                *ptr++ = c;
            }
            else
            {
                (void)sprintf(ptr, " %02x", i);
                ptr += 3;
            }
            break;
        }
    }
    *ptr = '\0';
    qInfo("Return");
    return buf;
}


double ReadWeather::getTodaysInsolation()
{
    qInfo("Begin");
    QSqlQuery query(dbConn);
    setDbTimeZone(localTimeZone);
    if (!query.exec(QString("SELECT Time, SolarRad FROM Weather WHERE Time > CURDATE() ORDER BY Time")))
    {
        qWarning() << "Error getting today's SolarRad\nError: " << query.lastError() << "\nQuery: " << query.lastQuery();
    }
    if (!query.next())
    {
        qCritical() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return" << 0.0;
        return 0.;
    }

    double prevTimeMSec = QDateTime(QDate::currentDate()).toMSecsSinceEpoch() / 3600000.0;      // hours since Epoch
    double thisTimeMSec = query.value(0).toDateTime().toMSecsSinceEpoch() / 3600000.0;
    double todayInsolation = query.value(1).toDouble() * (thisTimeMSec - prevTimeMSec);         // hours since last reading
    while (query.next())
    {
        thisTimeMSec = query.value(0).toDateTime().toMSecsSinceEpoch() / 3600000.0;
        todayInsolation += query.value(1).toDouble() * (thisTimeMSec - prevTimeMSec);
        prevTimeMSec = thisTimeMSec;
    }
    qInfo() << "Return" << todayInsolation;
    return todayInsolation;
}

ReadWeather::ReadWeather(QString &_SerialDeviceName, QObject *parent) :
    QObject(parent)
  , wxPort(NULL)
  , serialDeviceName(_SerialDeviceName)
  , syncCount(0)
  , loopTryCount(0)
  , wakeupCount(0)
  , tryUpdateArchiveCopy(0)
  , currentTimePeriod(0)
  , timePeriodLength(900)       // seconds = 15 min
  , TESTcmd("TEST\xA")
  , LOOPcmd("LOOP 1\xA")
  , SETTIMEcmd("SETTIME\xA")
  , TIMEdata("12345678")        // an 8 char string to reserve space
  , WAKEUPcmd("\xA")
  , DMPAFTcmd("DMPAFT\xA")
  , DMPAFTtime(QByteArray(6,'\0'))  // six bytes of zero to send all data in archive for DMPAFT command
  , EERDcmd("EERD 2D 01\xA")        // command to read archive interval from EEprom in console
  , TESTresponse("\n\rTEST\n\r")
  , ACKresponse("\x06")
  , NAKresponse("\x15")
  , OKresponse("OK\n\r")
  , LOOPresponse("LOO")
  , LFCRresponse("\n\r")
  , RESENDbuff("\x21")
  , ESCresponse("\x1B")
  , gettingArchiveFirstTime(0)
  , consoleArchiveInterval(0)
  , latestTimePeriodInArchive(0)
  , firstTimeInArchive()
  , localTimeZone(QTimeZone::systemTimeZoneId())
  , localStandardTimeZone(localTimeZone.standardTimeOffset(QDateTime::currentDateTime()))
  , UTCTimeZone(0)
  , archiveTimeStampLocalStandard()
  , publisher()

{
    qInfo("Begin ReadWeather creation");

    // Get environment to use for default values.
    QProcessEnvironment env=QProcessEnvironment::systemEnvironment();

    /* Setup MQTT connection  */
    qInfo("Setting up MQTT.");
    publisher.setHostName(env.value("MqttHost", "localhost").remove(QChar('"')));
    publisher.setPort(env.value("MqttPort", "1883").remove(QChar('"')).toInt());
    publisher.setUsername(env.value("MqttUser", "").remove(QChar('"')));
    publisher.setPassword(env.value("MqttPwd", "").remove(QChar('"')));
    qInfo("Connecting to host MQTT.");
    publisher.connectToHost();
    int count=0;
    while (! publisher.isConnectedToHost() && count < 20)
    {
        count++;
        qInfo("Waiting to connect MQTT");
        QTest::qWait(1000);
    }

    /* Setup database connection  */
    qInfo("Setting up database connection.");
    dbConn = QSqlDatabase::database(ConnectionName);
    if (!dbConn.isValid())
        qCritical() << ConnectionName << "is NOT valid.";
    if (!dbConn.isOpen())
        qCritical() << ConnectionName << "is NOT open.";

    toDay = QDateTime::currentDateTime().date().addDays(-1);    // set to yesterday incase getMinMaxFromDatabase fails.
    getMinMaxFromDatabase();
    int thisTimePeriod = int(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()/1000.)/timePeriodLength;
    currentTimePeriod = thisTimePeriod;

    recData.reserve(600);

    dayInsolation = getTodaysInsolation();

    updateTimer = new QTimer(this);
    connect(updateTimer, SIGNAL(timeout()), this, SLOT(updateWeatherData()));
    receiveTimer = new QTimer(this);
    connect(receiveTimer, SIGNAL(timeout()), this, SLOT(receiveTimeout()));
    receiveTimer->setSingleShot(true);

    if (reconnectWxSerial())
    {
        resyncWx();
    }
    else
    {
        qCritical("Weather station NOT CONNECTED.");
    }
    qInfo("End ReadWeather creation");
}

ReadWeather::~ReadWeather()
{
    qInfo("Begin ReadWeather deletion");
    qInfo("End ReadWeather deletion");
}


bool ReadWeather::reconnectWxSerial()
{
    qInfo("Begin");
    if (wxPort != NULL)
    {
        wxPort->flush();
        wxPort->close();
        delete wxPort;
        wxPort = NULL;
    }
    const QSerialPortInfo info(serialDeviceName);
    if (info.isNull())
    {
        qDebug("Serial port info for %s is null AND it is %s", qUtf8Printable(serialDeviceName), info.isBusy()?"busy.":"not busy.");
        qInfo() << "Available serial ports are:";
        QList<QSerialPortInfo> portList(QSerialPortInfo::availablePorts());
        foreach (QSerialPortInfo pl, portList) {
            qInfo() << "   " << pl.portName() << pl.description() << pl.systemLocation();
        }
        publisher.publishMsg(QString("OPEN FAILED"), MQTT_STATUS_TOPIC);
        qInfo() << "Return false";
        return false;
    }
    qDebug() << "Got port info for port" << info.portName();
    if (info.isBusy())
    {
        qDebug() << serialDeviceName << "is busy";
        qInfo() << "Available serial ports are:";
        QList<QSerialPortInfo> portList(QSerialPortInfo::availablePorts());
        foreach (QSerialPortInfo pl, portList) {
            qInfo() << "   " << pl.portName() << pl.description() << pl.systemLocation();
        }
        publisher.publishMsg("OPEN FAILED", MQTT_STATUS_TOPIC);
        qInfo() << "Return false";
        return false;
    }
    qDebug() << info.portName() << "is supposedly not busy.";
    QString s = QObject::tr("Port: ") + info.portName() + "\n"
            + QObject::tr("Location: ") + info.systemLocation() + "\n"
            + QObject::tr("Description: ") + info.description() + "\n"
            + QObject::tr("Manufacturer: ") + info.manufacturer() + "\n"
            + QObject::tr("Serial number: ") + info.serialNumber() + "\n"
            + QObject::tr("Vendor Identifier: ") + (info.hasVendorIdentifier() ? QString::number(info.vendorIdentifier(), 16) : QString()) + "\n"
            + QObject::tr("Product Identifier: ") + (info.hasProductIdentifier() ? QString::number(info.productIdentifier(), 16) : QString()) + "\n"
            + QObject::tr("Busy: ") + (info.isBusy() ? QObject::tr("Yes") : QObject::tr("No")) + "\n";

    qDebug() << (s);
    wxPort = new QSerialPort(info);
    s = "wxPort:    baudRate:  " + QString::number(wxPort->baudRate())
            + "    dataBits:  " + QString::number(wxPort->dataBits())
            + "    flowControl:  " + QString::number(wxPort->flowControl())
            + "    parity:  " + QString::number(wxPort->parity());
    qDebug() << (s);
    wxPort->setFlowControl(QSerialPort::NoFlowControl);
    wxPort->setBaudRate(19200);
    wxPort->setDataBits(QSerialPort::Data8);
    wxPort->setParity(QSerialPort::NoParity);
    if (!wxPort->open(QIODevice::ReadWrite))
    {
        qCritical() << (QString("could not open ") + info.portName() + "  error: ", wxPort->errorString());
        qInfo() << "Available serial ports are:";
        QList<QSerialPortInfo> portList(QSerialPortInfo::availablePorts());
        foreach (QSerialPortInfo pl, portList) {
            qInfo() << "   " << pl.portName() << pl.description() << pl.systemLocation();
        }
        publisher.publishMsg("OPEN FAILED", MQTT_STATUS_TOPIC);
    }
    else
    {
        qInfo() << (info.portName() + "  successfully opened.");
        s = "wxPort:    baudRate:  " + QString::number(wxPort->baudRate())
                + "    dataBits:  " + QString::number(wxPort->dataBits())
                + "    flowControl:  " + QString::number(wxPort->flowControl())
                + "    parity:  " + QString::number(wxPort->parity());
        qDebug() << (s);
        publisher.publishMsg(s + "  Opened", MQTT_STATUS_TOPIC);
        connect(wxPort, SIGNAL(readyRead()), this, SLOT(wxDataReady()));

        wxPort->flush();
    }
    qInfo() << "Return" << wxPort->isOpen();
    return wxPort->isOpen();
}

qint64 ReadWeather::sendData(const QByteArray &d, int timeout)
{           // default timeout = 5000 = 5 sec
    qInfo("Begin");
    recData.clear();
    receiveTimer->start(timeout);   // start timer for timeout msec in case Wx never answers
    qint64 numBytes = wxPort->write(d);
    if (numBytes == -1)
    {
        qCritical() << (QString("Error writing data to weather station - %1(%2).")
                        .arg(wxPort->errorString())
                        .arg(wxPort->error()));
    }
    else
    {
        qDebug() << (QString("Wrote %1 bytes \"%2\"")
                     .arg(numBytes)
                     .arg(PrintByteArrayInHex(d)));
    }

    if (numBytes < d.size())
    {
        qCritical() << (QString("Wrote %1 bytes; should have written %2 bytes.")
                        .arg(numBytes).arg(d.size()));
    }
    qInfo() << "Return" << numBytes;
    return numBytes;
}

void ReadWeather::sendSyncCmd()
{
    qInfo("Begin");
    syncCount++;
    state = sentTEST;
    sendData(TESTcmd);
    qDebug() << (QString("Looking for \"%1\"")
                 .arg(LogString(TESTresponse)));
    qInfo("Return");
}

void ReadWeather::resyncWx()
{
    qInfo("Begin");
    /* first flush any data waiting in the input buffers */
    QByteArray data;
    qint64 numBytesAval=0;
    int numReads = 0;
    while ((numBytesAval = wxPort->bytesAvailable()) > 0)
    {
        numReads++;
        data.append(wxPort->readAll());
    }
    qDebug() << (QString("Flushed %1 bytes in %2 reads \"%3\"")
                 .arg(data.size()).arg(numReads).arg(PrintByteArrayInHex(data)));
    syncCount = -1;
    updateTimer->stop();

    sendSyncCmd();
    qInfo("Return");
}

void ReadWeather::sendLoopCmd()
{
    qInfo("Begin");
    state = sentLOOP;
    wakeupCount = 0;
    loopTryCount++;
    sendData(LOOPcmd, 3000);
    qDebug() << (QString("Looking for loop data"));
    qInfo("Return");
}

void ReadWeather::sendDmpAftCmd()
{
    qInfo("Begin");
    state = sentDmpAft;
    wakeupCount = 0;
    loopTryCount++;
    sendData(DMPAFTcmd, 3000);
    qDebug() << (QString("Looking for <ACK> response to DMPAFT cmd."));
    qInfo("Return");
}

void ReadWeather::getMinMaxFromDatabase()
{
    qInfo("Begin");
    QString tempVal;
    QSqlQuery query(dbConn);
    toDay = QDateTime::currentDateTime().date();
    QString todayAt0000 = toDay.toString("yyyy-MM-dd");
    if (!(setDbTimeZone(localTimeZone)))
    {
        qInfo() << "Return";
        return;
    }
    if (!query.exec(QString("SELECT MAX(OutsideTemp) FROM Weather WHERE Time >= '%1'")
                    .arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMaxOutTemp = query.value(0).toDouble();
    tempVal = query.value(0).toString();
    if (!query.exec(QString("SELECT Time FROM Weather WHERE OutsideTemp = %1 AND Time >= '%2' ORDER BY Time LIMIT 1")
                    .arg(tempVal).arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMaxOutTempTime = query.value(0).toDateTime();

    if (!query.exec(QString("SELECT MIN(OutsideTemp) FROM Weather WHERE Time >= '%1'")
                    .arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMinOutTemp = query.value(0).toDouble();
    tempVal = query.value(0).toString();
    if (!query.exec(QString("SELECT Time FROM Weather WHERE OutsideTemp = %1 AND Time >= '%2' ORDER BY Time LIMIT 1")
                    .arg(tempVal).arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMinOutTempTime = query.value(0).toDateTime();

    if (!query.exec(QString("SELECT MAX(OutsideHumidity) FROM Weather WHERE Time >= '%1'")
                    .arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMaxOutHum = query.value(0).toDouble();
    tempVal = query.value(0).toString();
    if (!query.exec(QString("SELECT Time FROM Weather WHERE OutsideHumidity = %1 AND Time >= '%2' ORDER BY Time LIMIT 1")
                    .arg(tempVal).arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMaxOutHumTime = query.value(0).toDateTime();

    if (!query.exec(QString("SELECT MIN(OutsideHumidity) FROM Weather WHERE Time >= '%1'")
                    .arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMinOutHum = query.value(0).toDouble();
    tempVal = query.value(0).toString();
    if (!query.exec(QString("SELECT Time FROM Weather WHERE OutsideHumidity = %1 AND Time >= '%2' ORDER BY Time LIMIT 1")
                    .arg(tempVal).arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMinOutHumTime = query.value(0).toDateTime();

    if (!query.exec(QString("SELECT MAX(InsideTemp) FROM Weather WHERE Time >= '%1'")
                    .arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMaxInTemp = query.value(0).toDouble();
    tempVal = query.value(0).toString();
    if (!query.exec(QString("SELECT Time FROM Weather WHERE InsideTemp = %1 AND Time >= '%2' ORDER BY Time LIMIT 1")
                    .arg(tempVal).arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMaxInTempTime = query.value(0).toDateTime();

    if (!query.exec(QString("SELECT MIN(InsideTemp) FROM Weather WHERE Time >= '%1'")
                    .arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMinInTemp = query.value(0).toDouble();
    tempVal = query.value(0).toString();
    if (!query.exec(QString("SELECT Time FROM Weather WHERE InsideTemp = %1 AND Time >= '%2' ORDER BY Time LIMIT 1")
                    .arg(tempVal).arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMinInTempTime = query.value(0).toDateTime();

    if (!query.exec(QString("SELECT MAX(InsideHumidity) FROM Weather WHERE Time >= '%1'")
                    .arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMaxInHum = query.value(0).toDouble();
    tempVal = query.value(0).toString();
    if (!query.exec(QString("SELECT Time FROM Weather WHERE InsideHumidity = %1 AND Time >= '%2' ORDER BY Time LIMIT 1")
                    .arg(tempVal).arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMaxInHumTime = query.value(0).toDateTime();

    if (!query.exec(QString("SELECT MIN(InsideHumidity) FROM Weather WHERE Time >= '%1'")
                    .arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMinInHum = query.value(0).toDouble();
    tempVal = query.value(0).toString();
    if (!query.exec(QString("SELECT Time FROM Weather WHERE InsideHumidity = %1 AND Time >= '%2' ORDER BY Time LIMIT 1")
                    .arg(tempVal).arg(todayAt0000)))
    {
        qWarning() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }
    if (!query.next())
    {
        qWarning() << query.lastQuery() << " Produced no results.";
        qInfo() << "Return";
        return;
    }
    dayMinInHumTime = query.value(0).toDateTime();
    qInfo() << "Return";
}

bool ReadWeather::sendDataToRemoteDatabase(QString insertQuery)
{
    qInfo("Begin");
    QSqlError err;
    QString   driver("QMYSQL")
            , dbName("demay_farm")
            , host("mysql.server309.com")
            , user("databaseadmin")
            , passwd("66 Pair 98 Widen")
            ;
    int       port = 3306;
    bool retVal = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(driver, QString("RemoteWeather"));
        if (!db.isValid())
        {
            err = db.lastError();
            qCritical() << "Unable to addDatabase" << driver << err;
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(QString("RemoteWeather"));
            qInfo() << "Return" << retVal;
            return retVal;
        }
        db.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=10"); // Short timeout to skip if no connection.
        db.setDatabaseName(dbName);
        db.setHostName(host);
        db.setPort(port);
        if (!db.open(user, passwd))
        {
            err = db.lastError();
            qCritical("Unable to open database: err: %s; driver: %s; dbName: %s; host: %s; port: %d."
                      , qUtf8Printable(err.text())
                      , qUtf8Printable(driver)
                      , qUtf8Printable(dbName)
                      , qUtf8Printable(host)
                      , port);
            db.setConnectOptions(); // Clear option string.
            db = QSqlDatabase();    // Invalidate database.
        }
        else
        {
            QSqlDatabase dbConn = QSqlDatabase::database("RemoteWeather");
            if (!dbConn.isValid())
                qWarning() << "RemoteWeather is NOT valid.";
            if (!dbConn.isOpen())
                qWarning() << "RemoteWeather is NOT open.";

            QSqlQuery query(dbConn);
            if (!query.exec("SET time_zone = '+00:00'"))    // set time zone to UTC.  Don't use function for this database.
            {
                qWarning() << "Error setting time_zone to 0:00\nError: " << query.lastError() << "\nQuery: " << query.lastQuery();
            }
            if (!query.exec(insertQuery))
            {
                qWarning() << "Error inserting weather data in remote database.\nError: "
                           << query.lastError() << "\nQuery: " << query.lastQuery();
            }
            else
                retVal = true;
            db.close();
        }
    }
    // Both "db" and "query" are destroyed because they are out of scope
    QSqlDatabase::removeDatabase("RemoteWeather");
    qInfo() << "Return" << retVal;
    return retVal;
}

void ReadWeather::interpretLoopData()
{
    qInfo("Begin");
    QDateTime latestReadingDayTimeUTC;
    QDateTime latestReadingDayTimeLocal;
    QString latestReadingDayTimeStringUTC;
    receiveTimer->stop();
    updateTimer->start(20000);      // in millisec; this is 20 seconds.
    /* we got a good loop buffer */
    latestReadingDayTimeUTC = QDateTime::currentDateTimeUtc();
    latestReadingDayTimeLocal = latestReadingDayTimeUTC.toLocalTime();

    int thisTimePeriod = int(latestReadingDayTimeUTC.toMSecsSinceEpoch()/1000.)/timePeriodLength;
    bool storeOnWeb = (thisTimePeriod != currentTimePeriod);
    bool newDay = latestReadingDayTimeLocal.date() != toDay;

    currentTimePeriod = thisTimePeriod;
    latestReadingDayTimeStringUTC = latestReadingDayTimeUTC.toString("yyyy-MM-dd HH:mm:ss");
    if ((consoleArchiveInterval > 0) && (firstTimeInArchive.isValid()))
    {
        // See if time to update firstTimeInArchive
        qDebug() << "Checking for update of archive times.";
        qint64 currentArchiveTimePeriod = latestReadingDayTimeUTC.toMSecsSinceEpoch() / consoleArchiveInterval;
        if (currentArchiveTimePeriod > latestTimePeriodInArchive)
        {
            // Update firstTimeInArchive
            qDebug() << "Updating archive times.";
            firstTimeInArchive = firstTimeInArchive.addMSecs((currentArchiveTimePeriod - latestTimePeriodInArchive) * consoleArchiveInterval);
            latestTimePeriodInArchive = currentArchiveTimePeriod;
        }
    }

    //    if (recData.size() < 100)
    //        return;         // Belt and suspenders bail if buffer not ok
    const char *barTrend, *forecast;
    switch (uchar(recData.at(3))) {
    case 196:
        barTrend = "Falling Rapidly";
        break;
    case 236:
        barTrend = "Falling Slowly";
        break;
    case 0:
        barTrend = "Steady";
        break;
    case 20:
        barTrend = "Rising Slowly";
        break;
    case 60:
        barTrend = "Rising Rapidly";
        break;
    default:
        barTrend = "Unknown Trend";
        break;
    }
    switch (uchar(recData.at(89))) {
    case 8:
        forecast = "Mostly Clear";
        break;
    case 6:
        forecast = "Partially Cloudy";
        break;
    case 2:
        forecast = "Mostly Cloudy";
        break;
    case 3:
        forecast = "Mostly Cloudy, Rain within 12 hours";
        break;
    case 18:
        forecast = "Mostly Cloudy, Snow within 12 hours";
        break;
    case 19:
        forecast = "Mostly Cloudy, Rain or Snow within 12 hours";
        break;
    case 7:
        forecast = "Partially Cloudy, Rain within 12 hours";
        break;
    case 22:
        forecast = "Partially Cloudy, Snow within 12 hours";
        break;
    case 23:
        forecast = "Partially Cloudy, Rain or Snow within 12 hours";
        break;
    default:
        forecast = "Unknown";
        break;
    }
    double barometer = (uchar(recData.at(8))*256 + uchar(recData.at(7))) / 1000.;     // in Hg
    double insideTemp = (recData.at(10)*256 + uchar(recData.at(9))) / 10.;     // deg F
    double insideHumidity = uchar(recData.at(11));                         // percent
    double outsideTemp = (recData.at(13)*256 + uchar(recData.at(12))) / 10.;   // deg F
    double windSpeed = uchar(recData.at(14));                              // mph
    double avgWindSpeed = uchar(recData.at(15));                           // mph
    double windDir = (uchar(recData.at(17))*256 + uchar(recData.at(16))) / 1.;        // deg   e.g. 0=N, 90=E, 180=S, 270=W
    double outsideHumidity = uchar(recData.at(33));                        // percent
    double rainRate = (uchar(recData.at(42))*256 + uchar(recData.at(41))) / 100.;     // in/hour
    double UVindex = uchar(recData.at(43));                                // arbitrary units
    double solarRadiation = uchar(recData.at(45))*256 + uchar(recData.at(44));        // watt/meter^2
    double stormRain = (uchar(recData.at(47))*256 + uchar(recData.at(46))) / 100.;    // in
    double dayRain = (uchar(recData.at(51))*256 + uchar(recData.at(50))) / 100.;      // in
    double monthRain = (uchar(recData.at(53))*256 + uchar(recData.at(52))) / 100.;    // in
    double yearRain = (uchar(recData.at(55))*256 + uchar(recData.at(54))) / 100.;     // in
    ushort stormStart = uchar(recData.at(49))*256 + uchar(recData.at(48));
    int stormStartMonth = (stormStart >> 12) & 0xF;
    int stormStartDay   = (stormStart >> 7) & 0x1F;
    int stormStartYear  = (stormStart & 0x7F) + 2000;
    double dayET = (uchar(recData.at(57))*256 + uchar(recData.at(56))) / 1000.;     // in
    double consoleBatteryVolts = (uchar(recData.at(88))*256 + uchar(recData.at(87))) * 300. /512. / 100.;  // volts
    uchar xmitBattStat  = uchar(recData.at(86));

    QSqlQuery query(dbConn);
    QString insertQuery = QString("INSERT INTO Weather (Time, Barometer, InsideTemp, InsideHumidity"  //  1:4
                                  ", OutsideTemp, OutsideHumidity, WindSpeed, AveWindSpeed"       //  5:8
                                  ", WindDir, RainRate, UVIndex, SolarRad, StormRain"             //  9:13
                                  ", DayRain, MonthRain, YearRain, StormStart, ConsoleBatteryVolts"    // 14:18
                                  ", Forecast, BarometerTrend, StoreOnWeb, XmitBattStat, DayET) VALUES ");  // 19:23
    QString insertValues = QString("('%1', %2, %3, %4")  //  , %5, %6, %7, %8, %9, %10, %11, %12, %13, %14, %15, %16")
            .arg(latestReadingDayTimeStringUTC)
            .arg(barometer).arg(insideTemp).arg(insideHumidity);
    insertValues.append(outsideTemp < 3276.0 ? QString(", %1").arg(outsideTemp) : ", NULL");            // %5
    insertValues.append(outsideHumidity < 254.0 ? QString(", %1").arg(outsideHumidity) : ", NULL");     // %6
    insertValues.append(windSpeed < 254.0 ? QString(", %1").arg(windSpeed) : ", NULL");                 // %7
    insertValues.append(avgWindSpeed < 254.0 ? QString(", %1").arg(avgWindSpeed) : ", NULL");           // %8
    insertValues.append(windDir < 32760.0 ? QString(", %1").arg(windDir) : ", NULL");                   // %9
    insertValues.append(rainRate < 655.0 ? QString(", %1").arg(rainRate) : ", NULL");                   // %10
    insertValues.append(UVindex < 254.0 ? QString(", %1").arg(UVindex) : ", NULL");                     // %11
    insertValues.append(solarRadiation < 32760.0 ? QString(", %1").arg(solarRadiation) : ", NULL");     // %12
    insertValues.append(QString(", %1, %2, %3, %4").arg(stormRain).arg(dayRain).arg(monthRain).arg(yearRain));// %13,%14,%15,%16
    if (stormStart == 0xFFFFL)
        insertValues.append(", NULL");                                                                  // %17
    else
        insertValues.append(QString(", '%1-%2-%3'").arg(stormStartYear).arg(stormStartMonth).arg(stormStartDay)); // %17
    insertValues.append(QString(", %1, '%2', '%3', %4, %5, %6)")                                // %18, %19, %20, %21, %22, %23
                        .arg(consoleBatteryVolts)
                        .arg(forecast)
                        .arg(barTrend)
                        .arg(storeOnWeb ? 1 : 0) // StoreOnWeb
                        .arg(xmitBattStat)
                        .arg(dayET)
                        );
    setDbTimeZone(UTCTimeZone);
    if (!query.exec(insertQuery.append(insertValues)))
    {
        qCritical() << "Error inserting weather data\nError: " << query.lastError() << "\nQuery: " << query.lastQuery();
    }

    /* check to see if it is time to send data to other database. */
    if (storeOnWeb)
    {
        //        if (consoleArchiveInterval == -1)       // if getting console archive interval failed, try again
        //            consoleArchiveInterval = 0;         // next time we store on web.

        if (sendDataToRemoteDatabase(insertQuery))
        {
            if (!query.exec(QString("UPDATE Weather SET OnWeb = 1 WHERE Time = '%1'").arg(latestReadingDayTimeStringUTC)))
            {
                qWarning() << "Error updating OnWeb column in weather data\nError: " << query.lastError() << "\nQuery: " << query.lastQuery();
            }
        }

        DumpDebugInfo();        // At same interval as storing on remote database, dump debug info to local database table.
    }

    /*  Publish the weather data to the MQTT Broker in JSON format.   */

//    {     // This block constructs the JSON document "manually".
//        QString pubWeatherData(QString("{ \"Time\": \"%1\", \"Barometer\": %2, \"InsideTemp\": %3, \"InsideHumidity\": %4")
//                               .arg(latestReadingDayTimeStringUTC)
//                               .arg(barometer)
//                               .arg(insideTemp)
//                               .arg(insideHumidity)
//                               );
//        pubWeatherData.append(QString(", \"OutsideTemp\": "));
//        pubWeatherData.append(outsideTemp < 3276.0 ? QString("%1").arg(outsideTemp) : "null");
//        pubWeatherData.append(QString(", \"OutsideHumidity\": "));
//        pubWeatherData.append(outsideHumidity < 254.0 ? QString("%1").arg(outsideHumidity) : "null");
//        pubWeatherData.append(QString(", \"WindSpeed\": "));
//        pubWeatherData.append(windSpeed < 254.0 ? QString("%1").arg(windSpeed) : "null");
//        pubWeatherData.append(QString(", \"AveWindSpeed\": "));
//        pubWeatherData.append(avgWindSpeed < 254.0 ? QString("%1").arg(avgWindSpeed) : "null");
//        pubWeatherData.append(QString(", \"WindDir\": "));
//        pubWeatherData.append(windDir < 32760.0 ? QString("%1").arg(windDir) : "null");
//        pubWeatherData.append(QString(", \"RainRate\": "));
//        pubWeatherData.append(rainRate < 655.0 ? QString("%1").arg(rainRate) : "null");
//        pubWeatherData.append(QString(", \"UVIndex\": "));
//        pubWeatherData.append(UVindex < 254.0 ? QString("%1").arg(UVindex) : "null");
//        pubWeatherData.append(QString(", \"SolarRad\": "));
//        pubWeatherData.append(solarRadiation < 32760.0 ? QString("%1").arg(solarRadiation) : "null");
//        pubWeatherData.append(QString(", \"StormRain\": %1, \"DayRain\": %2, \"MonthRain\": %3, \"YearRain\": %4")
//                              .arg(stormRain).arg(dayRain).arg(monthRain).arg(yearRain));
//        pubWeatherData.append(QString(", \"StormStart\": "));
//        if (stormStart == 0xFFFFL)
//            pubWeatherData.append("null");
//        else
//            pubWeatherData.append(QString("\"%1-%2-%3\"").arg(stormStartYear).arg(stormStartMonth).arg(stormStartDay));
//        pubWeatherData.append(QString(", \"ConsoleBatteryVolts\": %1, \"Forecast\": \"%2\""
//                                      ", \"BarometerTrend\": \"%3\", \"XmitBattStat\": %4, \"DayET\": %5")
//                              .arg(consoleBatteryVolts)
//                              .arg(forecast)
//                              .arg(barTrend)
//                              .arg(xmitBattStat)
//                              .arg(dayET)
//                              );
//        pubWeatherData.append("}");
//        publisher.publishMsg(pubWeatherData, MQTT_WEATHER_TOPIC);
//    }

    {       // This block constructs JSON using QT's QJsonDocument class.
        QVariantMap qvmWeatherData;
        qvmWeatherData["Time"] = QVariant(latestReadingDayTimeStringUTC);
        qvmWeatherData["Barometer"] = QVariant(barometer);
        qvmWeatherData["InsideTemp"] = QVariant(insideTemp);
        qvmWeatherData["InsideHumidity"] = QVariant(insideHumidity);
        qvmWeatherData["OutsideTemp"] = outsideTemp < 3276.0 ? QVariant(outsideTemp) : QVariant("null");
        qvmWeatherData["OutsideHumidity"] = outsideHumidity < 254.0 ? QVariant(outsideHumidity) : QVariant("null");
        qvmWeatherData["WindSpeed"] = windSpeed < 254.0 ? QVariant(windSpeed) : QVariant("null");
        qvmWeatherData["AveWindSpeed"] = avgWindSpeed < 254.0 ? QVariant(avgWindSpeed) : QVariant("null");
        qvmWeatherData["WindDir"] = windDir < 32760.0 ? QVariant(windDir) : QVariant("null");
        qvmWeatherData["RainRate"] = rainRate < 655.0 ? QVariant(rainRate) : QVariant("null");
        qvmWeatherData["UVIndex"] = UVindex < 254.0 ? QVariant(UVindex) : QVariant("null");
        qvmWeatherData["SolarRad"] = solarRadiation < 32760.0 ? QVariant(solarRadiation) : QVariant("null");
        qvmWeatherData["StormStart"] = stormStart != 0xFFFFL ? QVariant(QString("\"%1-%2-%3\"").arg(stormStartYear).arg(stormStartMonth).arg(stormStartDay)) : QVariant("null");
        qvmWeatherData["StormRain"] = QVariant(stormRain);
        qvmWeatherData["DayRain"] = QVariant(dayRain);
        qvmWeatherData["MonthRain"] = QVariant(monthRain);
        qvmWeatherData["YearRain"] = QVariant(yearRain);
        qvmWeatherData["ConsoleBatteryVolts"] = QVariant(consoleBatteryVolts);
        qvmWeatherData["Forecast"] = QVariant(forecast);
        qvmWeatherData["BarometerTrend"] = QVariant(barTrend);
        qvmWeatherData["XmitBattStat"] = QVariant(xmitBattStat);
        qvmWeatherData["DayET"] = QVariant(dayET);
        publisher.publishMsg(QString(QJsonDocument::fromVariant(qvmWeatherData)
                                     .toJson(QJsonDocument::Compact))
                             , MQTT_WEATHER_TOPIC);
    }

    publisher.publishMsg(QString("%1 inside %2deg %3\% outside %4deg %5\% baro %6inHg ave wind %7mph")
                               .arg(QDateTime::currentDateTime().toString("M/d/yy HH:mm:ss"))
                               .arg(insideTemp)
                               .arg(insideHumidity)
                               .arg(outsideTemp)
                               .arg(outsideHumidity)
                               .arg(barometer)
                               .arg(avgWindSpeed), MQTT_STATUS_TOPIC);

    if (newDay)
    {
        toDay = latestReadingDayTimeLocal.date();
        /* first reading today, set min, max to first values. */
        dayMaxOutTemp = dayMinOutTemp = (outsideTemp < 3276.0 ? outsideTemp : -100.);
        dayMinOutTemp = dayMaxOutTemp;
        dayMaxInTemp  = dayMinInTemp  = insideTemp;
        dayMaxOutHum  = (outsideHumidity < 254.0 ? outsideHumidity : -200.0);
        dayMinOutHum  = dayMaxOutHum;
        dayMaxInHum   = dayMinInHum   = insideHumidity;
        dayMaxOutTempTime = dayMinOutTempTime = latestReadingDayTimeLocal;
        dayMaxInTempTime  = dayMinInTempTime  = latestReadingDayTimeLocal;
        dayMaxOutHumTime  = dayMinOutHumTime  = latestReadingDayTimeLocal;
        dayMaxInHumTime   = dayMinInHumTime   = latestReadingDayTimeLocal;
        dayInsolation = 0.;
    }

    /* Set min, max for today. */
    if ((outsideTemp > dayMaxOutTemp) && (outsideTemp < 3276.0))
    {
        dayMaxOutTemp = outsideTemp;
        dayMaxOutTempTime = latestReadingDayTimeLocal;
    }
    if (outsideTemp < dayMinOutTemp)
    {
        dayMinOutTemp = outsideTemp;
        dayMinOutTempTime = latestReadingDayTimeLocal;
    }
    if ((outsideHumidity > dayMaxOutHum) && (outsideHumidity < 254.0))
    {
        dayMaxOutHum = outsideHumidity;
        dayMaxOutHumTime = latestReadingDayTimeLocal;
    }
    if (outsideHumidity < dayMinOutHum)
    {
        dayMinOutHum = outsideHumidity;
        dayMinOutHumTime = latestReadingDayTimeLocal;
    }
    if (insideTemp > dayMaxInTemp)
    {
        dayMaxInTemp = insideTemp;
        dayMaxInTempTime = latestReadingDayTimeLocal;
    }
    if (insideTemp < dayMinInTemp)
    {
        dayMinInTemp = insideTemp;
        dayMinInTempTime = latestReadingDayTimeLocal;
    }
    if (insideHumidity > dayMaxInHum)
    {
        dayMaxInHum = insideHumidity;
        dayMaxInHumTime = latestReadingDayTimeLocal;
    }
    if (insideHumidity < dayMinInHum)
    {
        dayMinInHum = insideHumidity;
        dayMinInHumTime = latestReadingDayTimeLocal;
    }
    if (solarRadiation < 32760)     //
    {
        dayInsolation += solarRadiation * (previousSolarReadingDayTimeLocal.msecsTo(latestReadingDayTimeLocal))/3600000.0; // 3600000.0 = msec in an hour
        previousSolarReadingDayTimeLocal = latestReadingDayTimeLocal;
    }

    //    getMinMaxFromDatabase();

    qDebug() << (QString("Weather data:  %1 %2")
                 .arg(QDateTime::currentDateTime().toString("M/d/yy HH:mm:ss"))
                 .arg(insertValues));
    // We're all done with this LOOP data.
    state = LOOPdone;

    // Check for initialization actions.
    if ((consoleArchiveInterval <= 0) && (consoleArchiveInterval >= -10))
    {
        consoleArchiveInterval--;        // if getting archive interval fails, don't more than 10 times
        state = sentEERD;
        sendData(EERDcmd);
        qDebug() << (QString("Looking for \"%1\"")
                     .arg(LogString(OKresponse)));
    }
    else if (!firstTimeInArchive.isValid() && (consoleArchiveInterval > 0) && (gettingArchiveFirstTime >= 0) && (gettingArchiveFirstTime <= 10))
    {       // Check to see if we needto get archive first recorded time.
        gettingArchiveFirstTime++;
        getArchiveFromTime(firstTimeInArchive);
    }
    else if ((newDay) || ((tryUpdateArchiveCopy > 0) && (tryUpdateArchiveCopy <= kMaxArchiveUpdateRetries)))
    {
        qDebug() << "At end of day, update data in ArchiveCopy table.";
        if (newDay)
        {
            tryUpdateArchiveCopy = 0;
        }
        ++tryUpdateArchiveCopy;
        saveArchiveData();
    }
    if ((state == LOOPdone) && QFile::exists(QDir::homePath() + "/.CloseWeather"))
    {
        FlushDiagnostics();
        stopReadingData();
        QFile::remove(QDir::homePath() + "/.CloseWeather");
        qApp->quit();
    }
    qInfo() << "Return";
}

bool ReadWeather::archiveDmpData()
{
    qInfo("Begin");
    bool keepGoing = true;
    state = DmpDone;
    receiveTimer->stop();
    /* we got a good DMP buffer */
    currentTimePeriod = 0;      // forces first record to be stored on web
    int i = firstDmpBuffOffset;
    setDbTimeZone(UTCTimeZone);
    while ((i < 5)  && keepGoing)
    {
        qDebug() << "Saving archive record starting at buffer offset:  " << 1 + i * 52;
        keepGoing = archiveRecord(recData.mid(1 + i * 52,   52));
        ++i;
    }
    firstDmpBuffOffset = 0;
    qInfo() << "Return" << keepGoing;
    return keepGoing;
}

bool ReadWeather::archiveRecord(const QByteArray &archiveRec)
{
    qInfo("Begin");
    bool keepGoing;
    qDebug() << "Archiving record with data:  " << PrintByteArrayInHex(archiveRec);
    /* Extract time from archive record. */
    {   // isolate temp variables to this block
        int tmpd = uchar(archiveRec.at(0)) + 256*uchar(archiveRec.at(1));
        int tmpt = uchar(archiveRec.at(2)) + 256*uchar(archiveRec.at(3));
        int yr = tmpd / 512 + 2000, mon = (tmpd % 512) / 32, day = tmpd % 32;
        int hr = tmpt / 100, min = tmpt % 100;
        archiveTimeStampLocalStandard = QDateTime(QDate(yr, mon, day), QTime(hr, min), localStandardTimeZone);
        publisher.publishMsg(QString("Loaded archive data for %1").arg(archiveTimeStampLocalStandard.toString("yyyy-MM-dd HH:mm")), MQTT_STATUS_TOPIC);
        qDebug() << QString("Interpreting archive record:  %1").arg(archiveTimeStampLocalStandard.toString("yyyy-MM-dd HH:mm"));
    }
    if (gettingArchiveFirstTime > 0)
    {
        qInfo() << "Only getting first archive record time.";
        qInfo() << "Return false";
        return false;
    }
    keepGoing = !lastTimeToGetFromArchive.isValid() || (archiveTimeStampLocalStandard < lastTimeToGetFromArchive);

    QVariant theTimeVariant(archiveTimeStampLocalStandard.toUTC());
    QVariant theLSTimeVariant(archiveTimeStampLocalStandard);
    QVariant archRecVariant(archiveRec);

    if (!keepGoing)
        qDebug() << "Last desired archive record processed.";

    // The database connection is set to time zone UTC in calling routine.
    QSqlQuery query(dbConn);
    query.prepare("INSERT IGNORE INTO ArchiveCopy (Time, ArchiveDateTime, ArchiveData) VALUES (?, ?, ?)");
    query.bindValue(0, theTimeVariant);
    query.bindValue(1, theLSTimeVariant);
    query.bindValue(2, archRecVariant);
    if (!query.exec())
        qCritical() << "Error inserting archive record in database: " << query.lastError() << "\nQuery: " << query.lastQuery();
    else
        qDebug("Inserting archive data was successful.  Record Time %s, Archive Date Time %s."
               , qUtf8Printable( theTimeVariant.toDateTime().toString())
               , qUtf8Printable( archiveTimeStampLocalStandard.toString()));
    qInfo() << "Return" << keepGoing;
    return keepGoing;
}

void ReadWeather::saveArchiveData()
{
    qInfo("Begin");
    QDateTime maxDbTime;
    QSqlQuery query(dbConn);

    setDbTimeZone(localStandardTimeZone);       // Set database timezone to same as weather console.

    if (!query.exec(QString("SELECT MAX(Time) FROM ArchiveCopy")))
    {
        qCritical() << query.lastQuery() << ":" << query.lastError();
        qInfo() << "Return";
        return;
    }

    if (!query.next())
    {
        qWarning() << "Getting max time from ArchiveCopy produced no results.";
        publisher.publishMsg("Getting max time from ArchiveCopy produced no results.", MQTT_STATUS_TOPIC);
        qInfo() << "Return";
        return;
    }

    maxDbTime = query.value(0).toDateTime();
    qDebug() << "Max existing time in ArchiveCopy is " << maxDbTime;

    getArchiveFromTime(maxDbTime);      // Get all archive data after what we already have.
    /*
     * NOTE that if the max existing time in ArchiveCopy is not actually in the weather console,
     * we will get ALL the archive data from the console.  The only problem with this is it will
     * take a fair amount of time to get all that data from the console.  The database will ignore
     * duplicated entries.
     */
    qInfo() << "Return";
}

bool ReadWeather::interpretDmpData()
{
    qInfo("Begin");
    bool keepGoing = true;
    state = DmpDone;
    receiveTimer->stop();
    /* we got a good DMP buffer */
    currentTimePeriod = 0;      // forces first record to be stored on web
    int i = firstDmpBuffOffset;
    while ((i < 5)  && keepGoing)
    {
        qDebug() << "Interpreting archive record starting at buffer offset:  " << 1 + i * 52;
        keepGoing = interpretArchiveRecord(recData.mid(1 + i * 52,   52));
        ++i;
    }
    firstDmpBuffOffset = 0;
    qInfo() << "Return" << keepGoing;
    return keepGoing;
}

bool ReadWeather::interpretArchiveRecord(const QByteArray &archiveRec)
{
    qInfo("Begin");
    qDebug() << "Interpreting archive record with data:  " << PrintByteArrayInHex(archiveRec);
    /* Extract values from archive record. */
    {   // isolate temp variables to this block
        int tmpd = uchar(archiveRec.at(0)) + 256*uchar(archiveRec.at(1));
        int tmpt = uchar(archiveRec.at(2)) + 256*uchar(archiveRec.at(3));
        int yr = tmpd / 512 + 2000, mon = (tmpd % 512) / 32, day = tmpd % 32;
        int hr = tmpt / 100, min = tmpt % 100;
        archiveTimeStampLocalStandard = QDateTime(QDate(yr, mon, day), QTime(hr, min));
        publisher.publishMsg(QString("Loaded archive data for %1").arg(archiveTimeStampLocalStandard.toString("yyyy-MM-dd HH:mm")), MQTT_STATUS_TOPIC);
        qDebug() << QString("Interpreting archive record:  %1").arg(archiveTimeStampLocalStandard.toString("yyyy-MM-dd HH:mm"));
    }
    if (gettingArchiveFirstTime > 0)
    {
        qInfo("Return false");
        return false;
    }

    QDateTime latestReadingDayTimeUTC;
    QString latestReadingDayTimeStringUTC;
    latestReadingDayTimeUTC = archiveTimeStampLocalStandard.toUTC();
    int thisTimePeriod = int(latestReadingDayTimeUTC.toMSecsSinceEpoch()/1000.)/timePeriodLength;
    bool storeOnWeb = (thisTimePeriod != currentTimePeriod);
    currentTimePeriod = thisTimePeriod;
    latestReadingDayTimeStringUTC = latestReadingDayTimeUTC.toString("yyyy-MM-dd HH:mm:ss");

    double outsideTemp = (archiveRec.at(5)*256 + uchar(archiveRec.at(4))) / 10.;   // deg F
    double barometer = (uchar(archiveRec.at(15))*256 + uchar(archiveRec.at(14))) / 1000.;     // in Hg
    double solarRadiation = uchar(archiveRec.at(17))*256 + uchar(archiveRec.at(16));        // watt/meter^2
    double rainRate = (uchar(archiveRec.at(13))*256 + uchar(archiveRec.at(12))) / 100.;     // in/hour
    double insideTemp = (archiveRec.at(21)*256 + uchar(archiveRec.at(20))) / 10.;     // deg F
    double insideHumidity = uchar(archiveRec.at(22));                         // percent
    double outsideHumidity = uchar(archiveRec.at(23));                        // percent
    double avgWindSpeed = uchar(archiveRec.at(24));                           // mph
    double UVindex = uchar(archiveRec.at(28)) / 10.0;                         // arbitrary units
    double windDir = uchar(archiveRec.at(27))*360.0/16.0;        // deg   e.g. 0=N, 90=E, 180=S, 270=W in "quadrants"

    double incrementalRain = (uchar(archiveRec.at(11))*256 + uchar(archiveRec.at(10))) / 100.0;    // 0.01 inches rain since last sample.
    archiveRainData.insert(latestReadingDayTimeStringUTC, incrementalRain);     // Use UTC time string for key since it is exactly the primary key in the database.

    if (lastTimeToGetFromArchive.isValid() && (archiveTimeStampLocalStandard >= lastTimeToGetFromArchive))
    {
        qDebug() << "Last desired archive record processed.";
    }
    if (!lastTimeToGetFromArchive.isValid() || (archiveTimeStampLocalStandard <= lastTimeToGetFromArchive))
    {
        QSqlQuery query(dbConn);
        QString insertQuery = QString("INSERT IGNORE INTO Weather (Time, Barometer, InsideTemp, InsideHumidity"  //  1:4
                                      ", OutsideTemp, OutsideHumidity, AveWindSpeed"       //  5:7
                                      ", WindDir, RainRate, UVIndex, SolarRad"             //  8:11
                                      ", StoreOnWeb) VALUES ");                            // 121
        QString insertValues = QString("('%1'").arg(latestReadingDayTimeStringUTC);
        insertValues.append(barometer <= 1 ? ", NULL" : QString(", %1").arg(barometer));
        insertValues.append(insideTemp >= 3276.0 ? ", NULL" : QString(", %1").arg(insideTemp));
        insertValues.append(insideHumidity >= 250.0 ? ", NULL" : QString(", %1").arg(insideHumidity));
        insertValues.append(outsideTemp < 3276.0 ? QString(", %1").arg(outsideTemp) : ", NULL");
        insertValues.append(outsideHumidity < 254.0 ? QString(", %1").arg(outsideHumidity) : ", NULL");
        insertValues.append(avgWindSpeed < 254.0 ? QString(", %1").arg(avgWindSpeed) : ", NULL");
        insertValues.append(windDir < 420.0 ? QString(", %1").arg(windDir) : ", NULL");
        insertValues.append(rainRate < 655.0 ? QString(", %1").arg(rainRate) : ", NULL");
        insertValues.append(UVindex < 254.0 ? QString(", %1").arg(UVindex) : ", NULL");
        insertValues.append(solarRadiation < 32760.0 ? QString(", %1").arg(solarRadiation) : ", NULL");
        insertValues.append(QString(", %1)").arg(storeOnWeb ? 1 : 0)); // StoreOnWeb

        setDbTimeZone(UTCTimeZone);
        if (!query.exec(insertQuery.append(insertValues)))
        {
            qCritical() << "Error inserting weather data\nError: " << query.lastError() << "\nQuery: " << query.lastQuery();
        }

        qDebug() << (QString("Weather data:  %1 %2")
                     .arg(archiveTimeStampLocalStandard.toString("yyyy-MM-dd HH:mm:ss"))
                     .arg(insertValues));
    }
    qInfo() << "Return" << (!lastTimeToGetFromArchive.isValid() || (archiveTimeStampLocalStandard < lastTimeToGetFromArchive));
    return (!lastTimeToGetFromArchive.isValid() || (archiveTimeStampLocalStandard < lastTimeToGetFromArchive));
}

void ReadWeather::processTESTresponse()
{
    qInfo("Begin");
    if (recData != TESTresponse)
    {
        qDebug() << (QString("Sync modem try #%1 failed.")
                     .arg(syncCount));
        if (syncCount < kMaxSyncRetries)
        {
            syncCount++;
            sendData(TESTcmd);
        }
        else
        {
            qWarning() << (QString("Sync modem failed."));
            if (!reconnectWxSerial())
            {
                qCritical() << (QString("Wx modem reconnect failed."));
                qInfo("Return");
                return;
            }
            else
            {
                syncCount = 0;
                sendData(TESTcmd);
            }
        }
        qDebug() << (QString("Looking for \"%1\"")
                     .arg(LogString(TESTresponse)));
    }
    else
    {
        state = sentSETTIME;
        sendData(SETTIMEcmd);
        qDebug() << (QString("Looking for \"%1\"")
                     .arg(LogString(ACKresponse)));
    }
    qInfo("Return");
}

void ReadWeather::processSETTIMEresponse()
{
    qInfo("Begin");
    if (recData != ACKresponse)
    {
        if (recData == NAKresponse)
        {
            qWarning() << (QString("received NAK instead of ACK after SETTIME"));
        }
        qCritical() << (QString("SETTIME failed; resync."));
        resyncWx();
    }
    else
    {
        QTimeZone myTimeZone(QTimeZone::systemTimeZoneId());
        QDateTime theTime(QDateTime::currentDateTime());
        theTime = theTime.toOffsetFromUtc(myTimeZone.standardTimeOffset(theTime));
        qDebug() << "Setting time to standard time for now: year, month, day, hour, minute, sec "
                 << theTime.date().year() << theTime.date().month() << theTime.date().day()
                 << theTime.time().hour() << theTime.time().minute() << theTime.time().second();
        TIMEdata.clear();
        TIMEdata[0] = (uint8_t)theTime.time().second();
        TIMEdata[1] = (uint8_t)theTime.time().minute();
        TIMEdata[2] = (uint8_t)theTime.time().hour();
        TIMEdata[3] = (uint8_t)theTime.date().day();
        TIMEdata[4] = (uint8_t)theTime.date().month();
        TIMEdata[5] = (uint8_t)(theTime.date().year() - 1900);
        uint16_t timeCRC = ComputeCRC(TIMEdata, 6);
        TIMEdata[6] = (uint8_t)(timeCRC / 256);
        TIMEdata[7] = (uint8_t)(timeCRC % 256);
        state = sentTime;
        sendData(TIMEdata);
        qDebug() << (QString("Looking for \"%1\"")
                     .arg(LogString(ACKresponse)));
    }
    qInfo("Return");
}

void ReadWeather::processTIMEresponse()
{
    qInfo("Begin");
    if (recData != ACKresponse)
    {
        if (recData == NAKresponse)
        {
            qWarning() << (QString("received NAK instead of ACK after sending time data."));
        }
        qCritical() << (QString("setting time failed; resync."));
        resyncWx();
    }
    else
    {
        loopTryCount = -1;      //so first inc in sendLoopCmd routine will result in zero
        sendLoopCmd();
    }
    qInfo("Return");
}

void ReadWeather::processSETPERresponse()
{
    qInfo("Begin");
    if (recData != ACKresponse)
        qWarning() << QString("received \"%1\" instead of ACK after sending SETPER command.  Resync.")
                      .arg(LogString(recData));
    else
        qDebug() << "Successfully set archive interval.  Resync.";
    consoleArchiveInterval = 0;     // will cause archive interval to be read back from console next loop data
    resyncWx();
    qInfo("Return");
}

void ReadWeather::processEERDresponse()
{
    qInfo("Begin");
    if (recData.startsWith(LFCRresponse))
        recData.remove(0, LFCRresponse.size());
    if (recData.size() < OKresponse.size() * 2)  // 1 hex byte response (same size as OKresponse) needed too
    {
        if (moreLoopCount < kMaxMoreLoop)
        {
            state = waitMoreEERD;
        }
        else
        {
            qCritical() << (QString("Never got enough EERD response data - resync."));
            resyncWx();
        }
    }
    if (!recData.startsWith(OKresponse))
    {
        qCritical() << (QString("invalid response to EERD; resync."));
        resyncWx();
    }
    else
    {
        bool ok;
        recData.remove(0, OKresponse.size());
        consoleArchiveInterval = recData.left(2).toLongLong(&ok, 16) * 60 * 1000;   //in millisec
        if (!ok)
        {
            qCritical() << QString("intreperting archive interval response failed.  String was %1, value is %2")
                           .arg(LogString(recData.left(2))).arg(consoleArchiveInterval);
        }
        else
        {
            qDebug() << QString("Console archive interval read from console.  Value is %1 msec").arg(consoleArchiveInterval);
        }
        if (consoleArchiveInterval == 0)
            consoleArchiveInterval = -2;
        publisher.publishMsg(QString("Console archive interval read from console.  Value is %1 msec").arg(consoleArchiveInterval), MQTT_STATUS_TOPIC);
        // let refresh timeout trigger next loop command
        //        loopTryCount = -1;      //so first inc in sendLoopCmd routine will result in zero
        //        sendLoopCmd();
    }
    qInfo("Return");
}

void ReadWeather::processLOOPresponse()
{
    qInfo("Begin");
    if (loopTryCount > kMaxLoopRetries)
    {
        qWarning() << (QString("Loop comand failed too many times; resync."));
        resyncWx();
    }
    else
    {
        if (recData.startsWith(ACK))
            recData = recData.remove(0, 1);       // throw away leading ACK char
        if (recData.size() < 99)
        {
            qWarning() << (QString("Got a too small LOOP buffer; wait for more."));
            state = waitMoreLOOP;
            moreLoopCount = 0;
            QTimer::singleShot(100, this, SLOT(wxDataReady()));
            qInfo("Return");
            return;
        }
        qint16 crcVal = 0;
        if (!recData.startsWith(LOOPresponse))
        {
            qWarning() << (QString("Got a LOOP buffer that doesn't start with \"%1\"; resend LOOP.")
                           .arg(LOOPresponse.data()));
            sendLoopCmd();
            qInfo("Return");
            return;
        }
        else
        {
            crcVal = ComputeCRC(recData, 99);
            if ( crcVal != 0)
            {
                qWarning() << (QString("Received LOOP buffer with bad CRC; ignore."));
                //                    sendLoopCmd();
                //                    break;
            }
        }
        interpretLoopData();
    }
    qInfo("Return");
}

void ReadWeather::processMoreLOOPresponse()
{
    qInfo("Begin");
    {
        if (recData.size() < 99)
        {
            qDebug() << (QString("LOOP buffer still too small; wait for more."));
            if (moreLoopCount <= kMaxMoreLoop)
            {
                moreLoopCount++;
                QTimer::singleShot(100, this, SLOT(wxDataReady()));
                qInfo("Return");
                return;
            }
            sendLoopCmd();
            qInfo("Return");
            return;
        }
        qint16 crcVal = 0;
        if (!recData.startsWith(LOOPresponse))
        {
            qWarning() << (QString("Got a LOOP buffer that doesn't start with \"%1\"; resend LOOP.")
                           .arg(LOOPresponse.data()));
            sendLoopCmd();
            qInfo("Return");
            return;
        }
        else
        {
            crcVal = ComputeCRC(recData, 99);
            if ( crcVal != 0)
            {
                qWarning() << (QString("Received more LOOP buffer with bad CRC; resync weather station."));
                resyncWx();
                qInfo("Return");
                return;
            }
        }
        interpretLoopData();
    }
    qInfo("Return");
}

void ReadWeather::processDmpAftResponse()
{
    qInfo("Begin");
    if (recData.startsWith(ACK))
    {
        qDebug() << QString("Sending DMPAFT time string:  %1").arg(PrintByteArrayInHex(DMPAFTtime));
        moreLoopCount = 0;
        state = sentDmpAftTime;
        sendData(DMPAFTtime);
    }
    else
    {
        qWarning() << (QString("Did not receive ACK response to DMPAFT command."));
        publisher.publishMsg("Did not receive ACK response to DMPAFT command.  --  Resync weather station.", MQTT_STATUS_TOPIC);
        resyncWx();
    }
    qInfo("Return");
}

void ReadWeather::processDmpAftTimeResponse()
{
    qInfo("Begin");
    if (recData.size() < 7)
    {
        if (moreLoopCount <= kMaxMoreLoop)
        {
            qDebug() << (QString("Too few chars in response to DMPAFT time - wait for more."));
            moreLoopCount++;
            QTimer::singleShot(100, this, SLOT(wxDataReady()));
            qInfo("Return");
            return;
        }
        qWarning() << (QString("Too few chars in response to DMPAFT time - resync."));
        publisher.publishMsg("Too few chars in response to DMPAFT time.  --  Resync weather station.", MQTT_STATUS_TOPIC);
        resyncWx();
        qInfo("Return");
        return;
    }
    qDebug() << "Got a DMPAFT time response of " << recData.size() << " bytes.";
    qDebug() << PrintByteArrayInHex(recData);
    if (recData.startsWith(ACK))
        recData = recData.remove(0, 1);       // throw away leading ACK char
    else
    {
        qWarning() << (QString("Did not receive ACK response to DMPAFT time data."));
        publisher.publishMsg("Did not receive ACK response to DMPAFT time data.  --  Resync weather station.", MQTT_STATUS_TOPIC);
        resyncWx();
        qInfo("Return");
        return;
    }
    qint16 crcVal = 0;
    crcVal = ComputeCRC(recData, 6);
    if ( crcVal != 0)
    {
        qWarning() << QString("Received DMP buffer info with bad CRC (= %1); ignore.").arg(crcVal, 4, 16);
        publisher.publishMsg("Bad CRC for DMP info in response to DMPAFT time.  --  ignore.", MQTT_STATUS_TOPIC);
        //        resyncWx();
        //        qInfo("Return");
        //        return;
    }
    numDmpBuffers = (recData.at(1)*256 + uchar(recData.at(0)));
    firstDmpBuffOffset = (recData.at(3)*256 + uchar(recData.at(2)));
    qDebug() << QString("Expecting %1 buffers with first data in buffer %2").arg(numDmpBuffers).arg(firstDmpBuffOffset);
    archiveRainData.clear();
    state = waitDmpAftData;
    moreLoopCount = 0;              // Counts buffer resend commands
    sendData(ACKresponse);          // initiate transmission of next DMP buffer.
    qInfo("Return");
}

void ReadWeather::processDmpAftDataResponse()
{
    qInfo("Begin");
    if (loopTryCount > kMaxLoopRetries)
    {
        moreLoopCount++;
        if ((moreLoopCount < kMaxMoreLoop) && (recData.size() > 100))
        {
            // Maybe we got stalled getting data.  Send "!" to retry this buffer
            qDebug() << (QString("DMP data never arrived; request buffer again."));
            loopTryCount = 0;
            recData.clear();
            state = waitDmpAftData;
            sendData(RESENDbuff);
            qInfo("Return");
            return;
        }
        qWarning() << (QString("DMP data never arrived; resync."));
        publisher.publishMsg("DMP data never arrived; resync.", MQTT_STATUS_TOPIC);
        resyncWx();
        qInfo("Return");
        return;
    }

    //  BUFFERS CAN LEGITIMATLY HAVE "ACK" AS FIRST CHAR
    //    if (recData.startsWith(ACK))
    //        recData = recData.remove(0, 1);       // throw away leading ACK char
    if (recData.size() < 267)
    {
        qWarning() << (QString("Got a too small DMP buffer; wait for more."));
        if (state != waitMoreDmpAftData)
            loopTryCount = 0;
        else
            ++loopTryCount;
        state = waitMoreDmpAftData;
        QTimer::singleShot(100, this, SLOT(wxDataReady()));
        qInfo("Return");
        return;
    }
    qDebug() << "Number of buffers remaining:  " << numDmpBuffers;
    qint16 crcVal = 0;
    crcVal = ComputeCRC(recData, 267);
    if ( crcVal != 0)
    {
        qWarning() << QString("Received DMP buffer with bad CRC (= %1); ignore.").arg(crcVal, 4, 16);
        publisher.publishMsg("Received DMP buffer with bad CRC; ignored.", MQTT_STATUS_TOPIC);
        resyncWx();
        qInfo("Return");
        return;
    }
    else
    {       // Good DMP buffer with good CRC
        qDebug() << (QString("Received DMP buffer with good CRC; continue."));

        //        bool keepGoing = interpretDmpData();  // Returns false when processing a record with time >= end time, or getting first archive time info.
        bool keepGoing = archiveDmpData();
        --numDmpBuffers;
        if ((numDmpBuffers == 0) || !keepGoing)
        {
            //    All done with this DMP command
            //  Update day rain data in database from archiveRainData
            //    getMinMaxFromDatabase();
            tryUpdateArchiveCopy = -1;              // Flag all done updating the ArchiveCopy table.
            sendData(ESCresponse);
            if (gettingArchiveFirstTime > 0)
            {
                gettingArchiveFirstTime = -1;       // Flag that we have a good firstTimeInArchive
                firstTimeInArchive = archiveTimeStampLocalStandard;
                latestTimePeriodInArchive = QDateTime::currentMSecsSinceEpoch() / consoleArchiveInterval;
                if ((firstTimeInArchive.toMSecsSinceEpoch() % consoleArchiveInterval) != 0)
                    qDebug() << "First time in archive is not a multiple of the console archive interval??  Remainder is "
                             << (firstTimeInArchive.toMSecsSinceEpoch() % consoleArchiveInterval)
                             << "mSec";
                else
                    qDebug() << "Successfully set first time in archive.";
            }
            resyncWx();
            qInfo("Return");
            return;
        }
    }
    state = waitDmpAftData;
    sendData(ACKresponse);
    qInfo("Return");
}

void ReadWeather::wxDataReady()
{
    qInfo("Begin");
    qint64 numBytesAval=0;
    int numReads = 0;
    while ((numBytesAval = wxPort->bytesAvailable()) > 0)
    {
        numReads++;
        recData.append(wxPort->readAll());
    }
    qDebug() << (QString("Received %1 bytes in %2 reads \"%3\"")
                 .arg(recData.size()).arg(numReads).arg(PrintByteArrayInHex(recData)));
    if (recData == LFCRresponse)
    {
        /* ignore data from wx consisting only of \n\r */
        recData.clear();
        qInfo("Return");
        return;
    }
    receiveTimer->stop();
    switch (state) {
    case sentTEST:
        processTESTresponse();
        break;
    case sentSETTIME:
        processSETTIMEresponse();
        break;
    case sentTime:
        processTIMEresponse();
        break;
    case sentLOOP:
        processLOOPresponse();
        break;
    case waitMoreLOOP:
        processMoreLOOPresponse();
        break;
    case LOOPdone:
        // Received more data while processing loop data
        qCritical() << (QString("Received data in state LOOPdone."));
        break;
    case sentDmpAft:
        processDmpAftResponse();
        break;
    case sentDmpAftTime:
        processDmpAftTimeResponse();
        break;
    case waitDmpAftData:
        processDmpAftDataResponse();
        break;
    case waitMoreDmpAftData:
        processDmpAftDataResponse();
        break;
    case sentEERD:
        processEERDresponse();
        break;
    case waitMoreEERD:
        moreLoopCount++;
        processEERDresponse();
    case sentSETPER:
        processSETPERresponse();
        break;
    default:
        qCritical() << (QString("Received data in unknown state %1")
                        .arg(state));
        break;
    }
    qInfo("Return");
}

void ReadWeather::receiveTimeout()
{
    qInfo("Begin");
    qDebug() << (QString("Receive timeout for state %1")
                 .arg(state));
    switch (state) {
    case sentTEST:
        if (syncCount <= kMaxSyncRetries)
            sendSyncCmd();
        else
            reconnectWxSerial();
        break;
    case sentSETTIME:
        resyncWx();
        break;
    case sentTime:
        resyncWx();
        break;
    case sentLOOP:
        if (wakeupCount < kMaxWakeUps)
        {
            wakeupCount++;
            sendData(WAKEUPcmd, 500);
        }
        else
            resyncWx();
        break;
    case sentDmpAft:
        qDebug() << (QString("Receive timeout waiting for DMPAFT response."));
        publisher.publishMsg(QString("Receive timeout waiting for DMPAFT response."), MQTT_STATUS_TOPIC);
        resyncWx();
        break;
    case sentDmpAftTime:
        qDebug() << (QString("Receive timeout waiting for DMPAFTtime response."));
        publisher.publishMsg(QString("Receive timeout waiting for DMPAFTtime response."), MQTT_STATUS_TOPIC);
        resyncWx();
        break;
    case sentEERD:
        qDebug() << (QString("Receive timeout waiting for EERDcmd response."));
        publisher.publishMsg("Receive timeout waiting for EERDcmd response.", MQTT_STATUS_TOPIC);
        resyncWx();
        break;
    case waitMoreEERD:
        qDebug() << (QString("Receive timeout waiting for more EERD response."));
        publisher.publishMsg("Receive timeout waiting for more EERD response.", MQTT_STATUS_TOPIC);
        resyncWx();
        break;
    case sentSETPER:
        qDebug() << (QString("Receive timeout waiting for SETPER response."));
        publisher.publishMsg("Receive timeout waiting for SETPER response.", MQTT_STATUS_TOPIC);
        resyncWx();
        break;

    default:
        break;
    }
    qInfo("Return");
}

void ReadWeather::updateWeatherData()
{
    qInfo("Begin");
    loopTryCount = -1;
    sendLoopCmd();
    qInfo("Return");
}

void ReadWeather::stopReadingData()
{
    qInfo("Begin");
    updateTimer->stop();
    receiveTimer->stop();
    wxPort->close();
    publisher.closeConnection();
    qDebug() << ("Weather modem closed.");
    qInfo("Return");
}

void ReadWeather::getArchiveFromTime(const QDateTime firstTime, const QDateTime lastTime)
{
    qInfo("Begin");
    qDebug() << ("Loop done - send DMPAFT command.");

    lastTimeToGetFromArchive = lastTime;
    qint16 dateCalc = 0, timeCalc = 0;
    if (firstTime.isValid())
    {
        dateCalc = firstTime.date().day()
                + firstTime.date().month() * 32
                + (firstTime.date().year() - 2000) * 512;
        timeCalc = firstTime.time().hour()*100
                + firstTime.time().minute();
    }

    DMPAFTtime[0] = (uint8_t)(dateCalc & 0XFF);
    DMPAFTtime[1] = (uint8_t)((dateCalc >> 8) & 0XFF);
    DMPAFTtime[2] = (uint8_t)(timeCalc & 0XFF);
    DMPAFTtime[3] = (uint8_t)((timeCalc  >> 8) & 0XFF);
    quint16 crcCalc = ComputeCRC(DMPAFTtime, 4);
    DMPAFTtime[4] = (uint8_t)((crcCalc >> 8) & 0XFF);      // MSB first as per documentation
    DMPAFTtime[5] = (uint8_t)(crcCalc & 0XFF);

    qDebug() << QString("DMPAFT time string is %1 (%2 %3 %4)")
                .arg(PrintByteArrayInHex(DMPAFTtime))
                .arg(dateCalc&0xffff, 4, 16)
                .arg(timeCalc&0xffff, 4, 16)
                .arg(crcCalc&0xffff, 4, 16)
                ;
    qDebug() << QString("Computing CRC on whole DMPAFTtime buffer gives:  %1").arg(ComputeCRC(DMPAFTtime, 6)&0XFFFF, 4, 16);

    updateTimer->stop();
    receiveTimer->stop();
    loopTryCount = -1;      //so first inc in sendDmpAftCmd routine will result in zero
    state = sentDmpAft;
    sendData(DMPAFTcmd);
    qInfo("Return");
}

bool ReadWeather::setDbTimeZone(QTimeZone &theZone, QDateTime atTime)
{
    qInfo("Begin");
    QSqlQuery query(dbConn);
    int tzOffset = theZone.offsetFromUtc(atTime);
    int tzOffsetHours = abs(tzOffset) / 3600, tzOffsetMinutes = (abs(tzOffset) / 60) % 60;
    QChar plusMinus = tzOffset < 0 ? '-' : '+';
    if (!query.exec(QString("SET time_zone = '%1%2:%3'")
                    .arg(plusMinus)
                    .arg(tzOffsetHours, 2, 10, QChar('0'))
                    .arg(tzOffsetMinutes, 2, 10, QChar('0'))))
    {
        qCritical() << "Error setting time_zone to " << theZone <<"  : " << query.lastError() << "\nQuery: " << query.lastQuery();
        qInfo("Return false");
        return false;
    }
    qInfo("Return true");
    return true;
}
