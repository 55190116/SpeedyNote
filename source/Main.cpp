#ifdef _WIN32
#include <windows.h>
#endif

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QColor>
#include "MainWindow.h"
#include "SpnPackageManager.h"
#include "InkCanvas.h" // For BackgroundStyle enum

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    FreeConsole();  // Hide console safely on Windows

    /*
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    */
    
    
     // to show console for debugging
    
    
#endif
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
    SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);

    /*
    qDebug() << "SDL2 version:" << SDL_GetRevision();
    qDebug() << "Num Joysticks:" << SDL_NumJoysticks();

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            qDebug() << "Controller" << i << "is" << SDL_GameControllerNameForIndex(i);
        } else {
            qDebug() << "Joystick" << i << "is not a recognized controller";
        }
    }
    */  // For sdl2 debugging
    


    // Enable Windows IME support for multi-language input
    QApplication app(argc, argv);
    
    // Ensure IME is properly enabled for Windows
    #ifdef _WIN32
    app.setAttribute(Qt::AA_EnableHighDpiScaling, true);
    app.setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    #endif

    
    QTranslator translator;
    QString locale = QLocale::system().name(); // e.g., "zh_CN", "es_ES"
    QString langCode = locale.section('_', 0, 0); // e.g., "zh"

    // Debug: Uncomment these lines to debug translation loading issues
    // printf("Locale: %s\n", locale.toStdString().c_str());
    // printf("Language Code: %s\n", langCode.toStdString().c_str());

    // Try multiple paths to find translation files
    QStringList translationPaths = {
        QCoreApplication::applicationDirPath(),  // Same directory as executable
        QCoreApplication::applicationDirPath() + "/translations",  // translations subdirectory
        ":/resources/translations"  // Qt resource system
    };
    
    bool translationLoaded = false;
    for (const QString &path : translationPaths) {
        QString translationFile = path + "/app_" + langCode + ".qm";
        if (translator.load(translationFile)) {
            app.installTranslator(&translator);
            translationLoaded = true;
            // Debug: Uncomment this line to see which translation file was loaded
            // printf("Translation loaded from: %s\n", translationFile.toStdString().c_str());
            break;
        }
    }
    
    if (!translationLoaded) {
        // Debug: Uncomment this line to see when translation loading fails
        // printf("No translation file found for language: %s\n", langCode.toStdString().c_str());
    }

    QString inputFile;
    bool createNewPackage = false;
    bool createSilent = false;
    
    if (argc >= 2) {
        QString firstArg = QString::fromLocal8Bit(argv[1]);
        if (firstArg == "--create-new" && argc >= 3) {
            // Handle --create-new command (opens SpeedyNote)
            createNewPackage = true;
            inputFile = QString::fromLocal8Bit(argv[2]);
        } else if (firstArg == "--create-silent" && argc >= 3) {
            // Handle --create-silent command (creates file and exits)
            createSilent = true;
            inputFile = QString::fromLocal8Bit(argv[2]);
        } else {
            // Regular file argument
            inputFile = firstArg;
        }
        // qDebug() << "Input file received:" << inputFile << "Create new:" << createNewPackage << "Create silent:" << createSilent;
    }

    // Handle silent creation (context menu) - create file and exit immediately
    if (createSilent && !inputFile.isEmpty()) {
        if (inputFile.toLower().endsWith(".spn")) {
            // Check if file already exists
            if (QFile::exists(inputFile)) {
                return 1; // Exit with error code
            }
            
            // Get the base name for the notebook (without .spn extension)
            QFileInfo fileInfo(inputFile);
            QString notebookName = fileInfo.baseName();
            
            // ✅ Load user's default background settings
            QSettings settings("SpeedyNote", "App");
            BackgroundStyle defaultStyle = static_cast<BackgroundStyle>(
                settings.value("defaultBackgroundStyle", static_cast<int>(BackgroundStyle::Grid)).toInt());
            QColor defaultColor = QColor(settings.value("defaultBackgroundColor", "#FFFFFF").toString());
            int defaultDensity = settings.value("defaultBackgroundDensity", 30).toInt();
            
            // Ensure valid values
            if (!defaultColor.isValid()) defaultColor = Qt::white;
            if (defaultDensity < 10) defaultDensity = 10;
            if (defaultDensity > 200) defaultDensity = 200;
            
            // Create the new .spn package with user's preferred background settings
            if (SpnPackageManager::createSpnPackageWithBackground(inputFile, notebookName, 
                                                                  defaultStyle, defaultColor, defaultDensity)) {
                // ✅ Notify Windows Explorer to refresh (fix file manager issue)
#ifdef Q_OS_WIN
                SHChangeNotify(SHCNE_CREATE, SHCNF_PATH, inputFile.toStdWString().c_str(), nullptr);
#endif
                return 0; // Exit successfully
            } else {
                return 1; // Exit with error code
            }
        }
        return 1; // Invalid file extension
    }

    // Check if another instance is already running
    if (MainWindow::isInstanceRunning()) {
        if (!inputFile.isEmpty()) {
            // Prepare command for existing instance
            QString command;
            if (createNewPackage) {
                command = QString("--create-new|%1").arg(inputFile);
            } else {
                command = inputFile;
            }
            
            // Send command to existing instance
            if (MainWindow::sendToExistingInstance(command)) {
                return 0; // Exit successfully, command sent to existing instance
            }
        }
        // If no command to send or sending failed, just exit
        return 0;
    }

    MainWindow w;
    if (!inputFile.isEmpty()) {
        if (createNewPackage) {
            // Handle --create-new command
            if (inputFile.toLower().endsWith(".spn")) {
                w.show(); // Show window first
                w.createNewSpnPackage(inputFile);
            } else {
                // Invalid file extension for new package
                w.show();
            }
        } else {
            // Check file extension to determine how to handle it
            if (inputFile.toLower().endsWith(".pdf")) {
                // Handle PDF file association
                w.show(); // Show window first for dialog parent
                w.openPdfFile(inputFile);
            } else if (inputFile.toLower().endsWith(".spn")) {
                // Handle SpeedyNote package
                w.show(); // Show window first
                w.openSpnPackage(inputFile);
            } else {
                // Unknown file type, just show the application
                w.show();
            }
        }
    } else {
        w.show();
    }
    return app.exec();
}
