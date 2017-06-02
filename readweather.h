#ifndef READWEATHER_H
#define READWEATHER_H

#include <QtCore>
#include <QObject>
#include <QTest>
#include <QtSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QtSql>
#include <qmqtt/qmqtt.h>
extern QString MQTT_WEATHER_TOPIC;
extern QString MQTT_STATUS_TOPIC;
extern const qint8   MQTT_QoS_AT_MOST_ONCE;
extern const qint8   MQTT_QoS_AT_LEAST_ONCE;
extern const qint8   MQTT_QoS_EXACTLY_ONCE;

#define VERBOSITY_ABSOLUTLY_NOTHING 0
#define VERBOSITY_VERY_QUIET 1
#define VERBOSITY_THE_USUAL 5
#define VERBOSITY_VERBOSE 7
#define VERBOSITY_VERY_VERBOSE 9
#define VERBOSITY_EVERY_LITTLE_THING 10

class Publisher : public QMQTT::Client
{
    Q_OBJECT
    Q_PROPERTY(bool _outOfService READ outOfService)
    Q_PROPERTY(QMQTT::ClientError _errorId READ errorId)

public:
    explicit Publisher(const QHostAddress& host,
                           const quint16 port = 1883,
                           QObject* parent = NULL);

    explicit Publisher(const QString& hostName = "localhost",
                           const quint16 port = 1883,
                           QObject* parent = NULL);

    virtual ~Publisher();

    void publishMsg(const QString &msg, const QString &topic);
    bool outOfService() const {return _outOfService;}
    QMQTT::ClientError errorId() const {return _errorId;}
    void closeConnection();

public slots:
    void onConnected();
    void onError(const QMQTT::ClientError error);
    void onDisconnected();

private:
    QMQTT::ClientError _errorId;
    bool _outOfService;
};

class ReadWeather : public QObject
{
    Q_OBJECT

public:
    explicit ReadWeather(QString &_SerialDeviceName, QObject *parent = 0);
    ~ReadWeather();
    bool connected() {return wxPort != NULL;}

    void interpretLoopData(qint16 crcVal);
    void getMinMaxFromDatabase();
    bool sendDataToRemoteDatabase(QString insertQuery);
    double getTodaysInsolation();
public slots:
    void updateWeatherData();
    void wxDataReady();
    void receiveTimeout();

private:
    void stopReadingData();
    QSerialPort *wxPort;
    QTimer *updateTimer;
    QTimer *receiveTimer;
    const char *LogString(const QByteArray &str);
    const char *PrintByteArrayInHex(const QByteArray &str);
    uint16_t ComputeCRC(const QByteArray &buf, int num=-1);
    qint64 sendData(const QByteArray &d, int timeout=5000);
    bool reconnectWxSerial();
    void resyncWx();
    void sendLoopCmd();
    void sendSyncCmd();
    void sendDmpAftCmd();
    void interpretLoopData();
    bool interpretDmpData();
    bool interpretArchiveRecord(const QByteArray &archiveRec);
    bool archiveDmpData();
    bool archiveRecord(const QByteArray &archiveRec);
    void saveArchiveData();
    void processTESTresponse();
    void processSETTIMEresponse();
    void processTIMEresponse();
    void processSETPERresponse();
    void processEERDresponse();
    void processLOOPresponse();
    void processMoreLOOPresponse();
    void processDmpAftResponse();
    void processDmpAftDataResponse();
    void processDmpAftTimeResponse();
    void getArchiveFromTime(const QDateTime firstTime, const QDateTime lastTime = QDateTime());
    /* Create an SQL string that sets the time_zone session variable to the time zone given.
     * If the time zone is UTC or Standard, the atTime is irrelevant.
     * If the time zone is Local, the answer depends on if DST is active.
     */
    bool setDbTimeZone(QTimeZone &theZone, QDateTime atTime = QDateTime::currentDateTime());

    enum {
        sentTEST,               // 0
        sentSETTIME,            // 1
        sentTime,               // 2
        sentLOOP,               // 3
        waitMoreLOOP,           // 4
        LOOPdone,               // 5
        sentDmpAft,             // 7
        sentDmpAftTime,         // 8
        waitDmpAftInfo,         // 9
        waitDmpAftData,         // 10
        waitMoreDmpAftData,     // 11
        DmpDone,                // 12
        sentEERD,               // 13
        waitMoreEERD,           // 14
        sentSETPER,             // 15
        lastState
    } state;
    QString serialDeviceName;
    int syncCount, loopTryCount, wakeupCount, tryUpdateArchiveCopy, moreLoopCount, dmpBufCount;
    int currentTimePeriod, timePeriodLength;
    QByteArray TESTcmd, LOOPcmd, SETTIMEcmd, TIMEdata, WAKEUPcmd, DMPAFTcmd, DMPAFTtime;
    QByteArray EERDcmd;
    QByteArray TESTresponse, ACKresponse, NAKresponse, OKresponse, LOOPresponse;
    QByteArray recData, LFCRresponse, RESENDbuff, ESCresponse;
    QSqlDatabase    dbConn;
    double dayMaxOutTemp, dayMinOutTemp, dayMaxInTemp, dayMinInTemp;
    double dayMaxOutHum, dayMinOutHum, dayMaxInHum, dayMinInHum;
    double dayInsolation;
    QDateTime dayMaxOutTempTime, dayMinOutTempTime, dayMaxInTempTime, dayMinInTempTime;
    QDateTime dayMaxOutHumTime, dayMinOutHumTime, dayMaxInHumTime, dayMinInHumTime;
    QDate toDay;
    QDateTime previousSolarReadingDayTimeLocal;
    int numDmpBuffers, firstDmpBuffOffset;
    QMap <QString, double> archiveRainData;
    int gettingArchiveFirstTime;
    qint64 consoleArchiveInterval, latestTimePeriodInArchive;
    QDateTime firstTimeInArchive, lastTimeToGetFromArchive;
    QTimeZone localTimeZone, localStandardTimeZone, UTCTimeZone;
    QDateTime archiveTimeStampLocalStandard;
    Publisher publisher;
};

#endif // READWEATHER_H
