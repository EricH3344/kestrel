#include "project/ProjectLoader.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ProjectFlightsTest : public QObject
{
    Q_OBJECT

private slots:
    void oldProjectOpensAsOneFlight();
    void newProjectSortsAndFlagsDifferentStructure();
    void addingFlightPersistsWithoutChangingOriginal();
    void inspectsGdalMetadataWhenProvided();
    void opensExistingProjectWhenProvided();
};

void ProjectFlightsTest::oldProjectOpensAsOneFlight()
{
    QTemporaryDir directory(QDir::currentPath() + "/ProjectFlightsTest-XXXXXX");
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath("field.kproj");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path + ": " + file.errorString()));
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    const QDateTime date = QDateTime::fromString("2025-06-01", "yyyy-MM-dd");
    stream << quint32(0x4B50524F) << quint16(1) << quint16(0);
    stream << QString("field") << directory.path() << date;
    stream << QString("raw") << QString("processed") << QString("output") << QString("metadata");
    stream << quint32(0);
    file.close();

    ProjectLoader loader;
    const QVariantMap project = loader.openProject(path);
    QCOMPARE(project.value("projectName").toString(), "field");
    const QVariantList flights = project.value("flights").toList();
    QCOMPARE(flights.size(), 1);
    QCOMPARE(flights.first().toMap().value("flightPath").toString(), directory.path());
}

void ProjectFlightsTest::newProjectSortsAndFlagsDifferentStructure()
{
    QTemporaryDir directory(QDir::currentPath() + "/ProjectFlightsTest-XXXXXX");
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath("field.kproj");
    ProjectData data;
    data.projectName = "field";
    data.projectPath = directory.path();
    data.created = QDateTime::fromString("2025-06-01", "yyyy-MM-dd");
    data.flights = {
        {"first", ".", QDateTime::fromString("2025-06-01", "yyyy-MM-dd"), {"RedEdge|Blue|475|1280, 960|UInt16"}, {}},
        {"second", "flights/second", QDateTime::fromString("2025-07-01", "yyyy-MM-dd"), {"RedEdge|Green|560|1280, 960|UInt16"}, {}}
    };
    QVERIFY(ProjectLoader::writeProject(path, data));

    ProjectLoader loader;
    const QVariantList flights = loader.openProject(path).value("flights").toList();
    QCOMPARE(flights.size(), 2);
    QCOMPARE(flights.first().toMap().value("id").toString(), "second");
    QCOMPARE(flights.first().toMap().value("compatibility").toString(), "different");
    QCOMPARE(flights.last().toMap().value("compatibility").toString(), "baseline");
}

void ProjectFlightsTest::addingFlightPersistsWithoutChangingOriginal()
{
    QTemporaryDir directory(QDir::currentPath() + "/ProjectFlightsTest-XXXXXX");
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath("field.kproj");
    ProjectData data;
    data.projectName = "field";
    data.projectPath = directory.path();
    data.created = QDateTime::currentDateTime();
    data.flights.append({"original", ".", data.created, {"baseline"}, {}});
    QVERIFY(ProjectLoader::writeProject(path, data));
    const QString source = QDir(directory.path()).filePath("sample.tif");
    QFile image(source);
    QVERIFY(image.open(QIODevice::WriteOnly));
    image.write("test");
    image.close();

    ProjectLoader loader;
    const QVariantMap added = loader.addFlight(path, {source});
    QVERIFY(!added.value("flightPath").toString().isEmpty());
    QVERIFY(QFileInfo::exists(QDir(added.value("flightPath").toString()).filePath("raw_images/sample.tif")));
    const ProjectData reopened = loader.loadProject(path);
    QCOMPARE(reopened.flights.size(), 2);
    QCOMPARE(reopened.flights.first().id, "original");
}

void ProjectFlightsTest::inspectsGdalMetadataWhenProvided()
{
    const QString path = qEnvironmentVariable("KESTREL_TEST_FLIGHT_TIFF");
    if (path.isEmpty())
        QSKIP("Set KESTREL_TEST_FLIGHT_TIFF to an XMP/EXIF TIFF for the optional GDAL check");
    const FlightData flight = ProjectLoader::inspectFlight({path});
    QVERIFY2(!flight.structure.isEmpty(), "GDAL did not return camera or pixel layout");
    QVERIFY(flight.capturedAt.isValid());
}

void ProjectFlightsTest::opensExistingProjectWhenProvided()
{
    const QString path = qEnvironmentVariable("KESTREL_TEST_LEGACY_PROJECT");
    if (path.isEmpty())
        QSKIP("Set KESTREL_TEST_LEGACY_PROJECT for the optional read-only compatibility check");
    ProjectLoader loader;
    const QVariantMap project = loader.openProject(path);
    QVERIFY(!project.value("projectName").toString().isEmpty());
    QVERIFY(!project.value("flights").toList().isEmpty());
}

QTEST_GUILESS_MAIN(ProjectFlightsTest)
#include "ProjectFlightsTest.moc"
