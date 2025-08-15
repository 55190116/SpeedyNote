#ifndef PICTUREWINDOWMANAGER_H
#define PICTUREWINDOWMANAGER_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QRect>
#include <QVariantMap>
#include <QPainter>

class PictureWindow;
class InkCanvas;

class PictureWindowManager : public QObject {
    Q_OBJECT

public:
    explicit PictureWindowManager(InkCanvas *canvas, QObject *parent = nullptr);
    ~PictureWindowManager();

    // Picture window management
    PictureWindow* createPictureWindow(const QRect &rect, const QString &imagePath);
    void removePictureWindow(PictureWindow *window);
    void clearAllWindows();

    // Page management
    void saveWindowsForPage(int pageNumber);
    void loadWindowsForPage(int pageNumber);
    void deleteWindowsForPage(int pageNumber);
    void hideAllWindows();

    // Selection mode (for placing new pictures)
    void setSelectionMode(bool enabled);
    bool isSelectionMode() const { return selectionMode; }

    // Current page windows
    QList<PictureWindow*> getCurrentPageWindows() const;
    void exitAllEditModes();
    
    // Canvas rendering
    void renderPicturesToCanvas(QPainter &painter) const;
    void renderPicturesToCanvas(QPainter &painter, const QRect &updateRect) const;
    
    // Hit testing
    PictureWindow* hitTest(const QPoint &canvasPos) const;

    // File management
    QString copyImageToNotebook(const QString &sourcePath, int pageNumber);
    void cleanupUnusedImages();
    void clearCurrentPageWindows(); // Clear all pictures from current page

signals:
    void windowCreated(PictureWindow *window);
    void windowRemoved(PictureWindow *window);

public slots:
    void onWindowDeleteRequested(PictureWindow *window);
    void updateAllWindowPositions();

private:
    // Helper methods
    QString getPictureDataFilePath(int pageNumber) const;
    void savePictureData(int pageNumber, const QList<PictureWindow*> &windows);
    QList<PictureWindow*> loadPictureData(int pageNumber);
    
    QString getSaveFolder() const;
    QString getNotebookId() const;
    QString generateImageFileName(const QString &originalPath, int pageNumber);
    
    void connectWindowSignals(PictureWindow *window);
    QRect convertScreenToCanvasRect(const QRect &screenRect) const;

    // Data members
    InkCanvas *canvas;
    QList<PictureWindow*> currentWindows;
    QMap<int, QList<PictureWindow*>> pageWindows;
    bool selectionMode;
};

#endif // PICTUREWINDOWMANAGER_H
