#include <QCoreApplication>

#include "readweather.h"
#include <QTest>
#include <QtSql>
#include <QDebug>
#include <QProcessEnvironment>
#include <stdio.h>

#include "../SupportRoutines/supportfunctions.h"

int main(int argc, char *argv[])
{
    /*
     * DON'T ShowDiagnosticsSince OR DumpDebugInfo OR FlushDiagnostics
     * DIAGNOSTICS TILL AFTER OPTIONS PROCESSED.
     *
     * THIS MAY AVOID TRYING TO WRITE DIAGNOSTICS TO MAIN DATABASE SINCE
     * IT MAY NOT HAVE A DebugInfo TABLE.
     */
    QCoreApplication a(argc, argv);
    StartTime = QDateTime::currentDateTime();
    // Silent to terminal till options processed.
    qInstallMessageHandler(saveMessageOutput);
    qInfo() << "Begin";
    DetermineCommitTag();

    // Get environment to use for default values.
     QProcessEnvironment env=QProcessEnvironment::systemEnvironment();

    /*
     * Local variable declarations
     */
    QStringList driverList = QSqlDatabase::drivers();
    if (driverList.isEmpty())
    {
        qCritical("No database drivers found.");
        return 1;
    }
    qInfo() << "Available database drivers are:" << driverList;

    /*
     * Process command line options
     */
    QCommandLineParser parser;
    parser.setApplicationDescription("\nWeather\n"
                                     "     Program to read data from the weather station, store in database, and publish to MQTT.");
    parser.addHelpOption();

    QCommandLineOption serialDeviceOption(QStringList() << "s" << "serial-name", "The name of the serial device. [cu.SLAB_USBtoUART]", "Name", "cu.SLAB_USBtoUART");
    QCommandLineOption mqttWeatherTopic(QStringList() << "t" << "weather-topic", "The MQTT topic for weather data.", "Path", "demay_farm/Weather");
    QCommandLineOption mqttStatusTopic(QStringList() << "T" << "status-topic", "The MQTT topic for program status messages.", "Path", "demay_farm/Weather/Status");
    QCommandLineOption databaseOption(QStringList() << "d" << "database", "Database id string.", "URL"
                                      , env.value("DatabaseProgramAccess", "").remove(QChar('"')));
    QCommandLineOption debugDatabaseOption(QStringList() << "B" << "debug-database", "Database id string for debug info.", "URL"
                                      , "");
    QCommandLineOption showDiagnosticsOption(QStringList() << "D" << "show-diagnostics"
                                             , "Print saved diagnostics to terminal at runtime.");
    QCommandLineOption immediateDiagnosticsOption(QStringList() << "S" << "immediate-diagnostics"
                                                  , "Print diagnostic info to terminal immediately.");
    QCommandLineOption dontWriteDatabaseOption(QStringList() << "W" << "dont-write"
                                                  , "If specified, don't actually write to the database.");
    parser.addOption(serialDeviceOption);
    parser.addOption(mqttWeatherTopic);
    parser.addOption(mqttStatusTopic);
    parser.addOption(databaseOption);
    parser.addOption(debugDatabaseOption);
    parser.addOption(showDiagnosticsOption);
    parser.addOption(immediateDiagnosticsOption);
    parser.addOption(dontWriteDatabaseOption);
    parser.process(a);

    ShowDiagnostics = parser.isSet(showDiagnosticsOption);
    ImmediateDiagnostics = parser.isSet(immediateDiagnosticsOption);
    if (ImmediateDiagnostics)
        qInstallMessageHandler(terminalMessageOutput);
    else
        qInstallMessageHandler(saveMessageOutput);

    DontActuallyWriteDatabase = parser.isSet(dontWriteDatabaseOption);
    qDebug() << "DontActuallyWriteDatabase: " << DontActuallyWriteDatabase;

    QString serialDevice = parser.value(serialDeviceOption);
    qDebug() << "Using serial device" << serialDevice;

    MQTT_WEATHER_TOPIC = parser.value(mqttWeatherTopic);
    qInfo() << "MQTT_WEATHER_TOPIC" << MQTT_WEATHER_TOPIC;

    MQTT_STATUS_TOPIC = parser.value(mqttStatusTopic);
    qInfo() << "MQTT_STATUS_TOPIC" << MQTT_STATUS_TOPIC;

    QString databaseConnString = parser.value(databaseOption);
    qDebug() << "Using database connection string: " << databaseConnString;

    addConnectionFromString(databaseConnString);

    databaseConnString = parser.value(debugDatabaseOption);
    if (!databaseConnString.isEmpty())
    {
        qDebug() << "Using database connection string for debug info: " << databaseConnString;
        addConnectionFromString(databaseConnString, true);
    }

    FlushDiagnostics();
    if (QSqlDatabase::connectionNames().isEmpty())
    {
        qCritical("No database connection for weather data.");
        qInfo() << "Return" << 1;
        return 1;
    }

    //    qInfo() << "Available time zones are:  " << QTimeZone::availableTimeZoneIds();
    ReadWeather w(serialDevice);
    if (w.connected())
    {
        w.show();
        return a.exec();
    }
    qCritical() << "ReadWeather failed to connect.";
    DumpDebugInfo();
    qInfo() << "Return" << -1;
    return -1;
}
