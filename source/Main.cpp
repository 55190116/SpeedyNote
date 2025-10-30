#ifdef _WIN32
#include <windows.h>
#endif

#include <QApplication>
#include <QPalette>
#include <QTranslator>
#include <QLocale>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QColor>
#include "MainWindow.h"
#include "LauncherWindow.h"
#include "SpnPackageManager.h"
#include "InkCanvas.h" // For BackgroundStyle enum

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

// Helper function to detect Windows dark mode
static bool isWindowsDarkMode() {
#ifdef Q_OS_WIN
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                       QSettings::NativeFormat);
    int appsUseLightTheme = settings.value("AppsUseLightTheme", 1).toInt();
    return (appsUseLightTheme == 0);
#else
    return false;
#endif
}

// Helper function to detect Windows 11
static bool isWindows11() {
#ifdef Q_OS_WIN
    // Windows 11 is build 22000 or higher
    // Use QSysInfo to check Windows version
    return QSysInfo::kernelVersion().split('.')[2].toInt() >= 22000;
#else
    return false;
#endif
}

// Helper function to apply dark/light palette to Qt application
static void applySystemPalette(QApplication &app) {
#ifdef Q_OS_WIN
    // Windows 11 has native dark/light mode support with WinUI 3, so use default style
    // For older Windows versions, use Fusion style for proper dark mode support
    if (!isWindows11()) {
        if (isWindowsDarkMode()) {
            app.setStyle("Fusion");
        } else {
            app.setStyle("windowsvista");
        }
    }

    if (isWindowsDarkMode()) {
        // Create a comprehensive dark palette for Qt widgets
        QPalette darkPalette;

        // Base colors
        QColor darkGray(53, 53, 53);
        QColor gray(128, 128, 128);
        QColor black(25, 25, 25);
        QColor blue(42, 130, 218);
        QColor lightGray(180, 180, 180);

        // Window colors (main background)
        darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
        darkPalette.setColor(QPalette::WindowText, Qt::white);

        // Base (text input background) colors
        darkPalette.setColor(QPalette::Base, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::AlternateBase, darkGray);
        darkPalette.setColor(QPalette::Text, Qt::white);

        // Tooltip colors
        darkPalette.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);

        // Button colors (critical for dialogs)
        darkPalette.setColor(QPalette::Button, darkGray);
        darkPalette.setColor(QPalette::ButtonText, Qt::white);

        // 3D effects and borders (critical for proper widget rendering)
        darkPalette.setColor(QPalette::Light, QColor(80, 80, 80));
        darkPalette.setColor(QPalette::Midlight, QColor(65, 65, 65));
        darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::Mid, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));

        // Bright text
        darkPalette.setColor(QPalette::BrightText, Qt::red);

        // Link colors
        darkPalette.setColor(QPalette::Link, blue);
        darkPalette.setColor(QPalette::LinkVisited, QColor(blue).lighter());

        // Highlight colors (selection)
        darkPalette.setColor(QPalette::Highlight, blue);
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);

        // Placeholder text (for line edits, spin boxes, etc.)
        darkPalette.setColor(QPalette::PlaceholderText, gray);

        // Disabled colors (all color groups)
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Base, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Button, QColor(50, 50, 50));
        darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));

        app.setPalette(darkPalette);
    } else {
        // Use default Windows style and palette for light mode
        app.setPalette(QPalette());
    }
#else
    // On Linux, Qt usually handles this correctly via the desktop environment
    // So we don't override the palette
#endif
}

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
    
    // High DPI scaling is automatically enabled in Qt 6+
    // No need to set AA_EnableHighDpiScaling or AA_UseHighDpiPixmaps

    // Apply system-appropriate palette (dark/light) on Windows
    applySystemPalette(app);

    // ✅ DISK CLEANUP: Clean up orphaned temp directories from previous sessions on startup
    // Since .spn files are extracted to temp folders with hash-based names, orphaned folders
    // from crashes/force-close can accumulate. This cleanup runs on startup to free disk space.
    SpnPackageManager::cleanupOrphanedTempDirs();
    
    QTranslator translator;
    
    // Check for manual language override
    QSettings settings("SpeedyNote", "App");
    bool useSystemLanguage = settings.value("useSystemLanguage", true).toBool();
    QString langCode;
    
    if (useSystemLanguage) {
        // Use system language
        QString locale = QLocale::system().name(); // e.g., "zh_CN", "es_ES"
        langCode = locale.section('_', 0, 0); // e.g., "zh"
    } else {
        // Use manual override
        langCode = settings.value("languageOverride", "en").toString();
    }

    // Debug: Uncomment these lines to debug translation loading issues
    // printf("Locale: %s\n", locale.toStdString().c_str());
    // printf("Language Code: %s\n", langCode.toStdString().c_str());

    // Try multiple paths to find translation files
    QStringList translationPaths = {
        QCoreApplication::applicationDirPath(),  // Same directory as executable (Windows/portable)
        QCoreApplication::applicationDirPath() + "/translations",  // translations subdirectory (Windows)
        "/usr/share/speedynote/translations",  // Standard Linux installation path
        "/usr/local/share/speedynote/translations",  // Local Linux installation path
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, "speedynote/translations", QStandardPaths::LocateDirectory),  // XDG data directories
        ":/resources/translations"  // Qt resource system (fallback)
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
                SDL_Quit(); // Clean up SDL
                return 0; // Exit successfully
            } else {
                SDL_Quit(); // Clean up SDL
                return 1; // Exit with error code
            }
        }
        SDL_Quit(); // Clean up SDL
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
                SDL_Quit(); // Clean up SDL before exiting
                return 0; // Exit successfully, command sent to existing instance
            }
        }
        // If no command to send or sending failed, just exit
        SDL_Quit(); // Clean up SDL before exiting
        return 0;
    }

    // Determine which window to show based on command line arguments
    int exitCode = 0;
    
    if (!inputFile.isEmpty()) {
        // If a file is specified, go directly to MainWindow
        // Allocate on heap to avoid destructor issues on exit
        MainWindow *w = new MainWindow();
        w->setAttribute(Qt::WA_DeleteOnClose); // Qt will delete when closed
        
        if (createNewPackage) {
            // Handle --create-new command
            if (inputFile.toLower().endsWith(".spn")) {
                w->show(); // Show window first
                w->createNewSpnPackage(inputFile);
            } else {
                // Invalid file extension for new package
                w->show();
            }
        } else {
            // Check file extension to determine how to handle it
            if (inputFile.toLower().endsWith(".pdf")) {
                // Handle PDF file association
                w->show(); // Show window first for dialog parent
                w->openPdfFile(inputFile);
            } else if (inputFile.toLower().endsWith(".spn")) {
                // Handle SpeedyNote package
                w->show(); // Show window first
                w->openSpnPackage(inputFile);
            } else {
                // Unknown file type, just show the application
                w->show();
            }
        }
        exitCode = app.exec();
    } else {
        // No file specified - show the launcher window
        // Create the launcher and set it as the shared instance
        LauncherWindow *launcher = new LauncherWindow();
        MainWindow::sharedLauncher = launcher; // Set as shared instance
        launcher->show();
        exitCode = app.exec();
    }
    
    // Clean up SDL before exiting to properly release HID device handles
    // This is especially important on macOS where HID handles can remain locked
    SDL_Quit();
    
    return exitCode;
}
